#include "evaluation.hpp"
#include "util.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

ADD_BASE_OPERATORS_FOR(chess::Rank);
ADD_BASE_OPERATORS_FOR(chess::File);

ADD_INCR_OPERATORS_FOR(chess::Rank);
ADD_INCR_OPERATORS_FOR(chess::File);
ADD_INCR_OPERATORS_FOR(chess::PieceType);

int evaluate_basic(const chess::Board& board) {
    int ret = 0;
    for (chess::PieceType pt = chess::PieceType::PAWN; pt <= chess::PieceType::QUEEN; ++pt) {
        ret += chess::builtin::popcount(board.pieces(pt, board.sideToMove())) * get_value(pt);
        ret -= chess::builtin::popcount(board.pieces(pt, ~board.sideToMove())) * get_value(pt);
    }
    return ret;
}

int evaluate_nnue(const nnue::Board& board) {
    return prophet_utter_evaluation(board.get_prophet(), (uint8_t) board.sideToMove());
}

int evaluate(const chess::Board& board, bool debug) {
    // Material value
    int mv = 0;
    int our_mv = 0;
    int their_mv = 0;
    for (chess::PieceType pt = chess::PieceType::PAWN; pt <= chess::PieceType::QUEEN; ++pt) {
        our_mv += chess::builtin::popcount(board.pieces(pt, board.sideToMove())) * get_value(pt);
        their_mv += chess::builtin::popcount(board.pieces(pt, ~board.sideToMove())) * get_value(pt);
    }
    mv += our_mv;
    mv -= their_mv;
    GameProgress progress = get_progress(our_mv, their_mv);

    // Color advantage
    int ca = 0;
    if (progress == MIDGAME) {
        ca = (board.sideToMove() == chess::Color::WHITE) ? 15 : -15;
    }

    // Center control
    int cc = 0;
    if (board.at(chess::SQ_D5) != chess::Piece::NONE) cc += (board.color(board.at(chess::SQ_D5)) == board.sideToMove()) ? 25 : -25;
    if (board.at(chess::SQ_E5) != chess::Piece::NONE) cc += (board.color(board.at(chess::SQ_E5)) == board.sideToMove()) ? 25 : -25;
    if (board.at(chess::SQ_D4) != chess::Piece::NONE) cc += (board.color(board.at(chess::SQ_D4)) == board.sideToMove()) ? 25 : -25;
    if (board.at(chess::SQ_E4) != chess::Piece::NONE) cc += (board.color(board.at(chess::SQ_E4)) == board.sideToMove()) ? 25 : -25;

    // Knight placement
    static constexpr chess::Bitboard edges_mask =
        chess::movegen::MASK_FILE[(int) chess::File::FILE_A] |
        chess::movegen::MASK_RANK[(int) chess::Rank::RANK_1] |
        chess::movegen::MASK_FILE[(int) chess::File::FILE_H] |
        chess::movegen::MASK_RANK[(int) chess::Rank::RANK_8];
    int np = 0;
    np -= chess::builtin::popcount(board.pieces(chess::PieceType::KNIGHT, board.sideToMove()) & edges_mask) * 50;
    np += chess::builtin::popcount(board.pieces(chess::PieceType::KNIGHT, ~board.sideToMove()) & edges_mask) * 50;

    // Bishop placement
    static constexpr chess::Bitboard black_squares = 0xAA55AA55AA55AA55;
    int bp = 0;
    BOTH_COLORS {
        unsigned short white_square_count = 0;
        unsigned short black_square_count = 0;

        chess::Bitboard bishops = board.pieces(chess::PieceType::BISHOP, color);
        while (bishops) {
            chess::Square bishop = chess::builtin::poplsb(bishops);

            if ((black_squares >> bishop) & 1) {
                black_square_count++;
            } else {
                white_square_count++;
            }

            if (white_square_count && black_square_count) {
                bp += color == board.sideToMove() ? 50 : -50;
                break;
            }
        }
    }

    // Rook placement
    int rp = 0;
    rp += chess::builtin::popcount(board.pieces(chess::PieceType::ROOK, board.sideToMove()) & chess::movegen::MASK_RANK[(int) (board.sideToMove() == chess::Color::WHITE ? chess::Rank::RANK_7 : chess::Rank::RANK_2)]) * 30;
    rp -= chess::builtin::popcount(board.pieces(chess::PieceType::ROOK, ~board.sideToMove()) & chess::movegen::MASK_RANK[(int) (~board.sideToMove() == chess::Color::WHITE ? chess::Rank::RANK_7 : chess::Rank::RANK_2)]) * 30;

    // King placement
    /*
    function distance(x1, y1, x2, y2) {
        return Math.hypot(x2 - x1, y2 - y1);
    }

    let table = [];
    for (let y = 7; y > -1; --y) {
        for (let x = 0; x < 8; ++x) {
            table.push(Math.round(distance(x, y, 3.5, 3.5) * 20));
        }
    }

    table[0] = Math.round(distance(1, 6, 3.5, 3.5) * 20);
    table[7] = Math.round(distance(6, 6, 3.5, 3.5) * 20);
    table[56] = Math.round(distance(1, 1, 3.5, 3.5) * 20);
    table[63] = Math.round(distance(6, 1, 3.5, 3.5) * 20);
    */
    static constexpr int king_pcsq_table[64] = {71, 86, 76, 71, 71, 76, 86, 71, 86, 71, 58, 51, 51, 58, 71, 86, 76, 58, 42, 32, 32, 42, 58, 76, 71, 51, 32, 14, 14, 32, 51, 71, 71, 51, 32, 14, 14, 32, 51, 71, 76, 58, 42, 32, 32, 42, 58, 76, 86, 71, 58, 51, 51, 58, 71, 86, 71, 86, 76, 71, 71, 76, 86, 71};
    int kp = 0;
    if (progress == MIDGAME) {
        kp += king_pcsq_table[board.kingSq(board.sideToMove())];
        kp -= king_pcsq_table[board.kingSq(~board.sideToMove())];
    }

    // Doubled pawns
    int dp = 0;
    for (chess::File file = chess::File::FILE_A; file <= chess::File::FILE_H; ++file) {
        dp -= std::max(chess::builtin::popcount(board.pieces(chess::PieceType::PAWN, board.sideToMove()) & chess::movegen::MASK_FILE[(int) file]) - 1, 0) * 75;
        dp += std::max(chess::builtin::popcount(board.pieces(chess::PieceType::PAWN, ~board.sideToMove()) & chess::movegen::MASK_FILE[(int) file]) - 1, 0) * 75;
    }

    // Passed pawns
    int pp = 0;
    BOTH_COLORS {
        chess::Bitboard pawns = board.pieces(chess::PieceType::PAWN, color);
        while (pawns) {
            chess::Square sq = chess::builtin::poplsb(pawns);

            chess::Bitboard pawns_ahead_mask = chess::movegen::MASK_FILE[(int) chess::utils::squareFile(sq)];
            if (chess::utils::squareFile(sq) > chess::File::FILE_A) {
                pawns_ahead_mask |= chess::movegen::MASK_FILE[(int) chess::utils::squareFile(sq) - 1];
            }
            if (chess::utils::squareFile(sq) < chess::File::FILE_H) {
                pawns_ahead_mask |= chess::movegen::MASK_FILE[(int) chess::utils::squareFile(sq) + 1];
            }

            if (color == chess::Color::WHITE) {
                for (chess::Rank rank = chess::Rank::RANK_1; rank <= chess::utils::squareRank(sq); ++rank) {
                    pawns_ahead_mask &= ~chess::movegen::MASK_RANK[(int) rank];
                }
            } else if (color == chess::Color::BLACK) {
                for (chess::Rank rank = chess::Rank::RANK_8; rank >= chess::utils::squareRank(sq); --rank) {
                    pawns_ahead_mask &= ~chess::movegen::MASK_RANK[(int) rank];
                }
            } else {
                throw std::logic_error("Invalid color");
            }

            if (!(board.pieces(chess::PieceType::PAWN, ~color) & pawns_ahead_mask)) {
                if (progress == MIDGAME) {
                    pp += color == board.sideToMove() ? 30 : -30;
                } else if (progress == ENDGAME) {
                    int score = 0;
                    if (color == chess::Color::WHITE) {
                        score = (int) (chess::utils::squareRank(sq) - (int) chess::Rank::RANK_1) * 50;
                    } else if (color == chess::Color::BLACK) {
                        score = (int) (chess::Rank::RANK_8 - (int) chess::utils::squareRank(sq)) * 50;
                    } else {
                        throw std::logic_error("Invalid color");
                    }
                    pp += color == board.sideToMove() ? score : -score;
                } else {
                    throw std::logic_error("Invalid game progress");
                }
            }
        }
    }

    // Isolated pawns
    int ip = 0;
    if (progress == MIDGAME) {
        BOTH_COLORS {
            chess::Bitboard pawns = board.pieces(chess::PieceType::PAWN, color);
            while (pawns) {
                chess::Square sq = chess::builtin::poplsb(pawns);

                chess::Bitboard buddies_mask = 0;
                if (chess::utils::squareFile(sq) > chess::File::FILE_A) {
                    buddies_mask |= chess::movegen::MASK_FILE[(int) chess::utils::squareFile(sq) - 1];
                }
                if (chess::utils::squareFile(sq) < chess::File::FILE_H) {
                    buddies_mask |= chess::movegen::MASK_FILE[(int) chess::utils::squareFile(sq) + 1];
                }

                if (!(board.pieces(chess::PieceType::PAWN, color) & buddies_mask)) {
                    ip += color == board.sideToMove() ? -15 : 15;
                }
            }
        }
    }

    // Open files
    int of = 0;
    BOTH_COLORS {
        chess::Bitboard rooks = board.pieces(chess::PieceType::ROOK, color);
        while (rooks) {
            chess::Square sq = chess::builtin::poplsb(rooks);
            chess::File file = chess::utils::squareFile(sq);

            chess::Bitboard pawns[2] = {
                board.pieces(chess::PieceType::PAWN, chess::Color::WHITE) & chess::movegen::MASK_FILE[(int) file],
                board.pieces(chess::PieceType::PAWN, chess::Color::BLACK) & chess::movegen::MASK_FILE[(int) file],
            };

            if (pawns[(uint8_t) ~color]) {
                of += color == board.sideToMove() ? -5 : 5; // File is half-open
                if (pawns[(uint8_t) color]) {
                    of += color == board.sideToMove() ? -5 : 5; // File is closed
                }
            }
        }
    }

    // Sum up various scores
    if (debug) {
        std::cerr << "Material value " << mv << std::endl;
        std::cerr << "Color advantage " << ca << std::endl;
        std::cerr << "Center control " << cc << std::endl;
        std::cerr << "Knight placement " << np << std::endl;
        std::cerr << "Bishop placement " << bp << std::endl;
        std::cerr << "Rook placement " << rp << std::endl;
        std::cerr << "King placement " << kp << std::endl;
        std::cerr << "Doubled pawns " << dp << std::endl;
        std::cerr << "Passed pawns " << pp << std::endl;
        std::cerr << "Isolated pawns " << ip << std::endl;
        std::cerr << "Open files " << of << std::endl;
    }
    return mv + ca + cc + np + bp + rp + kp + dp + pp + ip + of;
}

