#include "search.hpp"
#include "surge/src/types.h"
#include <atomic>
#include <climits>
#include <cassert>
#include <cmath>

GameProgress get_progress(int mv1, int mv2) {
    return (mv1 <= 1300 && mv2 <= 1300) ? ENDGAME : MIDGAME;
}

template <Color Us>
int evaluate(const Position& pos) {
    // Material value
    int mv = 0;
    int mv_us = 0;
    int mv_them = 0;
    for (size_t i = 0; i < NPIECE_TYPES - 1; i++) {
        mv_us += pop_count(pos.bitboard_of(Us, (PieceType) i)) * piece_values[i];
        mv_them += pop_count(pos.bitboard_of(~Us, (PieceType) i)) * piece_values[i];
    }
    mv += mv_us;
    mv -= mv_them;
    GameProgress progress = get_progress(mv_us, mv_them);

    // Color advantage
    int ca = 0;
    if (progress == MIDGAME) {
        ca = (Us == WHITE) ? 15 : -15;
    }

    // Center control
    int cc = 0;
    if (progress == MIDGAME) {
        cc += (color_of(pos.at(d5)) == Us && type_of(pos.at(d5)) != KING) ? 25 : -25;
        cc += (color_of(pos.at(e5)) == Us && type_of(pos.at(e5)) != KING) ? 25 : -25;
        cc += (color_of(pos.at(d4)) == Us && type_of(pos.at(d4)) != KING) ? 25 : -25;
        cc += (color_of(pos.at(e4)) == Us && type_of(pos.at(e4)) != KING) ? 25 : -25;
    } else if (progress == ENDGAME) {
        cc += (color_of(pos.at(d5)) == Us) ? 25 : -25;
        cc += (color_of(pos.at(e5)) == Us) ? 25 : -25;
        cc += (color_of(pos.at(d4)) == Us) ? 25 : -25;
        cc += (color_of(pos.at(e4)) == Us) ? 25 : -25;
    } else {
        throw std::logic_error("Invalid progress value");
    }

    // Knight placement
    int np = 0;
    np -= sparse_pop_count(pos.bitboard_of(Us, KNIGHT) & MASK_FILE[AFILE] & MASK_RANK[RANK1] & MASK_FILE[HFILE] & MASK_RANK[RANK8]) * 50;
    np += sparse_pop_count(pos.bitboard_of(~Us, KNIGHT) & MASK_FILE[AFILE] & MASK_RANK[RANK1] & MASK_FILE[HFILE] & MASK_RANK[RANK8]) * 50;

    // King placement
    /*
    function distance(x1, y1, x2, y2) {
        return Math.hypot(x2 - x1, y2 - y1);
    }

    let table = [[], []];
    for (let y = 8; y > -1; y--) {
        for (let x = 0; x < 8; x++) {
            table[0].push(Math.round(-Math.min(distance(x, y, 0, 7), distance(x, y, 7, 7)) * 4));
            table[1].push(Math.round(-Math.min(distance(x, y, 0, 0), distance(x, y, 7, 0)) * 4));
        }
    }
    */
    static constexpr int king_pcsq_table[2][64] = {{0, -4, -8, -12, -12, -8, -4, 0, -4, -6, -9, -13, -13, -9, -6, -4, -8, -9, -11, -14, -14, -11, -9, -8, -12, -13, -14, -17, -17, -14, -13, -12, -16, -16, -18, -20, -20, -18, -16, -16, -20, -20, -22, -23, -23, -22, -20, -20, -24, -24, -25, -27, -27, -25, -24, -24, -28, -28, -29, -30, -30, -29, -28, -28}, {-28, -28, -29, -30, -30, -29, -28, -28, -24, -24, -25, -27, -27, -25, -24, -24, -20, -20, -22, -23, -23, -22, -20, -20, -16, -16, -18, -20, -20, -18, -16, -16, -12, -13, -14, -17, -17, -14, -13, -12, -8, -9, -11, -14, -14, -11, -9, -8, -4, -6, -9, -13, -13, -9, -6, -4, 0, -4, -8, -12, -12, -8, -4, 0}};
    int kp = 0;
    kp += king_pcsq_table[Us][bsf(pos.bitboard_of(Us, KING))];
    kp -= king_pcsq_table[~Us][bsf(pos.bitboard_of(~Us, KING))];

    // Pawn placement
    int pp = 0;
    for (int file = AFILE; file < HFILE; file++) {
        int pawn_count_us = sparse_pop_count(pos.bitboard_of(Us, PAWN) & MASK_FILE[file]);
        int pawn_count_them = sparse_pop_count(pos.bitboard_of(~Us, PAWN) & MASK_FILE[file]);
        if (pawn_count_us > 1) {
            pp -= (pawn_count_us - 1) * 75;
        }
        if (pawn_count_them > 1) {
            pp += (pawn_count_them - 1) * 75;
        }
    }

    // Sum up various scores
    return mv + ca + cc + np + kp + pp;
}

