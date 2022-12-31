#include "surge/src/position.h"
#include "surge/src/types.h"
#include <climits>

constexpr int piece_values[NPIECE_TYPES] = {
    100,    // PAWN
    300,    // KNIGHT
    305,    // BISHOP
    500,    // ROOK
    900,    // QUEEN
    2000000 // KING
};

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
int maxi(Position& pos, int alpha, int beta, unsigned int depth);
template <Color Us>
int mini(Position& pos, int alpha, int beta, unsigned int depth);

template <Color Us>
int maxi(Position& pos, int alpha, int beta, unsigned int depth) {
    if (depth == 0) {
        return evaluate<Us>(pos);
    }

    MoveList<Us> children(pos);
    if (children.size() == 0) {
        return -piece_values[KING] + depth;
    }
    for (Move child : children) {
        pos.play<Us>(child);
        int score = mini<Us>(pos, alpha, beta, depth - 1);
        pos.undo<Us>(child);
        if (score >= beta) {
            return beta;
        } else if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

template <Color Us>
int mini(Position& pos, int alpha, int beta, unsigned int depth) {
    if (depth == 0) {
        return evaluate<Us>(pos);
    }

    MoveList<~Us> children(pos);
    if (children.size() == 0) {
        return piece_values[KING] - depth;
    }
    for (Move child : children) {
        pos.play<~Us>(child);
        int score = maxi<Us>(pos, alpha, beta, depth - 1);
        pos.undo<~Us>(child);
        if (score <= alpha) {
            return alpha;
        } else if (score < beta) {
            beta = score;
        }
    }

    return beta;
}

template <Color Us>
Move find_best_move(Position& pos, unsigned int depth) {
    MoveList<Us> children(pos);
    Move best_move;
    int best_move_score = -piece_values[KING];
    for (Move child : children) {
        pos.play<Us>(child);
        int score = mini<Us>(pos, -piece_values[KING], piece_values[KING], depth - 1);
        pos.undo<Us>(child);
        if (score > best_move_score) {
            best_move = child;
            best_move_score = score;
        }
    }
    return best_move;
}
