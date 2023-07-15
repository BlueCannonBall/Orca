#include "util.hpp"

ADD_INCR_OPERATORS_FOR(chess::PieceType);

Logger logger("/tmp/orca.log", LogLevel::Info | LogLevel::Error);

GameProgress get_progress(int mv1, int mv2) {
    return (mv1 <= 1300 && mv2 <= 1300) ? ENDGAME : MIDGAME;
}

bool has_non_pawn_material(const chess::Board& board, chess::Color color) {
    for (chess::PieceType pt = chess::PieceType::KNIGHT; pt <= chess::PieceType::QUEEN; ++pt) {
        if (board.pieces(pt, color)) {
            return true;
        }
    }
    return false;
}

// From https://github.com/Disservin/Smallbrain/blob/main/src/see.h#L7-L25
chess::Bitboard attackers_for_side(const chess::Board& board, chess::Square sq, chess::Color attacker_color, chess::Bitboard occ) {
    chess::Bitboard attacking_bishops = board.pieces(chess::PieceType::BISHOP, attacker_color);
    chess::Bitboard attacking_rooks = board.pieces(chess::PieceType::ROOK, attacker_color);
    chess::Bitboard attacking_queens = board.pieces(chess::PieceType::QUEEN, attacker_color);
    chess::Bitboard attacking_knights = board.pieces(chess::PieceType::KNIGHT, attacker_color);
    chess::Bitboard attacking_king = board.pieces(chess::PieceType::KING, attacker_color);
    chess::Bitboard attacking_pawns = board.pieces(chess::PieceType::PAWN, attacker_color);

    chess::Bitboard inter_cardinal_rays = chess::movegen::attacks::bishop(sq, occ);
    chess::Bitboard cardinal_rays_rays = chess::movegen::attacks::rook(sq, occ);

    chess::Bitboard attackers = inter_cardinal_rays & (attacking_bishops | attacking_queens);
    attackers |= cardinal_rays_rays & (attacking_rooks | attacking_queens);
    attackers |= chess::movegen::attacks::knight(sq) & attacking_knights;
    attackers |= chess::movegen::attacks::king(sq) & attacking_king;
    attackers |= chess::movegen::attacks::pawn(~attacker_color, sq) & attacking_pawns;
    return attackers;
}
