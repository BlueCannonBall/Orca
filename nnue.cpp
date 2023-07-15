#include "nnue.hpp"

namespace nnue {
    void Board::accept_prophet(Prophet* new_prophet) {
        if (prophet) {
            prophet_die_for_sins(prophet);
        }

        prophet = new_prophet;
        prophet_reset(prophet);
        prophet_activate_all(prophet,
            {
                .white = us(chess::Color::WHITE),
                .black = us(chess::Color::BLACK),
                .pawns = pieces(chess::PieceType::PAWN),
                .knights = pieces(chess::PieceType::KNIGHT),
                .bishops = pieces(chess::PieceType::BISHOP),
                .rooks = pieces(chess::PieceType::ROOK),
                .queens = pieces(chess::PieceType::QUEEN),
                .kings = pieces(chess::PieceType::KING),
                .side_to_move = (uint8_t) side_to_move_,
            });
    }

    void Board::placePiece(chess::Piece piece, chess::Square sq) {
        if (prophet) prophet_activate(prophet, (int32_t) chess::utils::typeOfPiece(piece), (int32_t) color(piece), sq);
        chess::Board::placePiece(piece, sq);
    }

    void Board::removePiece(chess::Piece piece, chess::Square sq) {
        if (prophet) prophet_deactivate(prophet, (int32_t) chess::utils::typeOfPiece(piece), (int32_t) color(piece), sq);
        chess::Board::removePiece(piece, sq);
    }
} // namespace nnue
