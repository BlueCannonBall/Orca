#include "evaluation.hpp"
#include "util.hpp"
#include <cassert>
#include <iostream>

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
        ca = (Us == WHITE) ? 20 : -20;
    }

    // Center control
    int cc = 0;
    if (progress == MIDGAME) {
        if (pos.at(d5) != NO_PIECE && type_of(pos.at(d5)) != KING) cc += (color_of(pos.at(d5)) == Us) ? 25 : -25;
        if (pos.at(e5) != NO_PIECE && type_of(pos.at(e5)) != KING) cc += (color_of(pos.at(e5)) == Us) ? 25 : -25;
        if (pos.at(d4) != NO_PIECE && type_of(pos.at(d4)) != KING) cc += (color_of(pos.at(d4)) == Us) ? 25 : -25;
        if (pos.at(e4) != NO_PIECE && type_of(pos.at(e4)) != KING) cc += (color_of(pos.at(e4)) == Us) ? 25 : -25;
    } else if (progress == ENDGAME) {
        if (pos.at(d5) != NO_PIECE) cc += (color_of(pos.at(d5)) == Us) ? 25 : -25;
        if (pos.at(e5) != NO_PIECE) cc += (color_of(pos.at(e5)) == Us) ? 25 : -25;
        if (pos.at(d4) != NO_PIECE) cc += (color_of(pos.at(d4)) == Us) ? 25 : -25;
        if (pos.at(e4) != NO_PIECE) cc += (color_of(pos.at(e4)) == Us) ? 25 : -25;
    } else {
        throw std::logic_error("Invalid game progress");
    }

    // Knight placement
    int np = 0;
    np -= pop_count(pos.bitboard_of(Us, KNIGHT) & MASK_FILE[AFILE] & MASK_RANK[RANK1] & MASK_FILE[HFILE] & MASK_RANK[RANK8]) * 50;
    np += pop_count(pos.bitboard_of(~Us, KNIGHT) & MASK_FILE[AFILE] & MASK_RANK[RANK1] & MASK_FILE[HFILE] & MASK_RANK[RANK8]) * 50;

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
    */
    static constexpr int king_pcsq_table[64] = {99, 86, 76, 71, 71, 76, 86, 99, 86, 71, 58, 51, 51, 58, 71, 86, 76, 58, 42, 32, 32, 42, 58, 76, 71, 51, 32, 14, 14, 32, 51, 71, 71, 51, 32, 14, 14, 32, 51, 71, 76, 58, 42, 32, 32, 42, 58, 76, 86, 71, 58, 51, 51, 58, 71, 86, 99, 86, 76, 71, 71, 76, 86, 99};
    int kp = 0;
    if (progress == MIDGAME) {
        kp += king_pcsq_table[bsf(pos.bitboard_of(Us, KING))];
        kp -= king_pcsq_table[bsf(pos.bitboard_of(~Us, KING))];
    }

    // Doubled pawns
    int dp = 0;
    for (File file = AFILE; file < HFILE; ++file) {
        int our_pawn_count = sparse_pop_count(pos.bitboard_of(Us, PAWN) & MASK_FILE[file]);
        int their_pawn_count = sparse_pop_count(pos.bitboard_of(~Us, PAWN) & MASK_FILE[file]);
        if (our_pawn_count > 1) {
            dp -= (our_pawn_count - 1) * 75;
        }
        if (their_pawn_count > 1) {
            dp += (their_pawn_count - 1) * 75;
        }
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

    // Check status
    int cs = 0;
    if (pos.in_check<Us>()) {
        cs = -20;
    } else if (pos.in_check<~Us>()) {
        cs = 20;
    }

    // Sum up various scores
    if (debug) {
        std::cerr << "Material value: " << mv << std::endl;
        std::cerr << "Color advantage: " << ca << std::endl;
        std::cerr << "Center control: " << cc << std::endl;
        std::cerr << "Knight placement: " << np << std::endl;
        std::cerr << "King placement: " << kp << std::endl;
        std::cerr << "Doubled pawns: " << dp << std::endl;
        std::cerr << "Passed pawns: " << pp << std::endl;
        std::cerr << "Isolated pawns: " << ip << std::endl;
        std::cerr << "Check status: " << cs << std::endl;
    }
    return mv + ca + cc + np + kp + dp + pp + ip + cs;
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

template int evaluate<WHITE>(const Position& pos, bool debug);
template int evaluate<BLACK>(const Position& pos, bool debug);

template int see<WHITE>(const Position& pos, Move move);
template int see<BLACK>(const Position& pos, Move move);
