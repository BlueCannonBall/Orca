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
                Move best_move;
                int best_move_score;
                if (pos.turn() == WHITE) {
                    best_move = find_best_move<WHITE>(pos, 6, pool, &best_move_score);
                } else if (pos.turn() == BLACK) {
                    best_move = find_best_move<BLACK>(pos, 6, pool, &best_move_score);
                } else {
                    throw std::logic_error("Invalid side to move");
                }
                this->send_message("info", {"score", "cp", std::to_string(best_move_score)});
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
