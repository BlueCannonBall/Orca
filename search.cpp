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
    for (let y = 7; y > -1; y--) {
        for (let x = 0; x < 8; x++) {
            table[0].push(Math.round(-Math.min(distance(x, y, 0, 7), distance(x, y, 7, 7)) * 5));
            table[1].push(Math.round(-Math.min(distance(x, y, 0, 0), distance(x, y, 7, 0)) * 5));
        }
    }
    */
    static constexpr int king_pcsq_table[2][64] = {{0, -5, -10, -15, -15, -10, -5, 0, -5, -7, -11, -16, -16, -11, -7, -5, -10, -11, -14, -18, -18, -14, -11, -10, -15, -16, -18, -21, -21, -18, -16, -15, -20, -21, -22, -25, -25, -22, -21, -20, -25, -25, -27, -29, -29, -27, -25, -25, -30, -30, -32, -34, -34, -32, -30, -30, -35, -35, -36, -38, -38, -36, -35, -35}, {-35, -35, -36, -38, -38, -36, -35, -35, -30, -30, -32, -34, -34, -32, -30, -30, -25, -25, -27, -29, -29, -27, -25, -25, -20, -21, -22, -25, -25, -22, -21, -20, -15, -16, -18, -21, -21, -18, -16, -15, -10, -11, -14, -18, -18, -14, -11, -10, -5, -7, -11, -16, -16, -11, -7, -5, 0, -5, -10, -15, -15, -10, -5, 0}};
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

    // Pinned count
    int pc = 0;
    pc -= pop_count(pos.pinned & pos.all_pieces<Us>()) * 20;
    pc += pop_count(pos.pinned & pos.all_pieces<~Us>()) * 20;

    // Sum up various scores
    return mv + ca + cc + np + kp + pp + cs + pc;
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

template <Color Us>
int alpha_beta(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop) {
    TT::iterator entry_it = tt.find(pos.get_hash());
    Move hash_move;
    if (entry_it != tt.end()) {
        if (entry_it->second.depth >= depth) {
            return entry_it->second.score;
        } else if (entry_it->second.depth > 0) {
            hash_move = entry_it->second.hash_move;
        }
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
            if (entry_it != tt.end()) {
                entry_it->second.score = alpha;
                entry_it->second.depth = -piece_values[KING] - depth;
            } else {
                tt[pos.get_hash()] = TTEntry(alpha, -piece_values[KING] - depth);
            }
            return -piece_values[KING] - depth;
        } else {
            if (entry_it != tt.end()) {
                entry_it->second.score = alpha;
                entry_it->second.depth = 0;
            } else {
                tt[pos.get_hash()] = TTEntry(alpha, 0);
            }
            return 0;
        }
    }

    int static_move_scores[64][64] = {{0}};
    int sort_scores[64][64] = {{0}};
    for (const Move* move = moves; move != last_move; move++) {
        pos.play<Us>(*move);
        static_move_scores[move->from()][move->to()] = evaluate<Us>(pos);
        pos.undo<Us>(*move);

        if (*move == hash_move) {
            sort_scores[move->from()][move->to()] += piece_values[KING] * 2;
        } else {
            sort_scores[move->from()][move->to()] += static_move_scores[move->from()][move->to()];

            pos.play<Us>(*move);
            int swapoff = -see<~Us>(pos, move->to());
            pos.undo<Us>(*move);
            sort_scores[move->from()][move->to()] += swapoff;

            if (move->is_capture()) {
                sort_scores[move->from()][move->to()] += 15;
            }
            if (move->is_promotion()) {
                sort_scores[move->from()][move->to()] += 30;
            }
        }
    }
    std::sort(moves, last_move, [&sort_scores](Move a, Move b) {
        return sort_scores[a.from()][a.to()] > sort_scores[b.from()][b.to()];
    });

    for (const Move* move = moves; move != last_move; move++) {
        if (depth <= 2 && static_move_scores[move->from()][move->to()] + 100 <= alpha) {
            break;
        }

        pos.play<Us>(*move);
        int reduced_depth = depth - 1;
        if (move - moves > 4 && depth > 3) {
            reduced_depth -= 2;
        }
        int score = -alpha_beta<~Us>(pos, -beta, -alpha, reduced_depth, tt, stop);
        pos.undo<Us>(*move);

        if (stop) {
            return alpha;
        } else if (score >= beta) {
            return beta;
        } else if (score > alpha) {
            hash_move = *move;
            alpha = score;
        }
    }

    if (entry_it != tt.end()) {
        entry_it->second.score = alpha;
        entry_it->second.depth = depth;
        entry_it->second.hash_move = hash_move;
    } else {
        tt[pos.get_hash()] = TTEntry(alpha, depth, hash_move);
    }
    return alpha;
}

