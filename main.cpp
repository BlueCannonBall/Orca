#include "evaluation.hpp"
#include "logger.hpp"
#include "search.hpp"
#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include "uci.hpp"
#include "util.hpp"
#include <boost/atomic.hpp>
#include <boost/fiber/unbuffered_channel.hpp>
#include <boost/thread.hpp>
#include <chrono>
#include <cmath>
#include <map>
#include <prophet.h>
#include <string>
#include <vector>

void worker(boost::fibers::unbuffered_channel<Search>& channel, boost::atomic<bool>& stop) {
    tp::ThreadPool pool;
    boost::mutex mtx;
    std::map<boost::thread::id, TT> tts;
    std::map<boost::thread::id, Prophet*> prophets;
    Search search;
    while (channel.pop(search) == boost::fibers::channel_op_status::success) {
        if (search.new_game) {
            tts.clear();
        } else if (search.quit) {
            return;
        }

        stop.store(false, boost::memory_order_relaxed);

        Color us = search.pos.turn();

        Move moves[218];
        Move* last_move = DYN_COLOR_CALL(search.pos.generate_legals, us, moves);
        if (last_move - moves == 1) {
            uci::bestmove(moves[0]);
            continue;
        }

        std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
        std::vector<Finder> finders(last_move - moves, Finder(start_time, search, stop));

        const auto is_stopping = [start_time, &search, &stop](int depth) {
            return depth > 1 && (std::chrono::steady_clock::now() - start_time > search.time || stop.load(boost::memory_order_relaxed));
        };

        Move best_move;
        Move ponder_move;
        unsigned long long nodes = 0;
        int max_game_ply = search.target_depth == -1 ? NHISTORY : (search.pos.game_ply + search.target_depth);

        for (int depth = 1; !is_stopping(depth) && search.pos.game_ply + depth <= max_game_ply && depth <= 256; depth++) {
            Move current_best_move;
            int current_best_move_score = INT_MIN;
            int current_best_move_static_evaluation = INT_MIN;

            std::vector<std::shared_ptr<tp::Task>> tasks;

            for (Move* move_ptr = moves; move_ptr != last_move; move_ptr++) {
                Move move = *move_ptr;
                tasks.push_back(pool.schedule([&mtx, &tts, &prophets, us, depth, &current_best_move, &current_best_move_score, &current_best_move_static_evaluation, move](void* data) {
                    Finder* finder = (Finder*) data;
                    finder->starting_depth = depth;
                    finder->nodes = 0;
                    finder->tt = &tts[boost::this_thread::get_id()];

                    decltype(prophets)::iterator prophet_it;
                    mtx.lock();
                    if ((prophet_it = prophets.find(boost::this_thread::get_id())) != prophets.end()) {
                        finder->accept_prophet(prophet_it->second);
                    } else {
                        Prophet* new_prophet = raise_prophet(nullptr);
                        prophets[boost::this_thread::get_id()] = new_prophet;
                        finder->accept_prophet(new_prophet);
                    }
                    mtx.unlock();

                    int score;
                    int static_evaluation;
                    RT::const_iterator entry_it;
                    DYN_COLOR_CALL(finder->search.pos.play, us, move);
                    static_evaluation = DYN_COLOR_CALL(evaluate_nnue, us, finder->search.pos);
                    if ((entry_it = finder->search.rt.find(finder->search.pos.get_hash())) != finder->search.rt.end() && entry_it->second + 1 == 3) {
                        score = 0;
                    } else {
                        bool repetition = false;
                        Move nested_moves[218];
                        Move* last_nested_move = DYN_COLOR_CALL(finder->search.pos.generate_legals, ~us, nested_moves);
                        for (Move* nested_move = nested_moves; nested_move != last_nested_move; nested_move++) {
                            RT::const_iterator nested_entry_it;
                            DYN_COLOR_CALL(finder->search.pos.play, ~us, *nested_move);
                            if ((nested_entry_it = finder->search.rt.find(finder->search.pos.get_hash())) != finder->search.rt.end() && nested_entry_it->second + 1 == 3) {
                                repetition = true;
                                DYN_COLOR_CALL(finder->search.pos.undo, ~us, *nested_move);
                                break;
                            }
                            DYN_COLOR_CALL(finder->search.pos.undo, ~us, *nested_move);
                        }
                        if (repetition) {
                            score = 0;
                        } else {
                            score = -DYN_COLOR_CALL(finder->alpha_beta, ~us, -piece_values[KING] * 2, piece_values[KING] * 2, depth - 1);
                        }
                    }
                    DYN_COLOR_CALL(finder->search.pos.undo, us, move);

                    if (!finder->is_stopping()) {
                        mtx.lock();
                        if (score > current_best_move_score) {
                            current_best_move = move;
                            current_best_move_score = score;
                            current_best_move_static_evaluation = static_evaluation;
                        } else if (score == current_best_move_score && static_evaluation > current_best_move_static_evaluation) {
                            current_best_move = move;
                            current_best_move_score = score;
                            current_best_move_static_evaluation = static_evaluation;
                        }
                        mtx.unlock();
                    }
                },
                    &finders[move_ptr - moves]));
            }

            for (const auto& task : tasks) {
                auto status = task->await();
                if (status == tp::CommandStatus::Failure) {
                    throw task->error;
                }
            }

            if (!is_stopping(depth)) {
                best_move = current_best_move;
                nodes += last_move - moves;
                for (const auto& finder : finders) {
                    nodes += finder.nodes;
                }

                DYN_COLOR_CALL(search.pos.play, us, best_move);
                std::vector<Move> pv = get_pv(search.pos, finders[std::find(moves, last_move, best_move) - moves].tt);
                pv.insert(pv.begin(), best_move);
                pv.resize(std::min((int) pv.size(), depth));
                if (pv.size() > 1) {
                    ponder_move = pv[1];
                }
                DYN_COLOR_CALL(search.pos.undo, us, best_move);
                std::vector<std::string> pv_strings;
                std::transform(pv.cbegin(), pv.cend(), std::back_inserter(pv_strings), uci::format_move);

                std::chrono::milliseconds time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
                unsigned long long nps = (nodes / std::max(time_elapsed.count(), 1l)) * 1000;

                std::vector<std::string> args;
                if (current_best_move_score >= piece_values[KING] - NHISTORY) {
                    args = {"depth", std::to_string(depth), "score", "mate", std::to_string((piece_values[KING] - current_best_move_score + 1) / 2), "nodes", std::to_string(nodes), "time", std::to_string(time_elapsed.count()), "nps", std::to_string(nps), "pv"};
                } else if (current_best_move_score <= -piece_values[KING] + NHISTORY) {
                    args = {"depth", std::to_string(depth), "score", "mate", std::to_string(-(current_best_move_score + piece_values[KING]) / 2), "nodes", std::to_string(nodes), "time", std::to_string(time_elapsed.count()), "nps", std::to_string(nps), "pv"};
                } else {
                    args = {"depth", std::to_string(depth), "score", "cp", std::to_string(current_best_move_score), "nodes", std::to_string(nodes), "time", std::to_string(time_elapsed.count()), "nps", std::to_string(nps), "pv"};
                }
                args.insert(args.end(), pv_strings.begin(), pv_strings.end());
                uci::send_message("info", args);
            }
        }

        uci::bestmove(best_move, ponder_move);

        for (auto& tt : tts) {
            for (auto entry_it = tt.second.begin(); entry_it != tt.second.end();) {
                if (--entry_it->second.depth <= 0) {
                    entry_it = tt.second.erase(entry_it);
                } else {
                    ++entry_it;
                }
            }
        }
    }
}

