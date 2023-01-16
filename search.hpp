#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include "uci.hpp"
#include <atomic>
#include <climits>
#include <unordered_map>

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

class TTEntry {
public:
    int score;
    int depth;

    TTEntry() = default;
    TTEntry(int score, int depth) :
        score(score),
        depth(depth) { }
};

typedef std::unordered_map<uint64_t, TTEntry> TT;

GameProgress get_progress(int mv1, int mv2);

template <Color Us>
int evaluate(const Position& pos);

bool see(const Position& pos, Move move, int threshold);

template <Color Us>
int alpha_beta(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop);

template <Color Us>
int quiesce(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop);

template <Color Us, typename DurationT>
Move find_best_move(uci::Engine* engine, Position& pos, DurationT search_time, int starting_depth, tp::ThreadPool& pool, int* best_move_score_ret = nullptr, int* best_move_depth_ret = nullptr) {
    MoveList<Us> moves(pos);
    if (moves.size() == 1) {
        if (best_move_score_ret) *best_move_score_ret = 0;
        if (best_move_depth_ret) *best_move_depth_ret = 0;
        return *moves.begin();
    }

    Move best_move;
    int best_move_score = INT_MIN;
    int best_move_depth = 0;
    std::atomic<bool> stop(false);

    std::thread deepening_thread([engine, &pos, starting_depth, &pool, &moves, &best_move, &best_move_score, &best_move_depth, &stop]() {
        std::vector<std::shared_ptr<tp::Task>> tasks;
        for (int current_depth = starting_depth; !stop && current_depth < 256; current_depth++) {
            std::mutex current_mtx;
            Move current_best_move;
            int current_best_move_score = INT_MIN;

            for (Move move : moves) {
                tasks.push_back(pool.schedule([pos, current_depth, move, &current_mtx, &current_best_move, &current_best_move_score, &stop](void*) mutable {
                    TT tt;

                    pos.play<Us>(move);
                    int score = -alpha_beta<~Us>(pos, -piece_values[KING] * 2, piece_values[KING] * 2, current_depth - 1, tt, stop);
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
                engine->send_message("info", {"depth", std::to_string(best_move_depth), "score", "cp", std::to_string(best_move_score)});
            }
        }
    });

    std::this_thread::sleep_for(search_time);
    while (best_move_depth == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
    stop = true;
    deepening_thread.join();

    if (best_move_score_ret) *best_move_score_ret = best_move_score;
    if (best_move_depth_ret) *best_move_depth_ret = best_move_depth;
    return best_move;
}
