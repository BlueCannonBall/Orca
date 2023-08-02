#include "chess.hpp"
#include "evaluation.hpp"
#include "nnue.hpp"
#include "search.hpp"
#include "uci.hpp"
#include "util.hpp"
#include <boost/atomic.hpp>
#include <boost/fiber/unbuffered_channel.hpp>
#include <boost/range/adaptor/indexed.hpp>
#include <boost/thread.hpp>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <prophet.h>
#include <string>
#include <vector>

class ScoredMove {
public:
    chess::Move move;
    int score;
    int static_evaluation;

    ScoredMove(const chess::Move& move, int score, int static_evaluation):
        move(move),
        score(score),
        static_evaluation(static_evaluation) {}

    inline operator chess::Move() const {
        return this->move;
    }

    inline bool operator==(const ScoredMove& scored_move) const {
        return this->move == scored_move.move;
    }

    inline bool operator!=(const ScoredMove& scored_move) const {
        return this->move != scored_move.move;
    }

    inline bool operator>(const ScoredMove& scored_move) const {
        return this->score > scored_move.score || (this->score == scored_move.score && this->static_evaluation > scored_move.static_evaluation);
    }

    inline bool operator<(const ScoredMove& scored_move) const {
        return this->score < scored_move.score || (this->score == scored_move.score && this->static_evaluation < scored_move.static_evaluation);
    }

    inline bool operator>=(const ScoredMove& scored_move) const {
        return this->score >= scored_move.score;
    }

    inline bool operator<=(const ScoredMove& scored_move) const {
        return this->score <= scored_move.score;
    }
};

