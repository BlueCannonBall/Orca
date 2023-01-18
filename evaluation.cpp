#include "evaluation.hpp"
#include "util.hpp"

template <Color Us>
int evaluate(const Position& pos) {
    // Material value
    int mv = 0;
    int our_mv = 0;
    int their_mv = 0;
    for (size_t i = 0; i < NPIECE_TYPES - 1; i++) {
        our_mv += pop_count(pos.bitboard_of(Us, (PieceType) i)) * piece_values[i];
        their_mv += pop_count(pos.bitboard_of(~Us, (PieceType) i)) * piece_values[i];
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
        throw std::logic_error("Invalid progress value");
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

    let table = [[], []];
    for (let y = 7; y > -1; y--) {
        for (let x = 0; x < 8; x++) {
            table[0].push(Math.round(-Math.min(distance(x, y, 0, 7), distance(x, y, 7, 7)) * 10));
            table[1].push(Math.round(-Math.min(distance(x, y, 0, 0), distance(x, y, 7, 0)) * 10));
        }
    }
    */
    static constexpr int king_pcsq_table[2][64] = {{0, -15, -30, -45, -45, -30, -15, 0, -15, -21, -34, -47, -47, -34, -21, -15, -30, -34, -42, -54, -54, -42, -34, -30, -45, -47, -54, -64, -64, -54, -47, -45, -60, -62, -67, -75, -75, -67, -62, -60, -75, -76, -81, -87, -87, -81, -76, -75, -90, -91, -95, -101, -101, -95, -91, -90, -105, -106, -109, -114, -114, -109, -106, -105}, {-105, -106, -109, -114, -114, -109, -106, -105, -90, -91, -95, -101, -101, -95, -91, -90, -75, -76, -81, -87, -87, -81, -76, -75, -60, -62, -67, -75, -75, -67, -62, -60, -45, -47, -54, -64, -64, -54, -47, -45, -30, -34, -42, -54, -54, -42, -34, -30, -15, -21, -34, -47, -47, -34, -21, -15, 0, -15, -30, -45, -45, -30, -15, 0}};
    int kp = 0;
    kp += king_pcsq_table[Us][bsf(pos.bitboard_of(Us, KING))];
    kp -= king_pcsq_table[~Us][bsf(pos.bitboard_of(~Us, KING))];

    // Pawn placement
    int pp = 0;
    for (int file = AFILE; file < HFILE; file++) {
        int our_pawn_count = sparse_pop_count(pos.bitboard_of(Us, PAWN) & MASK_FILE[file]);
        int their_pawn_count = sparse_pop_count(pos.bitboard_of(~Us, PAWN) & MASK_FILE[file]);
        if (our_pawn_count > 1) {
            pp -= (our_pawn_count - 1) * 75;
        }
        if (their_pawn_count > 1) {
            pp += (their_pawn_count - 1) * 75;
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
    // std::cout << "Material value: " << mv << std::endl;
    // std::cout << "Color advantage: " << ca << std::endl;
    // std::cout << "Center control: " << cc << std::endl;
    // std::cout << "Knight placement: " << np << std::endl;
    // std::cout << "King placement: " << kp << std::endl;
    // std::cout << "Pawn placement: " << pp << std::endl;
    // std::cout << "Check status: " << cs << std::endl;
    return mv + ca + cc + np + kp + pp + cs;
}

template <Color Us>
int see(const Position& pos, Square sq) {
    Bitboard occ = BOTH_COLOR_CALL(pos.all_pieces);
    Bitboard attackers = BOTH_COLOR_CALL(pos.attackers_from, sq, occ);
    Bitboard diagonal_sliders = BOTH_COLOR_CALL(pos.diagonal_sliders);
    Bitboard orthogonal_sliders = BOTH_COLOR_CALL(pos.orthogonal_sliders);

    int ret = 0;
    int sq_occ = (pos.at(sq) == NO_PIECE) ? -1 : type_of(pos.at(sq));
    for (;;) {
        {
            bool attacked = false;
            for (size_t attacker_pc = PAWN; attacker_pc < NPIECE_TYPES; attacker_pc++) {
                Bitboard attacker = attackers & pos.bitboard_of(Us, (PieceType) attacker_pc);
                if (attacker) {
                    if (sq_occ != -1) {
                        ret += piece_values[sq_occ];
                    }

                    Square attacker_sq = bsf(attacker);
                    occ ^= SQUARE_BB[attacker_sq];
                    attackers ^= SQUARE_BB[attacker_sq];
                    sq_occ = attacker_pc;

                    if (attacker_pc == PAWN || attacker_pc == BISHOP || attacker_pc == QUEEN) {
                        diagonal_sliders ^= SQUARE_BB[attacker_sq];
                        attackers |= attacks<BISHOP>(sq, occ) & diagonal_sliders;
                    }
                    if (attacker_pc == ROOK || attacker_pc == QUEEN) {
                        orthogonal_sliders ^= SQUARE_BB[attacker_sq];
                        attackers |= attacks<ROOK>(sq, occ) & orthogonal_sliders;
                    }

                    attacked = true;
                    break;
                }
            }
            if (!attacked) {
                break;
            }
        }

        {
            bool attacked = false;
            for (size_t attacker_pc = PAWN; attacker_pc < NPIECE_TYPES; attacker_pc++) {
                Bitboard attacker = attackers & pos.bitboard_of(~Us, (PieceType) attacker_pc);
                if (attacker) {
                    if (sq_occ != -1) {
                        ret -= piece_values[sq_occ];
                    }

                    Square attacker_sq = bsf(attacker);
                    occ ^= SQUARE_BB[attacker_sq];
                    attackers ^= SQUARE_BB[attacker_sq];
                    sq_occ = attacker_pc;

                    if (attacker_pc == PAWN || attacker_pc == BISHOP || attacker_pc == QUEEN) {
                        diagonal_sliders ^= SQUARE_BB[attacker_sq];
                        attackers |= attacks<BISHOP>(sq, occ) & diagonal_sliders;
                    }
                    if (attacker_pc == ROOK || attacker_pc == QUEEN) {
                        orthogonal_sliders ^= SQUARE_BB[attacker_sq];
                        attackers |= attacks<ROOK>(sq, occ) & orthogonal_sliders;
                    }

                    attacked = true;
                    break;
                }
            }
            if (!attacked) {
                break;
            }
        }
    }

    return ret;
}

template int evaluate<WHITE>(const Position& pos);
template int evaluate<BLACK>(const Position& pos);

template int see<WHITE>(const Position& pos, Square sq);
template int see<BLACK>(const Position& pos, Square sq);
