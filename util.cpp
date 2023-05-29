#include "util.hpp"

Logger logger("/tmp/orca.log", LogLevel::Info | LogLevel::Error);

float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

GameProgress get_progress(int mv1, int mv2) {
    return (mv1 <= 1300 && mv2 <= 1300) ? ENDGAME : MIDGAME;
}

template <Color Us>
bool has_non_pawn_material(const Position& pos) {
    for (PieceType i = KNIGHT; i < NPIECE_TYPES - 1; ++i) {
        if (pos.bitboard_of(Us, i)) {
            return true;
        }
    }
    return false;
}

template <>
MoveFlags generate_move_flags<WHITE>(const Position& pos, Square from, Square to) {
    if (pos.at(to) != NO_PIECE) {
        if (pos.at(from) == WHITE_PAWN && rank_of(to) == RANK8) {
            return PROMOTION_CAPTURES;
        } else {
            return CAPTURE;
        }
    } else if (pos.at(from) == WHITE_PAWN && rank_of(from) == RANK2 && rank_of(to) == RANK4) {
        return DOUBLE_PUSH;
    } else if (pos.at(from) == WHITE_KING && from == e1) {
        if (to == g1) {
            return OO;
        } else if (to == c1) {
            return OOO;
        }
    } else if (pos.at(from) == WHITE_PAWN) {
        if (rank_of(to) == RANK8) {
            return PROMOTIONS;
        } else if (file_of(from) != file_of(to) && pos.at(to) == NO_PIECE) {
            return EN_PASSANT;
        }
    }
    return QUIET;
}

template <>
MoveFlags generate_move_flags<BLACK>(const Position& pos, Square from, Square to) {
    if (pos.at(to) != NO_PIECE) {
        if (pos.at(from) == BLACK_PAWN && rank_of(to) == RANK1) {
            return PROMOTION_CAPTURES;
        } else {
            return CAPTURE;
        }
    } else if (pos.at(from) == BLACK_PAWN && rank_of(from) == RANK7 && rank_of(to) == RANK5) {
        return DOUBLE_PUSH;
    } else if (pos.at(from) == BLACK_KING && from == e8) {
        if (to == g8) {
            return OO;
        } else if (to == c8) {
            return OOO;
        }
    } else if (pos.at(from) == BLACK_PAWN) {
        if (rank_of(to) == RANK1) {
            return PROMOTION_CAPTURES;
        } else if (file_of(from) != file_of(to) && pos.at(to) == NO_PIECE) {
            return EN_PASSANT;
        }
    }
    return QUIET;
}

template <>
MoveFlags generate_attack_move_flags<WHITE>(const Position& pos, Square from, Square to) {
    if (pos.at(to) != NO_PIECE) {
        if (pos.at(from) == WHITE_PAWN && rank_of(to) == RANK8) {
            return PROMOTION_CAPTURES;
        } else {
            return CAPTURE;
        }
    } else if (pos.at(from) == WHITE_PAWN && file_of(from) != file_of(to) && pos.at(to) == NO_PIECE) {
        return EN_PASSANT;
    }
    return QUIET;
}

template <>
MoveFlags generate_attack_move_flags<BLACK>(const Position& pos, Square from, Square to) {
    if (pos.at(to) != NO_PIECE) {
        if (pos.at(from) == BLACK_PAWN && rank_of(to) == RANK1) {
            return PROMOTION_CAPTURES;
        } else {
            return CAPTURE;
        }
    } else if (pos.at(from) == BLACK_PAWN && file_of(from) != file_of(to) && pos.at(to) == NO_PIECE) {
        return EN_PASSANT;
    }
    return QUIET;
}

ProphetBoard generate_prophet_board(const Position& pos) {
    ProphetBoard ret;
    ret.white = pos.all_pieces<WHITE>();
    ret.black = pos.all_pieces<BLACK>();
    ret.pawns = pos.bitboard_of(WHITE_PAWN) | pos.bitboard_of(BLACK_PAWN);
    ret.knights = pos.bitboard_of(WHITE_KNIGHT) | pos.bitboard_of(BLACK_KNIGHT);
    ret.bishops = pos.bitboard_of(WHITE_BISHOP) | pos.bitboard_of(BLACK_BISHOP);
    ret.rooks = pos.bitboard_of(WHITE_ROOK) | pos.bitboard_of(BLACK_ROOK);
    ret.queens = pos.bitboard_of(WHITE_QUEEN) | pos.bitboard_of(BLACK_QUEEN);
    ret.kings = pos.bitboard_of(WHITE_KING) | pos.bitboard_of(BLACK_KING);
    ret.side_to_move = pos.turn();
    return ret;
}

template bool has_non_pawn_material<WHITE>(const Position& pos);
template bool has_non_pawn_material<BLACK>(const Position& pos);
