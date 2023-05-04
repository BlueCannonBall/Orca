#include "evaluation.hpp"
#include "util.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

template <Color Us>
int evaluate_basic(const Position& pos) {
    int ret = 0;
    for (PieceType i = PAWN; i < NPIECE_TYPES - 1; ++i) {
        ret += pop_count(pos.bitboard_of(Us, i)) * piece_values[i];
        ret -= pop_count(pos.bitboard_of(~Us, i)) * piece_values[i];
    }
    return ret;
}

int evaluate_nn(const Position& pos) {
    ProphetBoard prophet_board = generate_prophet_board(pos);
    return prophet_sing_evaluation((Prophet*) pos.data, &prophet_board);
}

template <Color Us>
int evaluate_nnue(const Position& pos) {
    return prophet_utter_evaluation((Prophet*) pos.data, Us);
}

template <Color Us>
int evaluate(const Position& pos, bool debug) {
    // Material value
    int mv = 0;
    int our_mv = 0;
    int their_mv = 0;
    for (PieceType i = PAWN; i < NPIECE_TYPES - 1; ++i) {
        our_mv += pop_count(pos.bitboard_of(Us, i)) * piece_values[i];
        their_mv += pop_count(pos.bitboard_of(~Us, i)) * piece_values[i];
    }
    mv += our_mv;
    mv -= their_mv;
    GameProgress progress = get_progress(our_mv, their_mv);

    // Color advantage
    int ca = 0;
    if (progress == MIDGAME) {
        ca = (Us == WHITE) ? 15 : -15;
    }

    // Center control
    int cc = 0;
    if (pos.at(d5) != NO_PIECE) cc += (color_of(pos.at(d5)) == Us) ? 25 : -25;
    if (pos.at(e5) != NO_PIECE) cc += (color_of(pos.at(e5)) == Us) ? 25 : -25;
    if (pos.at(d4) != NO_PIECE) cc += (color_of(pos.at(d4)) == Us) ? 25 : -25;
    if (pos.at(e4) != NO_PIECE) cc += (color_of(pos.at(e4)) == Us) ? 25 : -25;

    // Knight placement
    const static Bitboard edges_mask = MASK_FILE[AFILE] | MASK_RANK[RANK1] | MASK_FILE[HFILE] | MASK_RANK[RANK8];
    int np = 0;
    np -= pop_count(pos.bitboard_of(Us, KNIGHT) & edges_mask) * 50;
    np += pop_count(pos.bitboard_of(~Us, KNIGHT) & edges_mask) * 50;

    // Bishop placement
    static constexpr Bitboard black_squares = 0xAA55AA55AA55AA55;
    int bp = 0;
    for (Color color = WHITE; color < NCOLORS; ++color) {
        unsigned short white_square_count = 0;
        unsigned short black_square_count = 0;

        Bitboard bishops = pos.bitboard_of(color, BISHOP);
        while (bishops) {
            Square bishop = pop_lsb(&bishops);

            if ((black_squares >> bishop) & 1) {
                black_square_count++;
            } else {
                white_square_count++;
            }

            if (white_square_count && black_square_count) {
                bp += color == Us ? 50 : -50;
                break;
            }
        }
    }

    // Rook placement
    int rp = 0;
    rp += pop_count(pos.bitboard_of(Us, ROOK) & MASK_RANK[Us == WHITE ? RANK7 : RANK2]) * 30;
    rp -= pop_count(pos.bitboard_of(~Us, ROOK) & MASK_RANK[~Us == WHITE ? RANK7 : RANK2]) * 30;

    // King placement
    /*
    function distance(x1, y1, x2, y2) {
        return Math.hypot(x2 - x1, y2 - y1);
    }

    let table = [];
    for (let y = 7; y > -1; y--) {
        for (let x = 0; x < 8; x++) {
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
        kp += king_pcsq_table[bsf(pos.bitboard_of(Us, KING))];
        kp -= king_pcsq_table[bsf(pos.bitboard_of(~Us, KING))];
    }

    // Doubled pawns
    int dp = 0;
    for (File file = AFILE; file < NFILES; ++file) {
        dp -= std::max(pop_count(pos.bitboard_of(Us, PAWN) & MASK_FILE[file]) - 1, 0) * 75;
        dp += std::max(pop_count(pos.bitboard_of(~Us, PAWN) & MASK_FILE[file]) - 1, 0) * 75;
    }

    // Passed pawns
    int pp = 0;
    for (Color color = WHITE; color < NCOLORS; ++color) {
        Bitboard pawns = pos.bitboard_of(color, PAWN);
        while (pawns) {
            Square sq = pop_lsb(&pawns);

            Bitboard pawns_ahead_mask = MASK_FILE[file_of(sq)];
            if (file_of(sq) > AFILE) {
                pawns_ahead_mask |= MASK_FILE[file_of(sq) - 1];
            }
            if (file_of(sq) < HFILE) {
                pawns_ahead_mask |= MASK_FILE[file_of(sq) + 1];
            }

            if (color == WHITE) {
                for (Rank rank = RANK1; rank <= rank_of(sq); ++rank) {
                    pawns_ahead_mask &= ~MASK_RANK[rank];
                }
            } else if (color == BLACK) {
                for (Rank rank = RANK8; rank >= rank_of(sq); --rank) {
                    pawns_ahead_mask &= ~MASK_RANK[rank];
                }
            } else {
                throw std::logic_error("Invalid color");
            }

            if (!(pos.bitboard_of(~color, PAWN) & pawns_ahead_mask)) {
                if (progress == MIDGAME) {
                    pp += color == Us ? 30 : -30;
                } else if (progress == ENDGAME) {
                    int score = 0;
                    if (color == WHITE) {
                        score = (rank_of(sq) - RANK1) * 50;
                    } else if (color == BLACK) {
                        score = (RANK8 - rank_of(sq)) * 50;
                    } else {
                        throw std::logic_error("Invalid color");
                    }
                    pp += color == Us ? score : -score;
                } else {
                    throw std::logic_error("Invalid game progress");
                }
            }
        }
    }

    // Isolated pawns
    int ip = 0;
    if (progress == MIDGAME) {
        for (Color color = WHITE; color < NCOLORS; ++color) {
            Bitboard pawns = pos.bitboard_of(color, PAWN);
            while (pawns) {
                Square sq = pop_lsb(&pawns);

                Bitboard buddies_mask = 0;
                if (file_of(sq) > AFILE) {
                    buddies_mask |= MASK_FILE[file_of(sq) - 1];
                }
                if (file_of(sq) < HFILE) {
                    buddies_mask |= MASK_FILE[file_of(sq) + 1];
                }

                if (!(pos.bitboard_of(color, PAWN) & buddies_mask)) {
                    ip += color == Us ? -15 : 15;
                }
            }
        }
    }

    // Open files
    int of = 0;
    for (Color color = WHITE; color < NCOLORS; ++color) {
        Bitboard rooks = pos.bitboard_of(color, ROOK);
        while (rooks) {
            Square sq = pop_lsb(&rooks);
            File file = file_of(sq);

            Bitboard pawns[NCOLORS] = {
                pos.bitboard_of(WHITE_PAWN) & MASK_FILE[file],
                pos.bitboard_of(BLACK_PAWN) & MASK_FILE[file],
            };

            if (pawns[~color]) {
                of += color == Us ? -5 : 5; // File is half-open
                if (pawns[color]) {
                    of += color == Us ? -5 : 5; // File is closed
                }
            }
        }
    }

    // Check status
    int cs = 0;
    if (pos.in_check<Us>()) {
        cs = -20;
    } else if (pos.in_check<~Us>()) {
        cs = 20;
    }

    // Sum up various scores
    if (debug) {
        std::cerr << "info string material value " << mv << std::endl;
        std::cerr << "info string color advantage " << ca << std::endl;
        std::cerr << "info string center control " << cc << std::endl;
        std::cerr << "info string knight placement " << np << std::endl;
        std::cerr << "info string bishop placement " << bp << std::endl;
        std::cerr << "info string rook placement " << rp << std::endl;
        std::cerr << "info string king placement " << kp << std::endl;
        std::cerr << "info string doubled pawns " << dp << std::endl;
        std::cerr << "info string passed pawns " << pp << std::endl;
        std::cerr << "info string isolated pawns " << ip << std::endl;
        std::cerr << "info string open files " << of << std::endl;
        std::cerr << "info string check status " << cs << std::endl;
    }
    return mv + ca + cc + np + bp + rp + kp + dp + pp + ip + of + cs;
}

template <Color Us>
int see(const Position& pos, Move move) {
    const Square attacked_sq = move.to();
    Bitboard occ = BOTH_COLOR_CALL(pos.all_pieces);
    Bitboard attackers = BOTH_COLOR_CALL(pos.attackers_from, attacked_sq, occ);
    Bitboard diagonal_sliders = BOTH_COLOR_CALL(pos.diagonal_sliders);
    Bitboard orthogonal_sliders = BOTH_COLOR_CALL(pos.orthogonal_sliders);

    int ret = 0;
    PieceType attacked_pc;

    {
        const Square attacker_sq = move.from();
        const PieceType attacker_pc = type_of(pos.at(attacker_sq));

        occ &= ~SQUARE_BB[attacker_sq];
        attackers &= ~SQUARE_BB[attacker_sq];
        attacked_pc = attacker_pc;

        if (attacker_pc == PAWN || attacker_pc == BISHOP || attacker_pc == QUEEN) {
            diagonal_sliders &= ~SQUARE_BB[attacker_sq];
            attackers |= attacks<BISHOP>(attacked_sq, occ) & diagonal_sliders;
        }
        if (attacker_pc == ROOK || attacker_pc == QUEEN) {
            orthogonal_sliders &= ~SQUARE_BB[attacker_sq];
            attackers |= attacks<ROOK>(attacked_sq, occ) & orthogonal_sliders;
        }
    }

    if (move.is_capture()) {
        ret += piece_values[pos.at(attacked_sq)];
    }

    for (Color side_to_play = ~Us;; side_to_play = ~side_to_play) {
        bool attacked = false;
        for (PieceType attacker_pc = PAWN; attacker_pc < NPIECE_TYPES; ++attacker_pc) {
            Bitboard attacker = attackers & pos.bitboard_of(side_to_play, attacker_pc);
            if (attacker) {
                if (attacker_pc == KING && (attackers & DYN_COLOR_CALL(pos.all_pieces, ~side_to_play))) {
                    break;
                }

                ret += side_to_play == Us ? piece_values[attacked_pc] : -piece_values[attacked_pc];

                const Square attacker_sq = bsf(attacker);
                occ &= ~SQUARE_BB[attacker_sq];
                attackers &= ~SQUARE_BB[attacker_sq];
                attacked_pc = attacker_pc;

                if (attacker_pc == PAWN || attacker_pc == BISHOP || attacker_pc == QUEEN) {
                    diagonal_sliders &= ~SQUARE_BB[attacker_sq];
                    attackers |= attacks<BISHOP>(attacked_sq, occ) & diagonal_sliders;
                }
                if (attacker_pc == ROOK || attacker_pc == QUEEN) {
                    orthogonal_sliders &= ~SQUARE_BB[attacker_sq];
                    attackers |= attacks<ROOK>(attacked_sq, occ) & orthogonal_sliders;
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

int mvv_lva(const Position& pos, Move move) {
    assert(move.is_capture());

    static constexpr int scores[NPIECE_TYPES * NPIECE_TYPES] = {
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

    if (move.flags() == EN_PASSANT) {
        return scores[PAWN * NPIECE_TYPES + PAWN];
    } else {
        return scores[type_of(pos.at(move.to())) * NPIECE_TYPES + type_of(pos.at(move.from()))];
    }
}

template int evaluate_basic<WHITE>(const Position& pos);
template int evaluate_basic<BLACK>(const Position& pos);

template int evaluate_nnue<WHITE>(const Position& pos);
template int evaluate_nnue<BLACK>(const Position& pos);

template int evaluate<WHITE>(const Position& pos, bool debug);
template int evaluate<BLACK>(const Position& pos, bool debug);

template int see<WHITE>(const Position& pos, Move move);
template int see<BLACK>(const Position& pos, Move move);
