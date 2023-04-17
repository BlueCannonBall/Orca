#include "search.hpp"
#include "evaluation.hpp"
#include "surge/src/types.h"
#include "util.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>

template <Color Us>
int Finder::alpha_beta(int alpha, int beta, int depth) {
    if (is_stopping()) {
        return 0;
    }

    // Mate distance pruning
    int mate_value = piece_values[KING] - current_ply();
    alpha = std::max(alpha, -mate_value);
    beta = std::min(beta, mate_value - 1);
    if (alpha >= beta) {
        nodes++;
        return alpha;
    }

    bool in_check = search.pos.in_check<Us>();

    // Check extensions
    if (in_check) {
        depth++;
    }

    Move hash_move;
    TT::iterator entry_it;
    if ((entry_it = tt->find(search.pos.get_hash())) != tt->end()) {
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

    nodes++;

    if (depth <= 0) {
        return quiesce<Us>(alpha, beta, depth - 1);
    }

    bool is_pv = alpha != beta - 1;

    // Reverse futility pruning
    int evaluation;
    if (search.pos.data) {
        int basic_evaluation;
        if (std::abs(basic_evaluation = evaluate_basic<Us>(search.pos)) < 100) {
            evaluation = basic_evaluation + prophet_get_residue((Prophet*) search.pos.data, Us);
        } else {
            evaluation = evaluate<Us>(search.pos);
        }
    } else {
        evaluation = evaluate<Us>(search.pos);
    }
    if (!is_pv && !in_check && depth <= 8) {
        if (evaluation - (120 * depth) >= beta) {
            return evaluation;
        }
    }

    Move moves[218];
    Move* last_move = search.pos.generate_legals<Us>(moves);

    int sort_scores[NSQUARES][NSQUARES] = {{0}};
    for (const Move* move = moves; move != last_move; move++) {
        if (*move == hash_move) {
            sort_scores[move->from()][move->to()] = 25000;
            continue;
        }

        if (move->flags() == QUIET) {
            if (is_killer_move<Us>(*move, current_ply())) {
                sort_scores[move->from()][move->to()] = 2;
                continue;
            }

            if (move->is_castling()) {
                sort_scores[move->from()][move->to()] = 1;
                continue;
            }

            sort_scores[move->from()][move->to()] = -30000 + get_history_score(*move);
            continue;
        }

        if (move->is_capture()) {
            if (move->flags() == EN_PASSANT) {
                sort_scores[move->from()][move->to()] = 10;
                continue;
            }

            sort_scores[move->from()][move->to()] += mvv_lva(search.pos, *move);

            if (see<Us>(search.pos, *move) >= -100) {
                sort_scores[move->from()][move->to()] += 10;
            } else {
                sort_scores[move->from()][move->to()] -= 30001;
            }
        }

        if (move->is_promotion()) {
            switch (move->promotion()) {
            case KNIGHT:
                sort_scores[move->from()][move->to()] += 5000;
                break;
            case BISHOP:
                sort_scores[move->from()][move->to()] += 6000;
                break;
            case ROOK:
                sort_scores[move->from()][move->to()] += 7000;
                break;
            case QUEEN:
                sort_scores[move->from()][move->to()] += 8000;
                break;
            default:
                throw std::logic_error("Invalid promotion");
            }
        }
    }
    std::sort(moves, last_move, [&sort_scores](Move a, Move b) {
        return sort_scores[a.from()][a.to()] > sort_scores[b.from()][b.to()];
    });

    Move best_move;
    TTEntryFlag flag = UPPERBOUND;
    for (const Move* move = moves; move != last_move; move++) {
        int reduced_depth = depth;

        // Late move reduction
        if (move - moves > 4 && depth > 2) {
            reduced_depth -= 2;
        }

        // Principle variation search
        int score;
        search.pos.play<Us>(*move);
        if (hash_move.is_null() || *move == hash_move || moves[0] != hash_move) {
            score = -alpha_beta<~Us>(-beta, -alpha, reduced_depth - 1);
        } else {
            score = -alpha_beta<~Us>(-alpha - 1, -alpha, reduced_depth - 1);
            if (score > alpha) {
                score = -alpha_beta<~Us>(-beta, -alpha, reduced_depth - 1);
            }
        }
        search.pos.undo<Us>(*move);

        if (is_stopping()) {
            return 0;
        }

        if (score > alpha) {
            best_move = *move;
            if (score >= beta) {
                if (move->flags() == QUIET) {
                    add_killer_move<Us>(*move, current_ply());
                    update_history_score(*move, depth);
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
            alpha = -mate_value;
        } else {
            alpha = 0;
        }
    } else if (best_move.is_null()) {
        best_move = moves[0];
    }

    if (!is_stopping()) {
        if (entry_it != tt->end()) {
            entry_it->second.score = alpha;
            entry_it->second.depth = depth;
            entry_it->second.best_move = best_move;
            entry_it->second.flag = flag;
        } else {
            TTEntry entry(alpha, depth, best_move, flag);
            tt->insert({search.pos.get_hash(), entry});
        }
    }

    return alpha;
}

template <Color Us>
int Finder::quiesce(int alpha, int beta, int depth) {
    if (is_stopping()) {
        return 0;
    }

    nodes++;

    int evaluation;
    if (search.pos.data) {
        int basic_evaluation;
        if (std::abs(basic_evaluation = evaluate_basic<Us>(search.pos)) < 100) {
            evaluation = basic_evaluation + prophet_get_residue((Prophet*) search.pos.data, Us);
        } else {
            evaluation = evaluate<Us>(search.pos);
        }
    } else {
        evaluation = evaluate<Us>(search.pos);
    }
    if (evaluation >= beta) {
        return beta;
    } else if (alpha < evaluation) {
        alpha = evaluation;
    }

    Move hash_move;
    TT::const_iterator entry_it;
    if ((entry_it = tt->find(search.pos.get_hash())) != tt->end()) {
        hash_move = entry_it->second.best_move;
    }

    bool late_endgame = !has_non_pawn_material(search.pos, Us);

    Move moves[218];
    Move* last_move = search.pos.generate_legals<Us>(moves);

    if (moves == last_move) {
        return 0;
    }

    int sort_scores[NSQUARES][NSQUARES] = {{0}};
    for (const Move* move = moves; move != last_move; move++) {
        if (*move == hash_move) {
            sort_scores[move->from()][move->to()] = 25000;
            continue;
        }

        if (move->is_capture()) {
            if (move->flags() == EN_PASSANT) {
                sort_scores[move->from()][move->to()] = 10;
                continue;
            }

            sort_scores[move->from()][move->to()] += mvv_lva(search.pos, *move);

            if (see<Us>(search.pos, *move) >= -100) {
                sort_scores[move->from()][move->to()] += 10;
            } else {
                sort_scores[move->from()][move->to()] -= 30001;
            }
        }

        if (move->is_promotion()) {
            switch (move->promotion()) {
            case KNIGHT:
                sort_scores[move->from()][move->to()] += 5000;
                break;
            case BISHOP:
                sort_scores[move->from()][move->to()] += 6000;
                break;
            case ROOK:
                sort_scores[move->from()][move->to()] += 7000;
                break;
            case QUEEN:
                sort_scores[move->from()][move->to()] += 8000;
                break;
            default:
                throw std::logic_error("Invalid promotion");
            }
        }
    }
    std::sort(moves, last_move, [&sort_scores](Move a, Move b) {
        return sort_scores[a.from()][a.to()] > sort_scores[b.from()][b.to()];
    });

    for (const Move* move = moves; move != last_move; move++) {
        if (!move->is_capture()) {
            continue;
        }

        // Delta pruning
        if (evaluation + piece_values[type_of(search.pos.at(move->to()))] + 200 < alpha && !late_endgame) {
            continue;
        }

        search.pos.play<Us>(*move);
        int score = -quiesce<~Us>(-beta, -alpha, depth - 1);
        search.pos.undo<Us>(*move);

        if (is_stopping()) {
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
void Finder::add_killer_move(Move move, int ply) {
    killer_moves[C][ply][2] = killer_moves[C][ply][1];
    killer_moves[C][ply][1] = killer_moves[C][ply][0];
    killer_moves[C][ply][0] = move;
}

template <Color C>
bool Finder::is_killer_move(Move move, int ply) const {
    bool ret = false;
    for (unsigned char i = 0; i < 3; i++) {
        if (move == killer_moves[C][ply][i]) {
            ret = true;
            break;
        }
    }
    return ret;
}

int Finder::get_history_score(Move move) const {
    return history_scores[move.from()][move.to()];
}

void Finder::update_history_score(Move move, int depth) {
    history_scores[move.from()][move.to()] += depth * depth;
    if (history_scores[move.from()][move.to()] >= 30000) {
        for (Square sq1 = a1; sq1 < NO_SQUARE; ++sq1) {
            for (Square sq2 = a1; sq2 < NO_SQUARE; ++sq2) {
                history_scores[sq1][sq2] >>= 1; // Divide by two
            }
        }
    }
}

std::vector<Move> get_pv(Position pos, const TT* tt) {
    std::vector<Move> ret;

    for (Color side_to_move = pos.turn(); pos.game_ply < NHISTORY; side_to_move = ~side_to_move) {
        TT::const_iterator entry_it;
        if ((entry_it = tt->find(pos.get_hash())) != tt->end()) {
            if (entry_it->second.best_move.is_null()) {
                break;
            } else {
                ret.push_back(entry_it->second.best_move);
                DYN_COLOR_CALL(pos.play, side_to_move, entry_it->second.best_move);
            }
        } else {
            break;
        }
    }

    return ret;
}

template int Finder::alpha_beta<WHITE>(int alpha, int beta, int depth);
template int Finder::alpha_beta<BLACK>(int alpha, int beta, int depth);

template int Finder::quiesce<WHITE>(int alpha, int beta, int depth);
template int Finder::quiesce<BLACK>(int alpha, int beta, int depth);

template void Finder::add_killer_move<WHITE>(Move move, int ply);
template bool Finder::is_killer_move<WHITE>(Move move, int ply) const;
