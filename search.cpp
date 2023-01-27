#include "search.hpp"
#include "surge/src/types.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>

template <Color Us>
int Finder::alpha_beta(Position& pos, int alpha, int beta, int depth, const std::atomic<bool>& stop) {
    if (stop) {
        return 0;
    }

    Move hash_move;
    TT::iterator entry_it;
    if ((entry_it = tt.find(pos.get_hash())) != tt.end()) {
        if (entry_it->second.depth >= depth) {
            if (entry_it->second.flag == EXACT) {
                return entry_it->second.score;
            } else if (entry_it->second.flag == LOWERBOUND) {
                alpha = std::max(alpha, entry_it->second.score);
            } else if (entry_it->second.flag == UPPERBOUND) {
                beta = std::min(beta, entry_it->second.score);
            }

            if (alpha >= beta) {
                return entry_it->second.score;
            }
        }
        hash_move = entry_it->second.best_move;
    }

    if (depth == 0) {
        return quiesce<Us>(pos, alpha, beta, depth - 1, stop);
    }

    bool in_check = pos.in_check<Us>();
    bool is_pv = alpha != beta - 1;

    // Reverse futility pruning
    if (!is_pv && !in_check && depth <= 8) {
        int evaluation = evaluate<Us>(pos);
        if (evaluation - (120 * depth) >= beta) {
            return evaluation;
        }
    }

    Move moves[218];
    Move* last_move = pos.generate_legals<Us>(moves);

    int move_evaluations[NSQUARES][NSQUARES] = {{0}};
    int sort_scores[NSQUARES][NSQUARES] = {{0}};
    for (const Move* move = moves; move != last_move; move++) {
        pos.play<Us>(*move);
        move_evaluations[move->from()][move->to()] = evaluate<Us>(pos);
        pos.undo<Us>(*move);

        if (*move == hash_move) {
            sort_scores[move->from()][move->to()] = piece_values[KING] * 2;
        } else if (is_killer_move<Us>(*move, depth)) {
            sort_scores[move->from()][move->to()] = piece_values[KING];
        } else {
            sort_scores[move->from()][move->to()] += move_evaluations[move->from()][move->to()];

            if (move->is_castling()) {
                sort_scores[move->from()][move->to()] += 5;
            }
            if (move->is_capture()) {
                sort_scores[move->from()][move->to()] += mvv_lva(pos, *move);

                // Static exchange evaluation
                if (!in_check && !move->is_promotion() && !(move->flags() == EN_PASSANT)) {
                    sort_scores[move->from()][move->to()] += see<Us>(pos, *move);
                }
            }
            if (move->is_promotion()) {
                sort_scores[move->from()][move->to()] += 50;
            }
        }
    }
    std::sort(moves, last_move, [&sort_scores](Move a, Move b) {
        return sort_scores[a.from()][a.to()] > sort_scores[b.from()][b.to()];
    });

    Move best_move;
    TTEntryFlag flag = UPPERBOUND;
    for (const Move* move = moves; move != last_move; move++) {
        // Futility pruning
        if (depth == 1 && move->flags() == QUIET && !in_check && move_evaluations[move->from()][move->to()] + 200 <= alpha) {
            continue;
        }
        // Razoring
        if (depth == 2 && move_evaluations[move->from()][move->to()] <= alpha) {
            break;
        }

        // Late move reduction
        int reduced_depth = depth;
        if (move - moves > 4 && depth > 2) {
            reduced_depth -= 2;
        }

        // PVS
        int score;
        pos.play<Us>(*move);
        if (hash_move.is_null() || *move == hash_move) {
            score = -alpha_beta<~Us>(pos, -beta, -alpha, reduced_depth - 1, stop);
        } else {
            score = -alpha_beta<~Us>(pos, -alpha - 1, -alpha, reduced_depth - 1, stop);
            if (score > alpha) {
                score = -alpha_beta<~Us>(pos, -beta, -alpha, reduced_depth - 1, stop);
            }
        }
        pos.undo<Us>(*move);

        if (stop) {
            return 0;
        }

        if (score > alpha) {
            best_move = *move;
            if (score >= beta) {
                if (move->flags() == QUIET) {
                    add_killer_move<Us>(*move, depth);
                }
                flag = LOWERBOUND;
                alpha = beta;
                break;
            }
            flag = EXACT;
            alpha = score;
        }
    }

    if (moves == last_move) {
        if (in_check) {
            alpha = -piece_values[KING] - depth;
        } else {
            alpha = 0;
        }
    } else if (best_move.is_null()) {
        best_move = moves[0];
    }

    if (!stop) {
        if (entry_it != tt.end()) {
            entry_it->second.score = alpha;
            entry_it->second.depth = depth;
            entry_it->second.best_move = best_move;
            entry_it->second.flag = flag;
        } else {
            TTEntry entry(alpha, depth, best_move, flag);
            tt[pos.get_hash()] = entry;
        }
    }

    return alpha;
}