int main() {
    initialise_all_databases();
    zobrist::initialise_zobrist_keys();

    logger.info("Engine started");

#if defined(ORCA_TIMESTAMP) && defined(ORCA_COMPILER)
    std::cout << "Orca NNUE (mono-accumulator 1x768 feature space i16 quantized eval with 64x scaling factor) compiled @ " << ORCA_TIMESTAMP << " on compiler " << ORCA_COMPILER << std::endl;
#endif

    Position pos(DEFAULT_FEN);
    RT rt;
    bool new_game = false;

    boost::atomic<bool> stop(false);
    boost::fibers::unbuffered_channel<Search> channel;
    boost::thread worker_thread(std::bind(worker, std::ref(channel), std::ref(stop)));

    for (;;) {
        auto message = uci::poll();

        str_switch(message.command) {
            str_case("uci"):
            {
                uci::send_message("id", {"name", "Orca"});
                uci::send_message("id", {"author", "BlueCannonBall"});
                uci::send_message("uciok");
                break;
            }

            str_case("isready"):
            {
                uci::send_message("readyok");
                break;
            }

            str_case("ucinewgame"):
            {
                new_game = true;
                break;
            }

            str_case("position"):
            {
                rt.clear();
                if (message.args[0] == "startpos") {
                    pos = Position(DEFAULT_FEN);
                    rt[pos.get_hash()] = 1;
                    if (message.args.size() > 2) {
                        for (size_t i = 2; i < message.args.size(); i++) {
                            if (((i - 2) % 2) == 0) {
                                pos.play<WHITE>(uci::parse_move<WHITE>(pos, message.args[i]));
                            } else {
                                pos.play<BLACK>(uci::parse_move<BLACK>(pos, message.args[i]));
                            }

                            RT::iterator entry_it;
                            if ((entry_it = rt.find(pos.get_hash())) != rt.end()) {
                                entry_it->second++;
                            } else {
                                rt[pos.get_hash()] = 1;
                            }
                        }
                    }
                } else if (message.args[0] == "fen") {
                    std::string fen;
                    for (size_t i = 1; i < message.args.size(); i++) {
                        fen += message.args[i];
                        if (i + 1 != message.args.size()) {
                            fen.push_back(' ');
                        }
                    }
                    pos = Position(fen);
                    rt[pos.get_hash()] = 1;
                    if (message.args.size() > 8) {
                        for (size_t i = 8; i < message.args.size(); i++) {
                            if (((i - 8) % 2) == 0) {
                                pos.play<WHITE>(uci::parse_move<WHITE>(pos, message.args[i]));
                            } else {
                                pos.play<BLACK>(uci::parse_move<BLACK>(pos, message.args[i]));
                            }

                            RT::iterator entry_it;
                            if ((entry_it = rt.find(pos.get_hash())) != rt.end()) {
                                entry_it->second++;
                            } else {
                                rt[pos.get_hash()] = 1;
                            }
                        }
                    }
                }
                break;
            }

            str_case("go"):
            {
                using namespace std::chrono_literals;

                std::chrono::milliseconds search_time = 10s;

                std::chrono::milliseconds movetime = 0ms;
                std::chrono::milliseconds wtime = 0ms;
                std::chrono::milliseconds btime = 0ms;
                std::chrono::milliseconds winc = 0ms;
                std::chrono::milliseconds binc = 0ms;
                bool infinite = false;
                bool ponder = false;
                int depth = -1;
                for (auto it = message.args.begin(); it != message.args.end(); it++) {
                    str_switch(*it) {
                        str_case("movetime"):
                        {
                            movetime = std::chrono::milliseconds(stoi(*++it));
                            break;
                        }
                        str_case("wtime"):
                        {
                            wtime = std::chrono::milliseconds(stoi(*++it));
                            break;
                        }
                        str_case("btime"):
                        {
                            btime = std::chrono::milliseconds(stoi(*++it));
                            break;
                        }
                        str_case("winc"):
                        {
                            winc = std::chrono::milliseconds(stoi(*++it));
                            break;
                        }
                        str_case("binc"):
                        {
                            binc = std::chrono::milliseconds(stoi(*++it));
                            break;
                        }
                        str_case("infinite"):
                        {
                            infinite = true;
                            break;
                        }
                        str_case("ponder"):
                        {
                            ponder = true;
                            break;
                        }
                        str_case("depth"):
                        {
                            depth = stoi(*++it);
                            break;
                        }
                    }
                }

                if (infinite || depth != -1) {
                    search_time = 10h;
                } else {
                    float moves_made = std::floor(pos.game_ply / 2.f);
                    int moves_left = 40;
                    if (moves_made < 60.f) {
                        moves_left = std::round(-0.5f * moves_made + 40.f);
                    } else if ((float) pos.game_ply / 2.f >= 60.f) {
                        moves_left = std::round(0.1f * (moves_made - 60.f) + 10.f);
                    }

                    if (pos.turn() == WHITE) {
                        if (movetime != 0ms) {
                            search_time = movetime;
                        } else if (wtime != 0ms) {
                            search_time = std::min(wtime / moves_left, 30000ms);
                        }

                        if (std::max(search_time - 250ms, 0ms) < winc) {
                            search_time = winc - 250ms;
                        }
                    } else if (pos.turn() == BLACK) {
                        if (movetime != 0ms) {
                            search_time = movetime;
                        } else if (btime != 0ms) {
                            search_time = std::min(btime / moves_left, 30000ms);
                        }

                        if (std::max(search_time - 250ms, 0ms) < binc) {
                            search_time = binc - 250ms;
                        }
                    } else {
                        throw std::logic_error("Invalid side to move");
                    }

                    if (ponder) search_time *= 2;
                }

                channel.push(Search {
                    .pos = pos,
                    .rt = rt,
                    .time = search_time,
                    .target_depth = depth,
                    .new_game = new_game,
                });
                new_game = false;

                break;
            }

            str_case("stop"):
            {
                stop.store(true, boost::memory_order_relaxed);
                break;
            }

            str_case("quit"):
            {
                stop.store(true, boost::memory_order_relaxed);
                channel.push(Search {
                    .quit = true,
                });
                worker_thread.join();
                return 0;
            }

            str_case("show"):
            {
                std::cout << pos << std::endl;
                break;
            }

            str_case("eval"):
                str_case("evaluate"):
            {
                if (pos.turn() == WHITE) {
                    uci::send_message("evaluation", {std::to_string(evaluate<WHITE>(pos, true))});
                } else if (pos.turn() == BLACK) {
                    uci::send_message("evaluation", {std::to_string(evaluate<BLACK>(pos, true))});
                } else {
                    throw std::logic_error("Invalid side to move");
                }
                break;
            }
        }
    }

    return 0;
}
