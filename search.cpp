#include "search.hpp"
#include "evaluation.hpp"
#include "util.hpp"
#include <boost/range/adaptor/indexed.hpp>
#include <cmath>
#include <stdexcept>

ADD_INCR_OPERATORS_FOR(chess::Square);

int SearchAgent::alpha_beta(nnue::Board& board, int alpha, int beta, int depth, SearchInfo& info, std::function<bool(int)> is_stopping, bool do_null_move, bool do_lmr) {
    if (is_stopping(info.starting_depth)) {
        return 0;
    }

    int mate_value = get_value(chess::PieceType::KING) - info.current_ply(board.fullMoveNumber());

    chess::GameResult result;
    if ((result = board.isGameOver().second) != chess::GameResult::NONE) {
        switch (result) {
        case chess::GameResult::WIN:
        case chess::GameResult::NONE:
            throw std::runtime_error("Invalid game result");

        case chess::GameResult::LOSE:
            return -mate_value;

        case chess::GameResult::DRAW:
            return 0;
        }
    }

    // Mate distance pruning
    alpha = std::max(alpha, -mate_value);
    beta = std::min(beta, mate_value - 1);
    if (alpha >= beta) {
        return alpha;
    }

    bool in_check = board.inCheck();

    // Check extensions
    if (in_check) {
        ++depth;
    }

    chess::Move hash_move(0);
    TTEntry* entry;
    if ((entry = tt->probe(board.zobrist()))) {
        if (entry->depth >= depth) {
            if (entry->flag == TT_FLAG_EXACT) {
                return entry->score;
            } else if (entry->flag == TT_FLAG_LOWERBOUND) {
                alpha = std::max(alpha, entry->score);
            } else if (entry->flag == TT_FLAG_UPPERBOUND) {
                beta = std::min(beta, entry->score);
            }

            if (alpha >= beta) {
                return entry->score;
            }
        }
        hash_move = entry->best_move;
    }

    if (info.current_ply(board.fullMoveNumber()) > info.seldepth) {
        info.seldepth = info.current_ply(board.fullMoveNumber());
    }

    if (depth <= 0) {
        return quiesce(board, alpha, beta, depth - 1, info, is_stopping);
    }

    bool is_pv = alpha != beta - 1;
    int evaluation = evaluate_nnue(board);

    // Reverse futility pruning
    if (!is_pv && !in_check && depth <= 8 && evaluation - (120 * depth) >= beta) {
        return evaluation;
    }

    // Null move pruning
    if (do_null_move && !is_pv && !in_check && depth >= 2 && evaluation >= beta && has_non_pawn_material(board, board.sideToMove())) {
        board.makeNullMove();
        int score = -alpha_beta(board, -beta, -beta + 1, depth - 1 - (3 + (depth - 2) / 4), info, is_stopping, false, false);
        board.unmakeNullMove();

        if (is_stopping(info.starting_depth)) {
            return 0;
        }

        if (score >= beta) {
            return beta;
        }
    }

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    if (moves.size() > 1) {
        for (auto& move : moves) {
            int16_t score = 0;
            bool capture;

            if (entry && move == hash_move) {
                score = 25000;
                goto set_score;
            }

            capture = move.typeOf() == chess::Move::ENPASSANT ||
                      board.at(move.to()) != chess::Piece::NONE;

            if (!capture) {
                if (is_killer_move(move, board.sideToMove(), info.current_ply(board.fullMoveNumber()))) {
                    score = 2;
                } else if (move.typeOf() == chess::Move::CASTLING) {
                    score = 1;
                } else {
                    score = -30000 + get_history_score(move);
                }
                goto set_score;
            }

            if (capture) {
                if (move.typeOf() == chess::Move::ENPASSANT) {
                    score = 10;
                    goto set_score;
                }

                score += mvv_lva(board, move);

                if (move.typeOf() == chess::Move::PROMOTION || see(board, move) >= -100) {
                    score += 10;
                } else {
                    score -= 30001;
                }
            }

            if (move.typeOf() == chess::Move::PROMOTION) {
                switch (move.promotionType()) {
                case chess::PieceType::KNIGHT:
                    score += 5000;
                    break;
                case chess::PieceType::BISHOP:
                    score += 6000;
                    break;
                case chess::PieceType::ROOK:
                    score += 7000;
                    break;
                case chess::PieceType::QUEEN:
                    score += 8000;
                    break;
                default:
                    throw std::logic_error("Invalid promotion");
                }
            }

        set_score:
            move.setScore(score);
        }
        moves.sort();
    }

    long long lmr_index = std::round(6.f / (1.f + std::exp(info.starting_depth / 4.f))) + 3;
    chess::Move best_move(0);
    int original_alpha = alpha;
    for (const auto& move : moves | boost::adaptors::indexed()) {
        bool capture = move.value().typeOf() == chess::Move::ENPASSANT ||
                       board.at(move.value().to()) != chess::Piece::NONE;

        int score;
        board.makeMove(move.value());
        ++info.nodes;

        // Late move reductions
        if (do_lmr && moves.size() > 1 && depth >= 2 && move.index() > lmr_index && !capture) {
            score = -alpha_beta(board, -alpha - 1, -alpha, depth - 1 - std::round(std::log(move.index() - (lmr_index - 1)) * std::log(depth)), info, is_stopping, true, false);
            if (score <= alpha) {
                goto unmake_move;
            }
        }

        // Principle variation search
        if (!entry || move.value() == hash_move || moves[0] != hash_move) {
            score = -alpha_beta(board, -beta, -alpha, depth - 1, info, is_stopping, true, do_lmr);
        } else {
            score = -alpha_beta(board, -alpha - 1, -alpha, depth - 1, info, is_stopping, true, do_lmr);
            if (alpha < score && score < beta) {
                score = -alpha_beta(board, -beta, -alpha, depth - 1, info, is_stopping, true, do_lmr);
            }
        }

    unmake_move:
        board.unmakeMove(move.value());

        if (is_stopping(info.starting_depth)) {
            return 0;
        }

        if (score >= beta) {
            alpha = beta;
            best_move = move.value();
            break;
        }

        if (score > alpha) {
            alpha = score;
            if (!capture) {
                add_killer_move(move.value(), board.sideToMove(), info.current_ply(board.fullMoveNumber()));
                update_history_score(move.value(), depth);
            }
            best_move = move.value();
        }
    }

    if (!is_stopping(info.starting_depth)) {
        TTEntryFlag flag;
        if (alpha <= original_alpha) {
            flag = TT_FLAG_UPPERBOUND;
        } else if (alpha >= beta) {
            flag = TT_FLAG_LOWERBOUND;
        } else {
            flag = TT_FLAG_EXACT;
        }

        if (entry) {
            if (depth >= entry->depth) {
                entry->score = alpha;
                entry->depth = depth;
                entry->best_move = best_move;
                entry->flag = flag;
            }
        } else {
            tt->insert(TTEntry(board.zobrist(), alpha, depth, best_move, flag));
        }
    }

    return alpha;
}