template <Color Us>
int quiesce(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop) {
    TT::iterator entry_it = tt.find(pos.get_hash());
    Move hash_move;
    if (entry_it != tt.end()) {
        if (entry_it->second.depth >= depth) {
            return entry_it->second.score;
        } else {
            hash_move = entry_it->second.hash_move;
        }
    }

    int stand_pat = evaluate<Us>(pos);
    if (stand_pat >= beta) {
        return beta;
    } else if (alpha < stand_pat) {
        alpha = stand_pat;
    }

    Move moves[218];
    Move* last_move = pos.generate_legals<Us>(moves);

    if (moves == last_move) {
        if (pos.in_check<Us>()) {
            if (entry_it != tt.end()) {
                entry_it->second.score = alpha;
                entry_it->second.depth = -piece_values[KING] - depth;
            } else {
                tt[pos.get_hash()] = TTEntry(alpha, -piece_values[KING] - depth);
            }
            return -piece_values[KING] - depth;
        } else {
            if (entry_it != tt.end()) {
                entry_it->second.score = alpha;
                entry_it->second.depth = 0;
            } else {
                tt[pos.get_hash()] = TTEntry(alpha, 0);
            }
            return 0;
        }
    }

    int static_move_scores[64][64] = {{0}};
    int sort_scores[64][64] = {{0}};
    for (const Move* move = moves; move != last_move; move++) {
        if (move->is_capture()) {
            pos.play<Us>(*move);
            static_move_scores[move->from()][move->to()] = evaluate<Us>(pos);
            pos.undo<Us>(*move);

            if (*move == hash_move) {
                sort_scores[move->from()][move->to()] += piece_values[KING] * 2;
            } else {
                sort_scores[move->from()][move->to()] += static_move_scores[move->from()][move->to()];

                pos.play<Us>(*move);
                int swapoff = -see<~Us>(pos, move->to());
                pos.undo<Us>(*move);
                sort_scores[move->from()][move->to()] += swapoff;

                if (move->is_promotion()) {
                    sort_scores[move->from()][move->to()] += 30;
                }
            }
        } else {
            sort_scores[move->from()][move->to()] = -piece_values[KING] * 2;
        }
    }
    std::sort(moves, last_move, [&sort_scores](Move a, Move b) {
        return sort_scores[a.from()][a.to()] > sort_scores[b.from()][b.to()];
    });

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
            hash_move = *move;
        }
    }

    if (entry_it != tt.end()) {
        entry_it->second.score = alpha;
        entry_it->second.depth = depth;
        entry_it->second.hash_move = hash_move;
    } else {
        tt[pos.get_hash()] = TTEntry(alpha, depth, hash_move);
    }
    return alpha;
}

template int evaluate<WHITE>(const Position& pos);
template int evaluate<BLACK>(const Position& pos);

template int see<WHITE>(const Position& pos, Square sq);
template int see<BLACK>(const Position& pos, Square sq);

template int alpha_beta<WHITE>(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop);
template int alpha_beta<BLACK>(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop);

template int quiesce<WHITE>(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop);
template int quiesce<BLACK>(Position& pos, int alpha, int beta, int depth, TT& tt, const std::atomic<bool>& stop);