void worker(boost::fibers::unbuffered_channel<SearchRequest>& channel, boost::atomic<bool>& stop) {
    Prophet* prophet = raise_prophet(nullptr);
    SearchRequest search_req;
    TT tt(64'000'000 / sizeof(TTEntry));
    while (channel.pop(search_req) == boost::fibers::channel_op_status::success) {
        if (search_req.quit) {
            prophet_die_for_sins(prophet);
            return;
        }

        stop.store(false, boost::memory_order_relaxed);
        tt.resize((search_req.hash_size * 1'000'000) / sizeof(TTEntry));
        search_req.board.accept_prophet(prophet);
        std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
        const auto is_stopping = [start_time, &search_req, &stop](int starting_depth) {
            return starting_depth > 1 && (std::chrono::steady_clock::now() - start_time > search_req.time || stop.load(boost::memory_order_relaxed));
        };

        if (search_req.new_game) {
            tt.clear();
        }

        chess::Movelist moves;
        chess::movegen::legalmoves(moves, search_req.board);
        if (moves.empty()) {
            logger.error("Invalid position given: " + search_req.board.getFen());
            search_req.board.release_prophet();
            continue;
        } else if (moves.size() == 1) {
            uci::bestmove(moves[0]);
            search_req.board.release_prophet();
            continue;
        }

        chess::Move best_move(0);
        int last_score;
        unsigned long long nodes = 0;
        int seldepth = 0;
        int max_ply = search_req.target_depth == -1 ? 1024 : (search_req.board.fullMoveNumber() + search_req.target_depth);
        for (int depth = 1; !is_stopping(depth) && search_req.board.fullMoveNumber() + depth <= max_ply && depth <= 256; ++depth) {
            for (auto& move : moves) {
                int16_t score = 0;
                bool capture;

                if (move == best_move) {
                    score = 25000;
                    goto set_score;
                }

                capture = move.typeOf() == chess::Move::ENPASSANT ||
                          search_req.board.at(move.to()) != chess::Piece::NONE;

                if (capture) {
                    if (move.typeOf() == chess::Move::ENPASSANT) {
                        score = 10;
                        goto set_score;
                    }

                    score += mvv_lva(search_req.board, move);

                    if (move.typeOf() == chess::Move::PROMOTION || see(search_req.board, move) >= -100) {
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

            SearchAgent agent(&tt);
            SearchInfo info(depth, search_req.board.fullMoveNumber());
            std::vector<ScoredMove> scored_moves;

            if (depth == 1 || search_req.multipv > 1) {
                int alpha = -get_value(chess::PieceType::KING);
                int beta = get_value(chess::PieceType::KING);

                for (const auto& move : moves) {
                    search_req.board.makeMove(move);
                    ++info.nodes;
                    int score = -agent.search(search_req.board, -beta, -alpha, info, is_stopping);
                    int static_evaluation = -evaluate_nnue(search_req.board);
                    search_req.board.unmakeMove(move);

                    if (is_stopping(depth)) {
                        break;
                    }

                    if (search_req.multipv > 1) {
                        scored_moves.emplace_back(move, score, static_evaluation);
                    } else {
                        if (score >= beta) {
                            alpha = beta;
                            scored_moves = {ScoredMove(move, alpha, static_evaluation)};
                            break;
                        }

                        if (score > alpha) {
                            alpha = score;
                            scored_moves = {ScoredMove(move, alpha, static_evaluation)};
                        }
                    }
                }
            } else {
                int lower_window_size = std::round((-150.f / (1.f + std::exp(-((depth - 1) / 3.f)))) + 175.f);
                int upper_window_size = std::round((-150.f / (1.f + std::exp(-((depth - 1) / 3.f)))) + 175.f);
                for (;;) {
                    int alpha = last_score - lower_window_size;
                    int beta = last_score + upper_window_size;

                    int original_alpha = last_score - lower_window_size;
                    for (const auto& move : moves) {
                        search_req.board.makeMove(move);
                        ++info.nodes;
                        int score = -agent.search(search_req.board, -beta, -alpha, info, is_stopping);
                        int static_evaluation = -evaluate_nnue(search_req.board);
                        search_req.board.unmakeMove(move);

                        if (is_stopping(depth)) {
                            break;
                        }

                        if (score >= beta) {
                            alpha = beta;
                            scored_moves = {ScoredMove(move, alpha, static_evaluation)};
                            continue;
                        }

                        if (score > alpha) {
                            alpha = score;
                            scored_moves = {ScoredMove(move, alpha, static_evaluation)};
                        }
                    }

                    if (is_stopping(depth)) {
                        break;
                    }

                    if (alpha <= original_alpha) {
                        lower_window_size <<= 1;
                    } else if (alpha >= beta) {
                        upper_window_size <<= 1;
                    } else {
                        break;
                    }
                }
            }

            if (!is_stopping(depth)) {
                std::sort(scored_moves.begin(), scored_moves.end(), std::greater<ScoredMove>());
                best_move = scored_moves[0].move;
                last_score = scored_moves[0].score;
                nodes += info.nodes;
                if (seldepth < info.seldepth) {
                    seldepth = info.seldepth;
                }

                std::chrono::milliseconds time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
                unsigned long long nps = (nodes / std::max((long long) time_elapsed.count(), 1ll)) * 1000;

                for (const auto& scored_move : scored_moves | boost::adaptors::indexed(1)) {
                    if (scored_move.index() > search_req.multipv) {
                        break;
                    }

                    search_req.board.makeMove(scored_move.value());
                    std::vector<chess::Move> pv = get_pv(search_req.board, tt);
                    search_req.board.unmakeMove(scored_move.value());
                    pv.insert(pv.begin(), scored_move.value());
                    pv.resize(std::min((int) pv.size(), seldepth));

                    std::vector<std::string> pv_strings;
                    std::transform(pv.cbegin(), pv.cend(), std::back_inserter(pv_strings), [](const chess::Move& move) {
                        return chess::uci::moveToUci(move);
                    });

                    std::vector<std::string> args;
                    if (scored_move.value().score >= get_value(chess::PieceType::KING) - 1024) {
                        args = {"multipv", std::to_string(scored_move.index()), "depth", std::to_string(depth), "seldepth", std::to_string(seldepth), "score", "mate", std::to_string((get_value(chess::PieceType::KING) - scored_move.value().score + 1) / 2), "nodes", std::to_string(nodes), "time", std::to_string(time_elapsed.count()), "nps", std::to_string(nps)};
                    } else if (scored_move.value().score <= -get_value(chess::PieceType::KING) + 1024) {
                        args = {"multipv", std::to_string(scored_move.index()), "depth", std::to_string(depth), "seldepth", std::to_string(seldepth), "score", "mate", std::to_string(-(scored_move.value().score + get_value(chess::PieceType::KING)) / 2), "nodes", std::to_string(nodes), "time", std::to_string(time_elapsed.count()), "nps", std::to_string(nps)};
                    } else {
                        args = {"multipv", std::to_string(scored_move.index()), "depth", std::to_string(depth), "seldepth", std::to_string(seldepth), "score", "cp", std::to_string(scored_move.value().score), "nodes", std::to_string(nodes), "time", std::to_string(time_elapsed.count()), "nps", std::to_string(nps)};
                    }

                    if (!pv_strings.empty()) {
                        args.push_back("pv");
                        args.insert(args.end(), pv_strings.begin(), pv_strings.end());
                    }

                    uci::send_message("info", args);
                }
            }
        }

        uci::bestmove(best_move);
        search_req.board.release_prophet();
    }
}

int main() {
    logger.info("Engine started");

    std::cout << "_______                         _____   ______   ______  ___________\n"
                 "__/ __ \\__________________ _    ___/ | / /__/ | / /_/ / / /__/ ____/\n"
                 "_/ / / /_/ ___// ___// __ `/    __/  |/ /__/  |/ /_/ / / /__/ __/\n"
                 "/ /_/ /_/ /   / /__ / /_/ /     _/ /|  / _/ /|  / / /_/ / _/ /___\n"
                 "\\____/ /_/    \\___/ \\__,_/      /_/ |_/  /_/ |_/  \\____/  /_____/\n";
#if defined(ORCA_TIMESTAMP) && defined(ORCA_COMPILER)
    std::cout << "Orca NNUE (mono-accumulator 1x768 feature space i16 quantized eval with 64x scaling factor) compiled @ " << ORCA_TIMESTAMP << " on compiler " << ORCA_COMPILER << std::endl;
#endif

    nnue::Board board(chess::STARTPOS);
    uint8_t multipv = 1;
    uint32_t hash_size = 64;
    bool new_game = false;

    boost::atomic<bool> stop = false;
    boost::fibers::unbuffered_channel<SearchRequest> channel;
    boost::thread worker_thread(std::bind(worker, std::ref(channel), std::ref(stop)));

    for (;;) {
        auto message = uci::poll();

        str_switch(message.command) {
            str_case("uci"):
            {
                uci::send_message("id", {"name", "Orca"});
                uci::send_message("id", {"author", "BlueCannonBall"});
                uci::send_message("option", {"name", "MultiPV", "type", "spin", "default", "1", "min", "1", "max", "255"});
                uci::send_message("option", {"name", "Hash", "type", "spin", "default", "64", "min", "1", "max", "65535"});
                uci::send_message("uciok");
                break;
            }

            str_case("isready"):
            {
                uci::send_message("readyok");
                break;
            }

            str_case("setoption"):
            {
                str_switch(message.args[1]) {
                    str_case("MultiPV"):
                    {
                        multipv = std::stoi(message.args[3]);
                        break;
                    }
                    str_case("Hash"):
                    {
                        hash_size = std::stoi(message.args[3]);
                        break;
                    }
                }
                break;
            }

            str_case("ucinewgame"):
            {
                new_game = true;
                break;
            }

            str_case("position"):
            {
                if (message.args[0] == "startpos") {
                    board.setFen(chess::STARTPOS);
                    if (message.args.size() > 2) {
                        for (size_t i = 2; i < message.args.size(); ++i) {
                            board.makeMove(chess::uci::uciToMove(board, message.args[i]));
                        }
                    }
                } else if (message.args[0] == "fen") {
                    std::string fen;
                    for (size_t i = 1; i < message.args.size(); ++i) {
                        fen += message.args[i];
                        if (i + 1 != message.args.size()) {
                            fen.push_back(' ');
                        }
                    }
                    board.setFen(fen);
                    if (message.args.size() > 8) {
                        for (size_t i = 8; i < message.args.size(); ++i) {
                            board.makeMove(chess::uci::uciToMove(board, message.args[i]));
                        }
                    }
                }
                break;
            }

            str_case("go"):
            {
                using namespace std::chrono_literals;

                std::chrono::milliseconds search_time = 10s;

                std::chrono::milliseconds movetime = 0ms;
                std::chrono::milliseconds wtime = 0ms;
                std::chrono::milliseconds btime = 0ms;
                std::chrono::milliseconds winc = 0ms;
                std::chrono::milliseconds binc = 0ms;
                bool infinite = false;
                int depth = -1;
                for (auto it = message.args.begin(); it != message.args.end(); ++it) {
                    str_switch(*it) {
                        str_case("movetime"):
                        {
                            movetime = std::chrono::milliseconds(std::stoi(*++it));
                            break;
                        }
                        str_case("wtime"):
                        {
                            wtime = std::chrono::milliseconds(std::stoi(*++it));
                            break;
                        }
                        str_case("btime"):
                        {
                            btime = std::chrono::milliseconds(std::stoi(*++it));
                            break;
                        }
                        str_case("winc"):
                        {
                            winc = std::chrono::milliseconds(std::stoi(*++it));
                            break;
                        }
                        str_case("binc"):
                        {
                            binc = std::chrono::milliseconds(std::stoi(*++it));
                            break;
                        }
                        str_case("infinite"):
                        {
                            infinite = true;
                            break;
                        }
                        str_case("depth"):
                        {
                            depth = stoi(*++it);
                            break;
                        }
                    }
                }

                if (infinite || depth != -1) {
                    search_time = 10h;
                } else {
                    if (board.sideToMove() == chess::Color::WHITE) {
                        if (movetime != 0ms) {
                            search_time = movetime;
                        } else if (wtime != 0ms) {
                            search_time = wtime / 30;
                        }

                        if (search_time < winc + 25ms) {
                            search_time = winc + 25ms;
                        }
                    } else if (board.sideToMove() == chess::Color::BLACK) {
                        if (movetime != 0ms) {
                            search_time = movetime;
                        } else if (btime != 0ms) {
                            search_time = btime / 30;
                        }

                        if (search_time < binc + 25ms) {
                            search_time = binc + 25ms;
                        }
                    } else {
                        throw std::logic_error("Invalid side to move");
                    }
                }

                channel.push(SearchRequest {
                    .board = board,
                    .multipv = multipv,
                    .hash_size = hash_size,
                    .time = search_time,
                    .target_depth = depth,
                    .new_game = new_game});
                new_game = false;

                break;
            }

            str_case("stop"):
            {
                stop.store(true, boost::memory_order_relaxed);
                break;
            }

            str_case("quit"):
            {
                stop.store(true, boost::memory_order_relaxed);
                channel.push(SearchRequest {
                    .quit = true,
                });
                worker_thread.join();
                return 0;
            }

            str_case("show"):
            {
                std::cout << "Current board:" << std::endl;
                std::cout << board << std::endl;
                break;
            }

            str_case("eval"):
                str_case("evaluate"):
            {
                int evaluation = evaluate(board, true);
                std::cout << "HCE Evaluation: " << evaluation << std::endl;
                break;
            }

            str_case("see"):
            {
                int evaluation = see(board, chess::uci::uciToMove(board, message.args[0]), true);
                std::cout << "SEE Evaluation: " << evaluation << std::endl;
            }
        }
    }

    return 0;
}
