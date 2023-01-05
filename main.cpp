#include "search.hpp"
#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include "uci.hpp"

class Engine: public uci::Engine {
protected:
    Position pos;
    tp::ThreadPool pool;

public:
    Engine() :
        uci::Engine("orca", "BlueCannonBall"),
        pos(DEFAULT_FEN) { }

protected:
    void on_message(const std::string& command, const std::vector<std::string>& args) override {
        str_switch(command) {
            str_case("position") :
            {
                if (args[0] == "startpos") {
                    pos = Position(DEFAULT_FEN);
                    if (args.size() > 1) {
                        for (size_t i = 2; i < args.size(); i++) {
                            Square from = create_square(File(args[i][0] - 'a'), Rank(args[i][1] - '1'));
                            Square to = create_square(File(args[i][2] - 'a'), Rank(args[i][3] - '1'));
                            MoveFlags flags = QUIET;
                            if (((i - 2) % 2) == 0) {
                                if (pos.at(to) != NO_PIECE) {
                                    if (pos.at(from) == WHITE_PAWN && rank_of(to) == RANK8) {
                                        flags = PC_QUEEN;
                                    } else {
                                        flags = CAPTURE;
                                    }
                                } else if (pos.at(from) == WHITE_PAWN && rank_of(from) == RANK2 && rank_of(to) == RANK4) {
                                    flags = DOUBLE_PUSH;
                                } else if (pos.at(from) == WHITE_KING && from == e1) {
                                    if (to == g1) {
                                        flags = OO;
                                    } else if (to == c1) {
                                        flags = OOO;
                                    }
                                } else if (pos.at(from) == WHITE_PAWN) {
                                    if (rank_of(to) == RANK8) {
                                        flags = PR_QUEEN;
                                    } else if (file_of(from) != file_of(to) && pos.at(to) == NO_PIECE) {
                                        flags = EN_PASSANT;
                                    }
                                }
                                pos.play<WHITE>(Move(from, to, flags));
                            } else {
                                if (pos.at(to) != NO_PIECE) {
                                    if (pos.at(from) == BLACK_PAWN && rank_of(to) == RANK1) {
                                        flags = PC_QUEEN;
                                    } else {
                                        flags = CAPTURE;
                                    }
                                } else if (pos.at(from) == BLACK_PAWN && rank_of(from) == RANK7 && rank_of(to) == RANK5) {
                                    flags = DOUBLE_PUSH;
                                } else if (pos.at(from) == BLACK_KING && from == e8) {
                                    if (to == g8) {
                                        flags = OO;
                                    } else if (to == c8) {
                                        flags = OOO;
                                    }
                                } else if (pos.at(from) == BLACK_PAWN) {
                                    if (rank_of(to) == RANK1) {
                                        flags = PR_QUEEN;
                                    } else if (file_of(from) != file_of(to) && pos.at(to) == NO_PIECE) {
                                        flags = EN_PASSANT;
                                    }
                                }
                                pos.play<BLACK>(Move(from, to, flags));
                            }
                        }
                    }
                } else if (args[0] == "fen") {
                    std::string fen;
                    for (size_t i = 1; i < args.size(); i++) {
                        fen += args[i];
                        if (i + 1 != args.size()) {
                            fen.push_back(' ');
                        }
                    }
                    pos = Position(fen);
                }
                break;
            }

            str_case("go") :
            {
                std::chrono::milliseconds movetime = std::chrono::milliseconds(-1);
                std::chrono::milliseconds wtime = std::chrono::milliseconds(-1);
                std::chrono::milliseconds btime = std::chrono::milliseconds(-1);
                for (auto it = args.begin(); it != args.end(); it++) {
                    if (*it == "movetime") {
                        movetime = std::chrono::milliseconds(stoi(*++it));
                    } else if (*it == "wtime") {
                        wtime = std::chrono::milliseconds(stoi(*++it));
                    } else if (*it == "btime") {
                        btime = std::chrono::milliseconds(stoi(*++it));
                    }
                }

                std::chrono::milliseconds search_time = std::chrono::seconds(10);
                unsigned int starting_depth = 6;

                Move best_move;
                int best_move_score;
                unsigned int best_move_depth;
                if (pos.turn() == WHITE) {
                    if (movetime != std::chrono::milliseconds(-1)) {
                        search_time = movetime;
                    } else if (wtime != std::chrono::milliseconds(-1)) {
                        search_time = std::chrono::milliseconds(std::min(wtime.count() / 40, 10000L));
                    }

                    if (search_time.count() < 250) {
                        starting_depth = 4;
                    } else if (search_time.count() < 3000) {
                        starting_depth = 5;
                    }

                    best_move = find_best_move<WHITE>(pos, search_time, starting_depth, pool, &best_move_score, &best_move_depth);
                } else if (pos.turn() == BLACK) {
                    if (movetime != std::chrono::milliseconds(-1)) {
                        search_time = movetime;
                    } else if (btime != std::chrono::milliseconds(-1)) {
                        search_time = std::chrono::milliseconds(std::min(btime.count() / 40, 10000L));
                    }

                    if (search_time.count() < 250) {
                        starting_depth = 4;
                    } else if (search_time.count() < 3000) {
                        starting_depth = 5;
                    }

                    best_move = find_best_move<BLACK>(pos, search_time, starting_depth, pool, &best_move_score, &best_move_depth);
                } else {
                    throw std::logic_error("Invalid side to move");
                }
                this->send_message("info", {"depth", std::to_string(best_move_depth), "score", "cp", std::to_string(best_move_score)});
                this->move(best_move);
                break;
            }

            str_case("quit") :
            {
                exit(0);
            }

            str_case("show") :
            {
                std::cout << pos << std::endl;
                break;
            }

            str_case("evaluate") :
            {
                if (pos.turn() == WHITE) {
                    this->send_message("score", {std::to_string(evaluate<WHITE>(pos))});
                } else if (pos.turn() == BLACK) {
                    this->send_message("score", {std::to_string(evaluate<BLACK>(pos))});
                } else {
                    throw std::logic_error("Invalid side to move");
                }
                break;
            }

            str_case("see") :
            {
                if (pos.turn() == WHITE) {
                    this->send_message("swapoff", {std::to_string(see<WHITE>(pos, create_square(File(args[0][0] - 'a'), Rank(args[0][1] - '1'))))});
                } else if (pos.turn() == BLACK) {
                    this->send_message("swapoff", {std::to_string(see<BLACK>(pos, create_square(File(args[0][0] - 'a'), Rank(args[0][1] - '1'))))});
                } else {
                    throw std::logic_error("Invalid side to move");
                }
                break;
            }
        }
    }
};

int main() {
    initialise_all_databases();
    zobrist::initialise_zobrist_keys();

    Engine engine;
    for (;;) {
        engine.poll();
    }

    return 0;
}