int SearchAgent::quiesce(nnue::Board& board, int alpha, int beta, int depth, SearchInfo& info, std::function<bool(int)> is_stopping) {
    if (is_stopping(info.starting_depth)) {
        return 0;
    }

    int evaluation = evaluate_nnue(board);

    if (evaluation >= beta) {
        return beta;
    } else if (alpha < evaluation) {
        alpha = evaluation;
    }

    chess::Movelist moves;
    chess::movegen::legalmoves<chess::MoveGenType::CAPTURE>(moves, board);

    if (moves.empty()) {
        return evaluation;
    }

    chess::Move hash_move(0);
    if (TTEntry* entry = tt->probe(board.zobrist())) {
        hash_move = entry->best_move;
    }

    if (moves.size() > 1) {
        for (auto& move : moves) {
            int16_t score = 0;
            bool capture;

            if (move == hash_move) {
                score = 25000;
                goto set_score;
            }

            capture = move.typeOf() == chess::Move::ENPASSANT ||
                      board.at(move.to()) != chess::Piece::NONE;

            if (capture) {
                if (move.typeOf() == chess::Move::ENPASSANT) {
                    score = 10;
                    goto set_score;
                }

                score += mvv_lva(board, move);

                if (move.typeOf() == chess::Move::PROMOTION || see(board, move) >= -100) {
                    score += 10;
                } else {
                    score -= 30001;
                }
            }

            if (move.typeOf() == chess::Move::PROMOTION) {
                switch (move.promotionType()) {
                case chess::PieceType::KNIGHT:
                    score += 5000;
                    break;
                case chess::PieceType::BISHOP:
                    score += 6000;
                    break;
                case chess::PieceType::ROOK:
                    score += 7000;
                    break;
                case chess::PieceType::QUEEN:
                    score += 8000;
                    break;
                default:
                    throw std::logic_error("Invalid promotion");
                }
            }

        set_score:
            move.setScore(score);
        }
        moves.sort();
    }

    for (const auto& move : moves) {
        board.makeMove(move);
        ++info.nodes;
        int score = -quiesce(board, -beta, -alpha, depth - 1, info, is_stopping);
        board.unmakeMove(move);

        if (is_stopping(info.starting_depth)) {
            return 0;
        }

        if (score >= beta) {
            alpha = beta;
            break;
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

void SearchAgent::add_killer_move(const chess::Move& move, chess::Color color, int ply) {
    if (!is_killer_move(move, color, ply)) {
        killer_moves[(uint8_t) color][ply][2] = killer_moves[(uint8_t) color][ply][1];
        killer_moves[(uint8_t) color][ply][1] = killer_moves[(uint8_t) color][ply][0];
        killer_moves[(uint8_t) color][ply][0] = move;
    }
}

bool SearchAgent::is_killer_move(const chess::Move& move, chess::Color color, int ply) const {
    bool ret = false;
    for (size_t i = 0; i < 3; ++i) {
        if (killer_moves[(uint8_t) color][ply][i] == move) {
            ret = true;
            break;
        }
    }
    return ret;
}

int SearchAgent::get_history_score(const chess::Move& move) const {
    return history_scores[move.from()][move.to()];
}

void SearchAgent::update_history_score(const chess::Move& move, int depth) {
    history_scores[move.from()][move.to()] += depth * depth;
    if (history_scores[move.from()][move.to()] >= 30000) {
        for (chess::Square sq1 = chess::SQ_A1; sq1 < chess::NO_SQ; ++sq1) {
            for (chess::Square sq2 = chess::SQ_A1; sq2 < chess::NO_SQ; ++sq2) {
                history_scores[sq1][sq2] >>= 1; // Divide by two
            }
        }
    }
}

std::vector<chess::Move> get_pv(chess::Board board, const TT& tt) {
    std::vector<chess::Move> ret;

    while (board.fullMoveNumber() < 1024) {
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        if (const TTEntry* entry = tt.probe(board.zobrist())) {
            if (std::find(moves.begin(), moves.end(), entry->best_move) == moves.end()) {
                break;
            } else {
                ret.push_back(entry->best_move);
                board.makeMove(entry->best_move);
            }
        } else {
            break;
        }
    }

    return ret;
}
