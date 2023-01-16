#include "search.hpp"
#include "surge/src/types.h"
#include "util.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <climits>
#include <cmath>

GameProgress get_progress(int mv1, int mv2) {
    return (mv1 <= 1300 && mv2 <= 1300) ? ENDGAME : MIDGAME;
}

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
    np -= pop_count(pos.bitboard_of(Us, KNIGHT) & MASK_FILE[AFILE] & MASK_RANK[RANK1] & MASK_FILE[HFILE] & MASK_RANK[RANK8]) * 50;
    np += pop_count(pos.bitboard_of(~Us, KNIGHT) & MASK_FILE[AFILE] & MASK_RANK[RANK1] & MASK_FILE[HFILE] & MASK_RANK[RANK8]) * 50;

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
        cs = -23;
    } else if (pos.in_check<~Us>()) {
        cs = 23;
    }

    // Pinned count
    int pc = 0;
    pc -= pop_count(pos.pinned & pos.all_pieces<Us>()) * 20;
    pc += pop_count(pos.pinned & pos.all_pieces<~Us>()) * 20;

    // Sum up various scores
    return mv + ca + cc + np + kp + pp + cs + pc;
}

bool see(const Position& pos, Move move, int threshold) {
    if (move.is_promotion()) {
        return true;
    }

    Square from_sq = move.from(), to_sq = move.to();

    int value = piece_values[type_of(pos.at(to_sq))] - threshold;
    if (value < 0)
        return false;

    value -= piece_values[type_of(pos.at(from_sq))];
    if (value >= 0)
        return true;

    Bitboard occ = BOTH_COLOR_CALL(pos.all_pieces) ^ SQUARE_BB[from_sq];
    Bitboard attackers = BOTH_COLOR_CALL(pos.attackers_from, to_sq, occ);
    Bitboard our_attackers;

    Bitboard diagonal_sliders = BOTH_COLOR_CALL(pos.diagonal_sliders);
    Bitboard orthogonal_sliders = BOTH_COLOR_CALL(pos.orthogonal_sliders);

    Color us = ~pos.turn();
    for (;;) {
        attackers &= occ;
        our_attackers = attackers & DYN_COLOR_CALL(pos.all_pieces, us);

        if (our_attackers == Bitboard(0)) {
            break;
        }

        PieceType attacking_pt = attacking_pt;
        for (size_t i = 0; i < NPIECE_TYPES; i++) {
            if ((our_attackers & pos.bitboard_of(us, (PieceType) i)) != Bitboard(0)) {
                attacking_pt = (PieceType) i;
                break;
            }
        }

        us = ~us;

        value = -value - 1 - piece_values[attacking_pt];

        std::cout << value << std::endl;
        if (value >= 0) {
            if (attacking_pt == KING && ((attackers & DYN_COLOR_CALL(pos.all_pieces, us)) != Bitboard(0))) {
                us = ~us;
            }
            break;
        }

        occ ^= SQUARE_BB[bsf(our_attackers & pos.bitboard_of(us, attacking_pt))];

        if (attacking_pt == PAWN || attacking_pt == BISHOP || attacking_pt == QUEEN) {
            attackers |= attacks<BISHOP>(to_sq, occ) & diagonal_sliders;
        }
        if (attacking_pt == ROOK || attacking_pt == QUEEN) {
            attackers |= attacks<ROOK>(to_sq, occ) & orthogonal_sliders;
        }
    }

    return us != color_of(pos.at(from_sq));
}

