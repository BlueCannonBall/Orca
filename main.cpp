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
            break;
        }

        std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
        std::vector<Finder> finders(last_move - moves, Finder(start_time, search, stop));

        const auto is_stopping = [start_time, &search, &stop]() {
            return std::chrono::steady_clock::now() - start_time > search.time || stop.load(std::memory_order_relaxed);
        };

        Move best_move;

        for (int depth = 1; !is_stopping() && search.pos.game_ply + depth < 2048; depth++) {
            std::mutex mtx;
            Move current_best_move;
            int current_best_move_score = INT_MIN;

            std::vector<std::shared_ptr<tp::Task>> tasks;

            for (Move* move_ptr = moves; move_ptr != last_move; move_ptr++) {
                Move move = *move_ptr;
                tasks.push_back(pool.schedule([us, depth, move, &mtx, &current_best_move, &current_best_move_score](void* data) {
                    Finder* finder = (Finder*) data;

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

            if (!is_stopping()) {
                best_move = current_best_move;
                unsigned long long nodes = last_move - moves;
                for (const auto& finder : finders) {
                    nodes += finder.tt.size();
                }

                std::vector<std::string> pv;
                const TT& tt = finders[std::find(moves, last_move, best_move) - moves].tt;

                Move current_move = best_move;
                Position current_pos = search.pos;
                for (Color side_to_move = us; current_pos.game_ply < search.pos.game_ply + depth && current_pos.game_ply < 2048; side_to_move = ~side_to_move) {
                    pv.push_back(uci::format_move(current_move));

                    DYN_COLOR_CALL(current_pos.play, side_to_move, current_move);
                    TT::const_iterator entry_it;
                    if ((entry_it = tt.find(current_pos.get_hash())) != tt.end()) {
                        if (entry_it->second.best_move.is_null()) {
                            break;
                        } else {
                            current_move = entry_it->second.best_move;
                        }
                    } else {
                        break;
                    }
                }

                std::vector<std::string> args;
                if (current_best_move_score >= piece_values[KING]) {
                    args = std::vector<std::string> {"depth", std::to_string(depth), "score", "mate", std::to_string((int) std::ceil((depth - (current_best_move_score - piece_values[KING])) / 2.f)), "nodes", std::to_string(nodes), "pv"};
                } else if (current_best_move_score <= -piece_values[KING]) {
                    args = std::vector<std::string> {"depth", std::to_string(depth), "score", "mate", std::to_string((int) std::ceil((-depth + std::abs(current_best_move_score + piece_values[KING])) / 2.f)), "nodes", std::to_string(nodes), "pv"};
                } else {
                    args = std::vector<std::string> {"depth", std::to_string(depth), "score", "cp", std::to_string(current_best_move_score), "nodes", std::to_string(nodes), "pv"};
                }
                args.insert(args.end(), pv.begin(), pv.end());
                uci::send_message("info", args);
            }
        }

        uci::bestmove(best_move);
    }
}

int main() {
    initialise_all_databases();
    zobrist::initialise_zobrist_keys();

    logger.info("Engine started");

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
                std::chrono::milliseconds search_time = std::chrono::seconds(10);

                std::chrono::milliseconds movetime = std::chrono::milliseconds(-1);
                std::chrono::milliseconds wtime = std::chrono::milliseconds(-1);
                std::chrono::milliseconds btime = std::chrono::milliseconds(-1);
                std::chrono::milliseconds winc = std::chrono::milliseconds(0);
                std::chrono::milliseconds binc = std::chrono::milliseconds(0);
                for (auto it = message.args.begin(); it != message.args.end(); it++) {
                    if (*it == "movetime") {
                        movetime = std::chrono::milliseconds(stoi(*++it));
                    } else if (*it == "wtime") {
                        wtime = std::chrono::milliseconds(stoi(*++it));
                    } else if (*it == "btime") {
                        btime = std::chrono::milliseconds(stoi(*++it));
                    } else if (*it == "winc") {
                        winc = std::chrono::milliseconds(stoi(*++it));
                    } else if (*it == "binc") {
                        binc = std::chrono::milliseconds(stoi(*++it));
                    }
                }

                int moves_left;
                if ((float) pos.game_ply / 2.f < 60.f) {
                    moves_left = std::round(((-2.f / 3.f) * ((float) pos.game_ply / 2.f)) + 50.f);
                } else if ((float) pos.game_ply / 2.f >= 60.f) {
                    moves_left = std::round((0.1f * ((float) pos.game_ply / 2.f - 60.f)) + 10.f);
                }

                if (pos.turn() == WHITE) {
                    if (movetime != std::chrono::milliseconds(-1)) {
                        search_time = movetime;
                    } else if (wtime != std::chrono::milliseconds(-1)) {
                        search_time = std::chrono::milliseconds(std::min(wtime.count() / moves_left, 30000l));
                    }

                    if (search_time - std::chrono::milliseconds(500) < winc) {
                        search_time = winc - std::chrono::milliseconds(500);
                    }
                } else if (pos.turn() == BLACK) {
                    if (movetime != std::chrono::milliseconds(-1)) {
                        search_time = movetime;
                    } else if (btime != std::chrono::milliseconds(-1)) {
                        search_time = std::chrono::milliseconds(std::min(btime.count() / moves_left, 30000l));
                    }

                    if (search_time - std::chrono::milliseconds(500) < binc) {
                        search_time = binc - std::chrono::milliseconds(500);
                    }
                } else {
                    throw std::logic_error("Invalid side to move");
                }

                channel.push(Search {
                    .pos = pos,
                    .rt = rt,
                    .time = search_time,
                });

                break;
            }

            str_case("stop") :
            {
                stop.store(true, std::memory_order_relaxed);
                break;
            }

            str_case("quit") :
            {
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