int see(const chess::Board& board, const chess::Move& move, bool debug) {
    assert(move.typeOf() != chess::Move::PROMOTION && move.typeOf() != chess::Move::ENPASSANT);

    const chess::Square attacked_sq = move.to();
    chess::Bitboard occ = board.occ();
    chess::Bitboard attackers = attackers_for_side(board, attacked_sq, chess::Color::WHITE, occ) |
                                attackers_for_side(board, attacked_sq, chess::Color::BLACK, occ);
    chess::Bitboard diagonal_sliders = board.pieces(chess::PieceType::BISHOP) | board.pieces(chess::PieceType::QUEEN);
    chess::Bitboard orthogonal_sliders = board.pieces(chess::PieceType::ROOK) | board.pieces(chess::PieceType::QUEEN);

    int ret = 0;
    chess::PieceType attacked_pt;

    {
        const chess::Square attacker_sq = move.from();
        const chess::PieceType attacker_pt = chess::utils::typeOfPiece(board.at(attacker_sq));

        occ &= ~(1ULL << attacker_sq);
        attackers &= ~(1ULL << attacker_sq);
        attacked_pt = attacker_pt;

        if (attacker_pt == chess::PieceType::PAWN ||
            attacker_pt == chess::PieceType::BISHOP ||
            attacker_pt == chess::PieceType::QUEEN) {
            diagonal_sliders &= ~(1ULL << attacker_sq);
            attackers |= chess::movegen::attacks::bishop(attacked_sq, occ) & diagonal_sliders;
        }
        if (attacker_pt == chess::PieceType::PAWN ||
            attacker_pt == chess::PieceType::ROOK ||
            attacker_pt == chess::PieceType::QUEEN) {
            orthogonal_sliders &= ~(1ULL << attacker_sq);
            attackers |= chess::movegen::attacks::rook(attacked_sq, occ) & orthogonal_sliders;
        }
    }

    if (board.at(move.to()) != chess::Piece::NONE) {
        ret += get_value(chess::utils::typeOfPiece(board.at(attacked_sq)));
        if (debug) std::cout << "S1 starts by gaining " << get_value(chess::utils::typeOfPiece(board.at(attacked_sq))) << std::endl;
    }

    for (chess::Color side_to_move = ~board.sideToMove();; side_to_move = ~side_to_move) {
        bool attacked = false;
        for (chess::PieceType attacker_pt = chess::PieceType::PAWN; attacker_pt <= chess::PieceType::KING; ++attacker_pt) {
            chess::Bitboard attacker = attackers & board.pieces(attacker_pt, side_to_move);
            if (attacker) {
                if (attacker_pt == chess::PieceType::KING && (attackers & board.them(~side_to_move))) {
                    break;
                }

                ret += side_to_move == board.sideToMove() ? get_value(attacked_pt) : -get_value(attacked_pt);
                if (debug) {
                    std::cout << (side_to_move == board.sideToMove() ? "S1" : "S2") << " gains " << get_value(attacked_pt) << std::endl;
                    std::cout << "Net gains for S1: " << ret << std::endl;
                }

                const chess::Square attacker_sq = chess::builtin::lsb(attacker);
                occ &= ~(1ULL << attacker_sq);
                attackers &= ~(1ULL << attacker_sq);
                attacked_pt = attacker_pt;

                if (attacker_pt == chess::PieceType::PAWN ||
                    attacker_pt == chess::PieceType::BISHOP ||
                    attacker_pt == chess::PieceType::QUEEN) {
                    diagonal_sliders &= ~(1ULL << attacker_sq);
                    attackers |= chess::movegen::attacks::bishop(attacked_sq, occ) & diagonal_sliders;
                }
                if (attacker_pt == chess::PieceType::PAWN ||
                    attacker_pt == chess::PieceType::ROOK ||
                    attacker_pt == chess::PieceType::QUEEN) {
                    orthogonal_sliders &= ~(1ULL << attacker_sq);
                    attackers |= chess::movegen::attacks::rook(attacked_sq, occ) & orthogonal_sliders;
                }

                attacked = true;
                break;
            }
        }
        if (!attacked) {
            break;
        }
    }

    return ret;
}

int mvv_lva(const chess::Board& board, const chess::Move& move) {
    static constexpr int scores[] = {
        105,
        104,
        103,
        102,
        101,
        100,
        205,
        204,
        203,
        202,
        201,
        200,
        305,
        304,
        303,
        302,
        301,
        300,
        405,
        404,
        403,
        402,
        401,
        400,
        505,
        504,
        503,
        502,
        501,
        500,
        605,
        604,
        603,
        602,
        601,
        600,
    };

    if (move.typeOf() == chess::Move::ENPASSANT) {
        return scores[(uint8_t) chess::PieceType::PAWN * 6 + (uint8_t) chess::PieceType::PAWN];
    } else if (board.at(move.to()) != chess::Piece::NONE) {
        return scores[(uint8_t) chess::utils::typeOfPiece(board.at(move.to())) * 6 + (uint8_t) chess::utils::typeOfPiece(board.at(move.from()))];
    } else {
        throw std::invalid_argument("Move must be a capture");
    }
}