template <Color Us>
int alpha_beta(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop) {
    TT::iterator entry_it = tt.find(pos.get_hash());
    if (entry_it != tt.end() && entry_it->second.depth >= depth) {
        return entry_it->second.score;
    }

    if (depth == 0) {
        int score = quiesce<Us>(pos, alpha, beta, depth - 1, tt, stop);
        if (entry_it != tt.end()) {
            entry_it->second.score = score;
            entry_it->second.depth = depth;
        } else {
            tt[pos.get_hash()] = TTEntry(score, depth);
        }
        return score;
    }

    Move moves[218];
    Move* last_move = pos.generate_legals<Us>(moves);

    if (moves == last_move) {
        if (pos.in_check<Us>()) {
            return -piece_values[KING] - depth;
        } else {
            return 0;
        }
    }

    int static_move_scores[64][64] = {{0}};
    int sort_scores[64][64] = {{0}};
    for (const Move* move = moves; move != last_move; move++) {
        pos.play<Us>(*move);
        static_move_scores[move->from()][move->to()] = evaluate<Us>(pos);
        pos.undo<Us>(*move);

        sort_scores[move->from()][move->to()] += static_move_scores[move->from()][move->to()];
        if (move->is_capture()) {
            sort_scores[move->from()][move->to()] += 15;
        }
    }
    std::sort(moves, last_move, [&sort_scores](Move a, Move b) {
        return sort_scores[a.from()][a.to()] > sort_scores[b.from()][b.to()];
    });

    for (const Move* move = moves; move != last_move; move++) {
        if (depth <= 2 && static_move_scores[move->from()][move->to()] + 100 <= alpha) {
            break;
        }

        // Null move heuristic
        // if (depth > 3 && !pos.in_check<Us>() && (pos.bitboard_of(Us, KNIGHT) || pos.bitboard_of(Us, BISHOP) || pos.bitboard_of(Us, ROOK) || pos.bitboard_of(Us, QUEEN))) {
        //     pos.play<Us>(Move());
        //     int score = -alpha_beta<~Us>(pos, -beta, -alpha, depth - 3, tt, stop);
        //     pos.undo<Us>(Move());

        //     if (stop) {
        //         return alpha;
        //     } else if (score >= beta) {
        //         return beta;
        //     }
        // }

        pos.play<Us>(*move);
        int score = -alpha_beta<~Us>(pos, -beta, -alpha, depth - 1, tt, stop);
        pos.undo<Us>(*move);

        if (stop) {
            return alpha;
        } else if (score >= beta) {
            return beta;
        } else if (score > alpha) {
            alpha = score;
        }
    }

    if (entry_it != tt.end()) {
        entry_it->second.score = alpha;
        entry_it->second.depth = depth;
    } else {
        tt[pos.get_hash()] = TTEntry(alpha, depth);
    }
    return alpha;
}

template <Color Us>
int quiesce(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop) {
    TT::iterator entry_it = tt.find(pos.get_hash());
    if (entry_it != tt.end() && entry_it->second.depth >= depth) {
        return entry_it->second.score;
    }

    int stand_pat = evaluate<Us>(pos);
    if (stand_pat >= beta) {
        return beta;
    } else if (alpha < stand_pat) {
        alpha = stand_pat;
    }

    Move moves[218];
    Move* last_move = pos.generate_legals<Us>(moves);
    int static_move_scores[64][64] = {{0}};
    int sort_scores[64][64] = {{0}};
    for (const Move* move = moves; move != last_move; move++) {
        if (move->is_capture()) {
            pos.play<Us>(*move);
            static_move_scores[move->from()][move->to()] = evaluate<Us>(pos);
            pos.undo<Us>(*move);

            sort_scores[move->from()][move->to()] += static_move_scores[move->from()][move->to()];
        } else {
            sort_scores[move->from()][move->to()] = -piece_values[KING] * 2;
        }
    }
    std::sort(moves, last_move, [&sort_scores](Move a, Move b) {
        return sort_scores[a.from()][a.to()] > sort_scores[b.from()][b.to()];
    });

    if (moves == last_move) {
        if (pos.in_check<Us>()) {
            return -piece_values[KING] - depth;
        } else {
            return 0;
        }
    }

    for (const Move* move = moves; move != last_move; move++) {
        if (!move->is_capture()) {
            break;
        }

        if (static_move_scores[move->from()][move->to()] + 400 <= alpha) {
            break;
        } else if (piece_values[type_of(pos.at(move->to()))] + 200 <= alpha) {
            continue;
        }

        pos.play<Us>(*move);
        int score = -quiesce<~Us>(pos, -beta, -alpha, depth - 1, tt, stop);
        pos.undo<Us>(*move);

        if (stop) {
            return alpha;
        } else if (score >= beta) {
            return beta;
        } else if (score > alpha) {
            alpha = score;
        }
    }

    if (entry_it != tt.end()) {
        entry_it->second.score = alpha;
        entry_it->second.depth = depth;
    } else {
        tt[pos.get_hash()] = TTEntry(alpha, depth);
    }
    return alpha;
}

template int evaluate<WHITE>(const Position& pos);
template int evaluate<BLACK>(const Position& pos);

template int alpha_beta<WHITE>(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop);
template int alpha_beta<BLACK>(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop);

template int quiesce<WHITE>(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop);
template int quiesce<BLACK>(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop);
