#pragma once

#include "chess.hpp"
#include <prophet.h>
#include <string>

namespace nnue {
    class Board : public chess::Board {
    public:
        Board():
            chess::Board() {}
        Board(const std::string& fen = chess::STARTPOS):
            chess::Board(fen) {}

        void setFen(const std::string& fen) override;

        void accept_prophet(Prophet* new_prophet);
        Prophet* release_prophet();

        inline Prophet* get_prophet() const {
            return prophet;
        }

        ~Board();

    protected:
        Prophet* prophet = nullptr;

        void placePiece(chess::Piece piece, chess::Square sq) override;
        void removePiece(chess::Piece piece, chess::Square sq) override;
    };
} // namespace nnue
