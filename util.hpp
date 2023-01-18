#pragma once

#include "surge/src/position.h"
#include "surge/src/types.h"

#define BOTH_COLOR_CALL(function, ...)       (function<WHITE>(__VA_ARGS__) | function<BLACK>(__VA_ARGS__))
#define DYN_COLOR_CALL(function, color, ...) ({                                   \
    color == WHITE ? function<WHITE>(__VA_ARGS__) : function<BLACK>(__VA_ARGS__); \
})

constexpr int piece_values[NPIECE_TYPES] = {
    100,    // PAWN
    300,    // KNIGHT
    300,    // BISHOP
    500,    // ROOK
    900,    // QUEEN
    2000000 // KING
};

enum GameProgress {
    MIDGAME,
    ENDGAME
};

GameProgress get_progress(int mv1, int mv2);

bool has_non_pawn_material(const Position& pos, Color c);

template <Color C>
MoveFlags generate_move_flags(const Position& pos, Square from, Square to);

template <Color C>
MoveFlags generate_attack_move_flags(const Position& pos, Square from, Square to);
