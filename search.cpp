#include "search.hpp"

template <Color C>
int evaluate(const Position& pos) {
    int ret = 0;

    // EVAL: Material value
    for (size_t i = 0; i < NPIECE_TYPES - 1; i++) {
        ret += __builtin_popcountll(pos.bitboard_of(C, (PieceType) i)) * piece_values[i];
    }
    for (size_t i = 0; i < NPIECE_TYPES - 1; i++) {
        ret -= __builtin_popcountll(pos.bitboard_of(~C, (PieceType) i)) * piece_values[i];
    }

    return ret;
}

template <Color Us>
int negamax(Position& pos, int alpha, int beta, unsigned int depth, TranspositionTable& transposition_table) {
    // TranspositionTable::iterator transposition_compound_it = transposition_table.find(pos.get_hash());
    // if (transposition_compound_it != transposition_table.end() && transposition_compound_it->second.depth >= depth) {
    //     return transposition_compound_it->second.score;
    // }

    if (depth == 0) {
        int evaluation = evaluate<Us>(pos);
        // if (transposition_compound_it == transposition_table.end()) {
        //     transposition_table[pos.get_hash()] = TranspositionCompound(evaluation, depth);
        // }
        return evaluation;
    }

    int ret;
    MoveList<Us> children(pos);
    if (children.size() == 0) {
        ret = -piece_values[KING] + depth;
    } else {
        for (Move child : children) {
            pos.play<Us>(child);
            int score = -negamax<~Us>(pos, -beta, -alpha, depth - 1, transposition_table);
            pos.undo<Us>(child);

            if (score >= beta) {
                ret = beta;
                goto cutoff;
            } else if (score > alpha) {
                alpha = score;
            }
        }
        ret = alpha;
    }

cutoff:
    // if (transposition_compound_it != transposition_table.end()) {
    //     if (depth < transposition_compound_it->second.depth) {
    //         transposition_compound_it->second = TranspositionCompound(ret, depth);
    //     }
    // } else {
    //     transposition_table[pos.get_hash()] = TranspositionCompound(ret, depth);
    // }
    return ret;
}

template <Color Us>
Move find_best_move(Position& pos, unsigned int depth, tp::ThreadPool& pool, int* best_move_score_ret) {
    MoveList<Us> children(pos);

    Move best_move;
    int best_move_score = -piece_values[KING];

    std::mutex mtx;
    std::vector<std::shared_ptr<tp::Task>> tasks;

    for (Move child : children) {
        tasks.push_back(pool.schedule([pos, depth, child, &best_move, &best_move_score, &mtx](void*) mutable {
            TranspositionTable transposition_table;
            pos.play<Us>(child);
            int score = -negamax<~Us>(pos, -piece_values[KING], piece_values[KING], depth - 1, transposition_table);
            mtx.lock();
            if (score > best_move_score) {
                best_move = child;
                best_move_score = score;
            }
            mtx.unlock();
        }));
    }

    for (auto& task : tasks) {
        task->await();
    }

    *best_move_score_ret = best_move_score;
    return best_move;
}

template int evaluate<WHITE>(const Position& pos);
template int evaluate<BLACK>(const Position& pos);

template int negamax<WHITE>(Position& pos, int alpha, int beta, unsigned int depth, TranspositionTable& transposition_table);
template int negamax<BLACK>(Position& pos, int alpha, int beta, unsigned int depth, TranspositionTable& transposition_table);

template Move find_best_move<WHITE>(Position& pos, unsigned int depth, tp::ThreadPool& pool, int* best_move_score_ret);
template Move find_best_move<BLACK>(Position& pos, unsigned int depth, tp::ThreadPool& pool, int* best_move_score_ret);
