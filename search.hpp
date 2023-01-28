#pragma once

#include "evaluation.hpp"
#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include "uci.hpp"
#include <atomic>
#include <climits>
#include <thread>
#include <unordered_map>
#include <vector>

enum TTEntryFlag {
    EXACT,
    LOWERBOUND,
    UPPERBOUND,
};

class TTEntry {
public:
    int score;
    int depth;
    Move best_move;
    TTEntryFlag flag;

    TTEntry() = default;
    TTEntry(int score, int depth, Move best_move, TTEntryFlag flag) :
        score(score),
        depth(depth),
        best_move(best_move),
        flag(flag) { }
};

typedef std::unordered_map<uint64_t, TTEntry> TT;
typedef Move KillerMoves[NCOLORS][2048][3];
typedef std::unordered_map<uint64_t, unsigned short> RT;

class Finder {
public:
    int max_depth;
    TT tt;
    KillerMoves killer_moves = {{{0}}};

    Finder(int max_depth = 0) :
        max_depth(max_depth) { }

    template <Color Us>
    int alpha_beta(Position& pos, int alpha, int beta, int depth, const std::atomic<bool>& stop);

    template <Color Us>
    int quiesce(Position& pos, int alpha, int beta, int depth, const std::atomic<bool>& stop);

protected:
    template <Color C>
    void add_killer_move(Move move, int depth);

    template <Color Us>
    bool is_killer_move(Move move, int depth) const;
};

template <Color Us, typename DurationT>
void go(uci::Engine* engine, Position& pos, DurationT search_time, const RT& rt, tp::ThreadPool& pool) {
    Move moves[218];
    Move* last_move = pos.generate_legals<Us>(moves);
    if (last_move - moves == 1) {
        engine->move(moves[0]);
    }

    std::vector<Finder> finders(last_move - moves);

    Move best_move;
    std::atomic<bool> produced_move(false);
    std::atomic<bool> stop(false);

    std::thread deepening_thread([engine, &pos, &rt, &pool, &moves, last_move, &finders, &best_move, &produced_move, &stop]() {
        std::vector<std::shared_ptr<tp::Task>> tasks;
        for (int depth = 1; !stop && pos.game_ply + depth < 2048; depth++) {
            std::mutex mtx;
            Move current_best_move;
            int best_move_score = INT_MIN;

            for (Move* move = moves; move != last_move; move++) {
                tasks.push_back(pool.schedule([pos, &rt, &moves, &finders, depth, move, &mtx, &current_best_move, &best_move_score, &stop](void*) mutable {
                    Finder& finder = finders[move - moves];
                    finder.max_depth = depth;

                    int score;
                    pos.play<Us>(*move);
                    RT::const_iterator entry_it;
                    if ((entry_it = rt.find(pos.get_hash())) != rt.end() && entry_it->second + 1 == 3) {
                        score = 0;
                    } else {
                        bool repetition = false;
                        MoveList<~Us> nested_moves(pos);
                        for (Move nested_move : nested_moves) {
                            RT::const_iterator nested_entry_it;
                            pos.play<~Us>(nested_move);
                            if ((nested_entry_it = rt.find(pos.get_hash())) != rt.end() && nested_entry_it->second + 1 == 3) {
                                repetition = true;
                                pos.undo<~Us>(nested_move);
                                break;
                            }
                            pos.undo<~Us>(nested_move);
                        }
                        if (repetition) {
                            score = 0;
                        } else {
                            score = -finder.alpha_beta<~Us>(pos, -piece_values[KING] * 2, piece_values[KING] * 2, depth - 1, stop);
                        }
                    }
                    pos.undo<Us>(*move);

                    if (!stop) {
                        mtx.lock();
                        if (score > best_move_score) {
                            current_best_move = *move;
                            best_move_score = score;
                        }
                        mtx.unlock();
                    }
                }));
            }

            for (const auto& task : tasks) {
                auto status = task->await();
                if (status == tp::CommandStatus::Failure) {
                    throw task->error;
                }
            }

            if (!stop) {
                best_move = current_best_move;
                unsigned long long nodes = last_move - moves;
                for (const auto& finder : finders) {
                    nodes += finder.tt.size();
                }

                std::vector<std::string> pv;
                const TT& tt = finders[std::find(moves, last_move, best_move) - moves].tt;

                Move current_move = best_move;
                Position current_pos = pos;
                for (Color side_to_move = Us; current_pos.game_ply < pos.game_ply + depth && current_pos.game_ply < 2048; side_to_move = ~side_to_move) {
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

                std::vector<std::string> args = {"depth", std::to_string(depth), "score", "cp", std::to_string(best_move_score), "nodes", std::to_string(nodes), "pv"};
                args.insert(args.end(), pv.begin(), pv.end());
                engine->send_message("info", args);
                produced_move = true;
            }
        }
    });

    std::this_thread::sleep_for(search_time);
    while (!produced_move) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    stop = true;
    deepening_thread.join();

    engine->move(best_move);
}
