#pragma once

#include "chess.hpp"
#include "nnue.hpp"

constexpr int piece_values[6] = {
    100,  // PAWN
    300,  // KNIGHT
    305,  // BISHOP
    500,  // ROOK
    900,  // QUEEN
    20000 // KING
};

constexpr int get_value(chess::PieceType pt) {
    return piece_values[(uint8_t) pt];
}

int evaluate_basic(const chess::Board& pos);

int evaluate_nnue(const nnue::Board& pos);

int evaluate(const chess::Board& pos, bool debug = false);

int see(const chess::Board& board, const chess::Move& move, bool debug = false);

int mvv_lva(const chess::Board& board, const chess::Move& move);
