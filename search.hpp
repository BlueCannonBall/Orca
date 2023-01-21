#pragma once

#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include "uci.hpp"
#include "util.hpp"
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
typedef Move KillerMoves[NCOLORS][256][3];

template <Color Us>
int alpha_beta(Position& pos, int alpha, int beta, int depth, TT& tt, KillerMoves& killer_moves, const std::atomic<bool>& stop);

template <Color Us>
int quiesce(Position& pos, int alpha, int beta, int depth, const TT& tt, const KillerMoves& killer_moves, const std::atomic<bool>& stop);

template <Color Us, typename DurationT>
Move find_best_move(uci::Engine* engine, Position& pos, DurationT search_time, tp::ThreadPool& pool, int* best_move_score_ret = nullptr, int* best_move_depth_ret = nullptr) {
    Move moves[218];
    Move* last_move = pos.generate_legals<Us>(moves);
    if (last_move - moves == 1) {
        if (best_move_score_ret) *best_move_score_ret = 0;
        if (best_move_depth_ret) *best_move_depth_ret = 0;
        return moves[0];
    }

    std::vector<TT> tts(last_move - moves);
    std::vector<KillerMoves> killer_move_lists(last_move - moves);

    Move best_move;
    int best_move_score = INT_MIN;
    int best_move_depth = 0;
    std::atomic<bool> stop(false);

    std::thread deepening_thread([engine, &pos, &pool, &moves, last_move, &tts, &killer_move_lists, &best_move, &best_move_score, &best_move_depth, &stop]() {
        std::vector<std::shared_ptr<tp::Task>> tasks;
        for (int current_depth = 1; !stop && current_depth < 256; current_depth++) {
            std::mutex current_mtx;
            Move current_best_move;
            int current_best_move_score = INT_MIN;

            for (Move* move = moves; move != last_move; move++) {
                tasks.push_back(pool.schedule([pos, &moves, &tts, &killer_move_lists, current_depth, move, &current_mtx, &current_best_move, &current_best_move_score, &stop](void*) mutable {
                    TT& tt = tts[move - moves];
                    KillerMoves& killer_moves = killer_move_lists[move - moves];

                    pos.play<Us>(*move);
                    int score = -alpha_beta<~Us>(pos, -piece_values[KING] * 2, piece_values[KING] * 2, current_depth - 1, tt, killer_moves, stop);
                    pos.undo<Us>(*move);

                    if (!stop) {
                        current_mtx.lock();
                        if (score > current_best_move_score) {
                            current_best_move = *move;
                            current_best_move_score = score;
                        }
                        current_mtx.unlock();
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
                best_move_score = current_best_move_score;
                best_move_depth = current_depth;
                engine->send_message("info", {"depth", std::to_string(best_move_depth), "score", "cp", std::to_string(best_move_score)});
            }
        }
    });

    std::this_thread::sleep_for(search_time);
    while (best_move_depth == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    stop = true;
    deepening_thread.join();

    if (best_move_score_ret) *best_move_score_ret = best_move_score;
    if (best_move_depth_ret) *best_move_depth_ret = best_move_depth;
    return best_move;
}