template <Color Us>
int Finder::quiesce(Position& pos, int alpha, int beta, int depth, const std::atomic<bool>& stop) {
    if (stop) {
        return 0;
    }

    int evaluation = evaluate<Us>(pos);
    if (evaluation >= beta) {
        return beta;
    } else if (alpha < evaluation) {
        alpha = evaluation;
    }

    Move hash_move;
    TT::const_iterator entry_it;
    if ((entry_it = tt.find(pos.get_hash())) != tt.end()) {
        hash_move = entry_it->second.best_move;
    }

    bool in_check = pos.in_check<Us>();

    Move moves[218];
    Move* last_move = pos.generate_legals<Us>(moves);

    if (moves == last_move) {
        if (in_check) {
            return -piece_values[KING] - depth;
        } else {
            return 0;
        }
    }

    int move_evaluations[NSQUARES][NSQUARES] = {{0}};
    int sort_scores[NSQUARES][NSQUARES] = {{0}};
    for (const Move* move = moves; move != last_move; move++) {
        if (move->is_capture()) {
            pos.play<Us>(*move);
            move_evaluations[move->from()][move->to()] = evaluate<Us>(pos);
            pos.undo<Us>(*move);

            if (*move == hash_move) {
                sort_scores[move->from()][move->to()] = piece_values[KING] * 2;
            } else {
                sort_scores[move->from()][move->to()] += move_evaluations[move->from()][move->to()];

                sort_scores[move->from()][move->to()] += mvv_lva(pos, *move);

                // Static exchange evaluation
                if (!in_check && !move->is_promotion() && !(move->flags() == EN_PASSANT)) {
                    sort_scores[move->from()][move->to()] += see<Us>(pos, *move);
                }

                if (move->is_promotion()) {
                    sort_scores[move->from()][move->to()] += 50;
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

        // Delta pruning
        if (piece_values[type_of(pos.at(move->to()))] + 200 <= alpha) {
            continue;
        }

        pos.play<Us>(*move);
        int score = -quiesce<~Us>(pos, -beta, -alpha, depth - 1, stop);
        pos.undo<Us>(*move);

        if (stop) {
            return 0;
        }

        if (score > alpha) {
            if (score >= beta) {
                return beta;
            }
            alpha = score;
        }
    }

    return alpha;
}

template <Color C>
void Finder::add_killer_move(Move move, int depth) {
    killer_moves[C][depth][2] = killer_moves[C][depth][1];
    killer_moves[C][depth][1] = killer_moves[C][depth][0];
    killer_moves[C][depth][0] = move;
}

template <Color C>
bool Finder::is_killer_move(Move move, int depth) const {
    bool ret = false;
    for (unsigned char i = 0; i < 3; i++) {
        if (move == killer_moves[C][depth][i]) {
            ret = true;
            break;
        }
    }
    return ret;
}

template int Finder::alpha_beta<WHITE>(Position& pos, int alpha, int beta, int depth, const std::atomic<bool>& stop);
template int Finder::alpha_beta<BLACK>(Position& pos, int alpha, int beta, int depth, const std::atomic<bool>& stop);

template int Finder::quiesce<WHITE>(Position& pos, int alpha, int beta, int depth, const std::atomic<bool>& stop);
template int Finder::quiesce<BLACK>(Position& pos, int alpha, int beta, int depth, const std::atomic<bool>& stop);

template void Finder::add_killer_move<WHITE>(Move move, int depth);
template bool Finder::is_killer_move<WHITE>(Move move, int depth) const;
