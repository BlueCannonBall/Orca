#include "evaluation.hpp"
#include "logger.hpp"
#include "search.hpp"
#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include "uci.hpp"
#include "util.hpp"
#include <atomic>
#include <boost/fiber/unbuffered_channel.hpp>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

void worker(boost::fibers::unbuffered_channel<Search>& channel, std::atomic<bool>& stop) {
    tp::ThreadPool pool;
    Search search;
    while (channel.pop(search) == boost::fibers::channel_op_status::success) {
        if (search.quit) {
            return;
        }

        stop.store(false, std::memory_order_relaxed);

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
            return depth > 1 && (std::chrono::steady_clock::now() - start_time > search.time || stop.load(std::memory_order_relaxed));
        };

        Move best_move;
        Move ponder_move;
        int max_game_ply = search.target_depth == -1 ? NHISTORY : (search.pos.game_ply + search.target_depth);

        for (int depth = 1; !is_stopping(depth) && search.pos.game_ply + depth <= max_game_ply; depth++) {
            std::mutex mtx;
            Move current_best_move;
            int current_best_move_score = INT_MIN;

            std::vector<std::shared_ptr<tp::Task>> tasks;

            for (Move* move_ptr = moves; move_ptr != last_move; move_ptr++) {
                Move move = *move_ptr;
                tasks.push_back(pool.schedule([us, depth, move, &mtx, &current_best_move, &current_best_move_score](void* data) {
                    Finder* finder = (Finder*) data;
                    finder->starting_depth = depth;

                    int score;
                    RT::const_iterator entry_it;
                    DYN_COLOR_CALL(finder->search.pos.play, us, move);
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
                unsigned long long nodes = last_move - moves;
                for (const auto& finder : finders) {
                    nodes += finder.tt.size();
                }

                const TT& tt = finders[std::find(moves, last_move, best_move) - moves].tt;
                DYN_COLOR_CALL(search.pos.play, us, best_move);
                std::vector<Move> pv = get_pv(search.pos, tt);
                pv.insert(pv.begin(), best_move);
                pv.resize(std::min((int) pv.size(), depth));
                if (pv.size() > 1) {
                    ponder_move = pv[1];
                }
                DYN_COLOR_CALL(search.pos.undo, us, best_move);
                std::vector<std::string> pv_strings;
                std::transform(pv.cbegin(), pv.cend(), std::back_inserter(pv_strings), uci::format_move);

                std::vector<std::string> args;
                if (current_best_move_score >= piece_values[KING]) {
                    args = {"depth", std::to_string(depth), "score", "mate", std::to_string((int) std::ceil((depth - (current_best_move_score - piece_values[KING])) / 2.f)), "nodes", std::to_string(nodes), "pv"};
                } else if (current_best_move_score <= -piece_values[KING]) {
                    args = {"depth", std::to_string(depth), "score", "mate", std::to_string((int) std::floor((-depth + std::abs(current_best_move_score + piece_values[KING])) / 2.f)), "nodes", std::to_string(nodes), "pv"};
                } else {
                    args = {"depth", std::to_string(depth), "score", "cp", std::to_string(current_best_move_score), "nodes", std::to_string(nodes), "pv"};
                }
                args.insert(args.end(), pv_strings.begin(), pv_strings.end());
                uci::send_message("info", args);
            }
        }

        uci::bestmove(best_move, ponder_move);
    }
}

int main() {
    initialise_all_databases();
    zobrist::initialise_zobrist_keys();

    logger.info("Engine started");

#if defined(ORCA_TIMESTAMP) && defined(ORCA_COMPILER)
    std::cout << "Orca HCE compiled @ " << ORCA_TIMESTAMP << " on compiler " <<  ORCA_COMPILER << std::endl;
#endif

    Position pos(DEFAULT_FEN);
    RT rt;

    std::atomic<bool> stop(false);
    boost::fibers::unbuffered_channel<Search> channel;
    std::thread worker_thread(std::bind(worker, std::ref(channel), std::ref(stop)));

    for (;;) {
        auto message = uci::poll();

        str_switch(message.command) {
            str_case("uci") :
            {
                uci::send_message("id", {"name", "Orca"});
                uci::send_message("id", {"author", "BlueCannonBall"});
                uci::send_message("uciok");
                break;
            }

            str_case("isready") :
            {
                uci::send_message("readyok");
                break;
            }

            str_case("position") :
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

            str_case("go") :
            {
                using namespace std::chrono_literals;

                std::chrono::milliseconds search_time = 10s;

                std::chrono::milliseconds movetime = 0ms;
                std::chrono::milliseconds wtime = 0ms;
                std::chrono::milliseconds btime = 0ms;
                std::chrono::milliseconds winc = 0ms;
                std::chrono::milliseconds binc = 0ms;
                bool infinite = false;
                int depth = -1;
                for (auto it = message.args.begin(); it != message.args.end(); it++) {
                    str_switch(*it) {
                        str_case("movetime") :
                        {
                            movetime = std::chrono::milliseconds(stoi(*++it));
                            break;
                        }
                        str_case("wtime") :
                        {
                            wtime = std::chrono::milliseconds(stoi(*++it));
                            break;
                        }
                        str_case("btime") :
                        {
                            btime = std::chrono::milliseconds(stoi(*++it));
                            break;
                        }
                        str_case("winc") :
                        {
                            winc = std::chrono::milliseconds(stoi(*++it));
                            break;
                        }
                        str_case("binc") :
                        {
                            binc = std::chrono::milliseconds(stoi(*++it));
                            break;
                        }
                        str_case("infinite") :
                            str_case("ponder") :
                        {
                            infinite = true;
                            break;
                        }
                        str_case("depth") :
                        {
                            depth = stoi(*++it);
                            break;
                        }
                    }
                }

                if (infinite || depth != -1) {
                    search_time = 10h;
                } else {
                    int moves_left;
                    if ((float) pos.game_ply / 2.f < 60.f) {
                        moves_left = std::round(((-2.f / 3.f) * ((float) pos.game_ply / 2.f)) + 50.f);
                    } else if ((float) pos.game_ply / 2.f >= 60.f) {
                        moves_left = std::round((0.1f * ((float) pos.game_ply / 2.f - 60.f)) + 10.f);
                    }

                    if (pos.turn() == WHITE) {
                        if (movetime != 0ms) {
                            search_time = movetime;
                        } else if (wtime != 0ms) {
                            search_time = std::min(wtime / moves_left, 30000ms);
                        }

                        if (search_time - 500ms < winc) {
                            search_time = winc - 500ms;
                        }
                    } else if (pos.turn() == BLACK) {
                        if (movetime != 0ms) {
                            search_time = movetime;
                        } else if (btime != 0ms) {
                            search_time = std::min(btime / moves_left, 30000ms);
                        }

                        if (search_time - 500ms < binc) {
                            search_time = binc - 500ms;
                        }
                    } else {
                        throw std::logic_error("Invalid side to move");
                    }
                }

                channel.push(Search {
                    .pos = pos,
                    .rt = rt,
                    .time = search_time,
                    .target_depth = depth,
                });

                break;
            }

            str_case("stop") :
                str_case("ponderhit") :
            {
                stop.store(true, std::memory_order_relaxed);
                break;
            }

            str_case("quit") :
            {
                stop.store(true, std::memory_order_relaxed);
                channel.push(Search {
                    .quit = true,
                });
                worker_thread.join();
                return 0;
            }

            str_case("show") :
            {
                std::cout << pos << std::endl;
                break;
            }

            str_case("eval") :
                str_case("evaluate") :
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
