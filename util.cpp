#include "util.hpp"

Logger logger("/tmp/orca.log", LogLevel::Info | LogLevel::Error);

GameProgress get_progress(int mv1, int mv2) {
    return (mv1 <= 1300 && mv2 <= 1300) ? ENDGAME : MIDGAME;
}

bool has_non_pawn_material(const Position& pos, Color c) {
    for (PieceType i = KNIGHT; i < NPIECE_TYPES - 1; ++i) {
        if (pos.bitboard_of(c, i)) {
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
