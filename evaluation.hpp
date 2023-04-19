#pragma once

#include "surge/src/position.h"
#include "surge/src/types.h"

constexpr int piece_values[NPIECE_TYPES] = {
    100,    // PAWN
    300,    // KNIGHT
    305,    // BISHOP
    500,    // ROOK
    900,    // QUEEN
    2000000 // KING
};

template <Color Us>
int evaluate_basic(const Position& pos);

int evaluate_nn(const Position& pos);

template <Color Us>
int evaluate_nnue(const Position& pos);

template <Color Us>
int evaluate(const Position& pos, bool debug = false);

template <Color Us>
int see(const Position& pos, Move move);

int mvv_lva(const Position& pos, Move move);