template <Color Us>
int see(const Position& pos, Square sq) {
    assert(pos.at(sq) == NO_PIECE || color_of(pos.at(sq)) != Us);

    Bitboard all_pieces  = pos.all_pieces<WHITE>() | pos.all_pieces<BLACK>();

    Bitboard attackers[2];
    attackers[Us] = pos.attackers_from<Us>(sq, all_pieces);
    attackers[~Us] = pos.attackers_from<~Us>(sq, all_pieces);

    int attackers_count[2][NPIECE_TYPES];
    for (size_t i = 0; i < NPIECE_TYPES; i++) {
        attackers_count[Us][i] = pop_count(attackers[Us] & pos.bitboard_of(Us, (PieceType) i));
        attackers_count[~Us][i] = pop_count(attackers[~Us] & pos.bitboard_of(~Us, (PieceType) i));
    }

    int ret = 0;
    size_t current_attackers[2] = {PAWN, PAWN};
    int sq_occupation = (pos.at(sq) == NO_PIECE) ? -1 : type_of(pos.at(sq));
    for (;;) {
        {
            bool attacked = false;
            for (size_t attacker = current_attackers[Us]; attacker < NPIECE_TYPES; attacker++) {
                current_attackers[Us] = attacker;
                if (attackers_count[Us][attacker]) {
                    if (sq_occupation >= 0) {
                        ret += piece_values[sq_occupation];
                    }
                    sq_occupation = attacker;
                    attackers_count[Us][attacker]--;
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
            for (size_t attacker = current_attackers[~Us]; attacker < NPIECE_TYPES; attacker++) {
                current_attackers[~Us] = attacker;
                if (attackers_count[~Us][attacker]) {
                    if (sq_occupation >= 0) {
                        ret -= piece_values[sq_occupation];
                    }
                    sq_occupation = attacker;
                    attackers_count[~Us][attacker]--;
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

template <Color Us>
int maxi(Position& pos, int alpha, int beta, unsigned int depth, const std::atomic<bool>& stop) {
    if (depth == 0) {
        return evaluate<Us>(pos);
    }

    MoveList<Us> moves(pos);
    if (moves.empty()) {
        if (pos.in_check<Us>()) {
            return -piece_values[KING] - depth;
        } else {
            return 0;
        }
    }
    for (Move move : moves) {
        pos.play<Us>(move);
        if (see<~Us>(pos, move.to()) < -100) {
            pos.undo<Us>(move);
            continue;
        }
        int score = mini<Us>(pos, alpha, beta, depth - 1, stop);
        pos.undo<Us>(move);
        if (stop) {
            return alpha;
        } else if (score >= beta) {
            return beta;
        } else if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

template <Color Us>
int mini(Position& pos, int alpha, int beta, unsigned int depth, const std::atomic<bool>& stop) {
    if (depth == 0) {
        return evaluate<Us>(pos);
    }

    MoveList<~Us> moves(pos);
    if (moves.empty()) {
        if (pos.in_check<~Us>()) {
            return piece_values[KING] + depth;
        } else {
            return 0;
        }
    }
    for (Move move : moves) {
        pos.play<~Us>(move);
        if (see<Us>(pos, move.to()) > 100) {
            pos.undo<~Us>(move);
            continue;
        }
        int score = maxi<Us>(pos, alpha, beta, depth - 1, stop);
        pos.undo<~Us>(move);
        if (stop) {
            return beta;
        } else if (score <= alpha) {
            return alpha;
        } else if (score < beta) {
            beta = score;
        }
    }

    return beta;
}

template int evaluate<WHITE>(const Position& pos);
template int evaluate<BLACK>(const Position& pos);

template int see<WHITE>(const Position& pos, Square square);
template int see<BLACK>(const Position& pos, Square square);

template int maxi<WHITE>(Position& pos, int alpha, int beta, unsigned int depth, const std::atomic<bool>& stop);
template int maxi<BLACK>(Position& pos, int alpha, int beta, unsigned int depth, const std::atomic<bool>& stop);
template int mini<WHITE>(Position& pos, int alpha, int beta, unsigned int depth, const std::atomic<bool>& stop);
template int mini<BLACK>(Position& pos, int alpha, int beta, unsigned int depth, const std::atomic<bool>& stop);
