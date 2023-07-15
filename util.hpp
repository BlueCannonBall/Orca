#pragma once

#include "chess.hpp"
#include "logger.hpp"
#include <prophet.h>

#define BOTH_COLORS for (chess::Color color = chess::Color::WHITE; color != chess::Color::WHITE; color = ~color)

extern Logger logger;

enum GameProgress {
    MIDGAME,
    ENDGAME
};

GameProgress get_progress(int mv1, int mv2);

bool has_non_pawn_material(const chess::Board& board, chess::Color color);

chess::Bitboard attackers_for_side(const chess::Board& board, chess::Square sq, chess::Color attacker_color, chess::Bitboard occ);
