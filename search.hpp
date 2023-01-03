#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include <atomic>
#include <climits>

enum GameProgress {
    MIDGAME,
    ENDGAME
};

constexpr int piece_values[NPIECE_TYPES] = {
    100,    // PAWN
    300,    // KNIGHT
    300,    // BISHOP
    500,    // ROOK
    900,    // QUEEN
    2000000 // KING
};

GameProgress get_progress(int mv1, int mv2);

template <Color Us>
int evaluate(const Position& pos);

template <Color Us>
int see(const Position& pos, Square square);

template <Color Us>
int maxi(Position& pos, int alpha, int beta, unsigned int depth, const std::atomic<bool>& stop);

template <Color Us>
int mini(Position& pos, int alpha, int beta, unsigned int depth, const std::atomic<bool>& stop);

template <Color Us, typename DurationT>
Move find_best_move(Position& pos, DurationT search_time, unsigned int starting_depth, tp::ThreadPool& pool, int* best_move_score_ret, unsigned int* best_move_depth_ret) {
    MoveList<Us> moves(pos);
    if (moves.size() == 1) {
        *best_move_score_ret = 0;
        *best_move_depth_ret = 0;
        return *moves.begin();
    }

    Move best_move;
    int best_move_score = INT_MIN;
    unsigned int best_move_depth = 0;
    std::atomic<bool> stop(false);

    std::thread deepening_thread([&pos, starting_depth, &pool, &moves, &best_move, &best_move_score, &best_move_depth, &stop]() {
        std::vector<std::shared_ptr<tp::Task>> tasks;
        for (unsigned int current_depth = starting_depth; !stop && current_depth < 256; current_depth++) {
            std::mutex current_mtx;
            Move current_best_move;
            int current_best_move_score = INT_MIN;

            for (Move move : moves) {
                tasks.push_back(pool.schedule([pos, current_depth, move, &current_mtx, &current_best_move, &current_best_move_score, &stop](void*) mutable {
                    pos.play<Us>(move);
                    int score = mini<Us>(pos, INT_MIN, INT_MAX, current_depth - 1, stop);
                    pos.undo<Us>(move);
                    if (!stop) {
                        current_mtx.lock();
                        if (score > current_best_move_score) {
                            current_best_move = move;
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
            }
        }
    });

    std::this_thread::sleep_for(search_time);
    while (best_move_depth == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
    stop = true;
    deepening_thread.join();

    *best_move_score_ret = best_move_score;
    *best_move_depth_ret = best_move_depth;
    return best_move;
}
