#include "evaluation.hpp"
#include "search.hpp"
#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include "uci.hpp"
#include "util.hpp"
#include <iostream>

class Engine: public uci::Engine {
protected:
    Position pos;
    int plies_made = 0;
    tp::ThreadPool pool;

public:
    Engine() :
        uci::Engine("orca", "BlueCannonBall"),
        pos(DEFAULT_FEN) { }

protected:
    void declare_options() override {
        this->send_message("option", {"name", "UCI_Chess960", "type", "check", "default", "false"});
    }

    void on_message(const std::string& command, const std::vector<std::string>& args) override {
        str_switch(command) {
            str_case("position") :
            {
                plies_made = 0;
                if (args[0] == "startpos") {
                    pos = Position(DEFAULT_FEN);
                    if (args.size() > 1) {
                        for (size_t i = 2; i < args.size(); i++) {
                            plies_made++;
                            Square from = create_square(File(args[i][0] - 'a'), Rank(args[i][1] - '1'));
                            Square to = create_square(File(args[i][2] - 'a'), Rank(args[i][3] - '1'));
                            if (((i - 2) % 2) == 0) {
                                MoveFlags m_flags = generate_move_flags<WHITE>(pos, from, to);
                                if (m_flags == PROMOTIONS) {
                                    switch (args[i][4]) {
                                        case 'n':
                                            m_flags = PR_KNIGHT;
                                            break;
                                        case 'b':
                                            m_flags = PR_BISHOP;
                                            break;
                                        case 'r':
                                            m_flags = PR_ROOK;
                                            break;
                                        case 'q':
                                            m_flags = PR_QUEEN;
                                            break;
                                    }
                                } else if (m_flags == PROMOTION_CAPTURES) {
                                    switch (args[i][4]) {
                                        case 'n':
                                            m_flags = PC_KNIGHT;
                                            break;
                                        case 'b':
                                            m_flags = PC_BISHOP;
                                            break;
                                        case 'r':
                                            m_flags = PC_ROOK;
                                            break;
                                        case 'q':
                                            m_flags = PC_QUEEN;
                                            break;
                                    }
                                }
                                pos.play<WHITE>(Move(from, to, m_flags));
                            } else {
                                MoveFlags m_flags = generate_move_flags<BLACK>(pos, from, to);
                                if (m_flags == PROMOTIONS) {
                                    switch (args[i][4]) {
                                        case 'n':
                                            m_flags = PR_KNIGHT;
                                            break;
                                        case 'b':
                                            m_flags = PR_BISHOP;
                                            break;
                                        case 'r':
                                            m_flags = PR_ROOK;
                                            break;
                                        case 'q':
                                            m_flags = PR_QUEEN;
                                            break;
                                    }
                                } else if (m_flags == PROMOTION_CAPTURES) {
                                    switch (args[i][4]) {
                                        case 'n':
                                            m_flags = PC_KNIGHT;
                                            break;
                                        case 'b':
                                            m_flags = PC_BISHOP;
                                            break;
                                        case 'r':
                                            m_flags = PC_ROOK;
                                            break;
                                        case 'q':
                                            m_flags = PC_QUEEN;
                                            break;
                                    }
                                }
                                pos.play<BLACK>(Move(from, to, m_flags));
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
                    if (args.size() > 7) {
                        for (size_t i = 8; i < args.size(); i++) {
                            plies_made++;
                            Square from = create_square(File(args[i][0] - 'a'), Rank(args[i][1] - '1'));
                            Square to = create_square(File(args[i][2] - 'a'), Rank(args[i][3] - '1'));
                            if (((i - 8) % 2) == 0) {
                                MoveFlags m_flags = generate_move_flags<WHITE>(pos, from, to);
                                if (m_flags == PROMOTIONS) {
                                    switch (args[i][4]) {
                                        case 'n':
                                            m_flags = PR_KNIGHT;
                                            break;
                                        case 'b':
                                            m_flags = PR_BISHOP;
                                            break;
                                        case 'r':
                                            m_flags = PR_ROOK;
                                            break;
                                        case 'q':
                                            m_flags = PR_QUEEN;
                                            break;
                                    }
                                } else if (m_flags == PROMOTION_CAPTURES) {
                                    switch (args[i][4]) {
                                        case 'n':
                                            m_flags = PC_KNIGHT;
                                            break;
                                        case 'b':
                                            m_flags = PC_BISHOP;
                                            break;
                                        case 'r':
                                            m_flags = PC_ROOK;
                                            break;
                                        case 'q':
                                            m_flags = PC_QUEEN;
                                            break;
                                    }
                                }
                                pos.play<WHITE>(Move(from, to, m_flags));
                            } else {
                                MoveFlags m_flags = generate_move_flags<BLACK>(pos, from, to);
                                if (m_flags == PROMOTIONS) {
                                    switch (args[i][4]) {
                                        case 'n':
                                            m_flags = PR_KNIGHT;
                                            break;
                                        case 'b':
                                            m_flags = PR_BISHOP;
                                            break;
                                        case 'r':
                                            m_flags = PR_ROOK;
                                            break;
                                        case 'q':
                                            m_flags = PR_QUEEN;
                                            break;
                                    }
                                } else if (m_flags == PROMOTION_CAPTURES) {
                                    switch (args[i][4]) {
                                        case 'n':
                                            m_flags = PC_KNIGHT;
                                            break;
                                        case 'b':
                                            m_flags = PC_BISHOP;
                                            break;
                                        case 'r':
                                            m_flags = PC_ROOK;
                                            break;
                                        case 'q':
                                            m_flags = PC_QUEEN;
                                            break;
                                    }
                                }
                                pos.play<BLACK>(Move(from, to, m_flags));
                            }
                        }
                    }
                }
                break;
            }

            str_case("go") :
            {
                std::chrono::milliseconds movetime = std::chrono::milliseconds(-1);
                std::chrono::milliseconds wtime = std::chrono::milliseconds(-1);
                std::chrono::milliseconds btime = std::chrono::milliseconds(-1);
                std::chrono::milliseconds winc = std::chrono::milliseconds(0);
                std::chrono::milliseconds binc = std::chrono::milliseconds(0);
                for (auto it = args.begin(); it != args.end(); it++) {
                    if (*it == "movetime") {
                        movetime = std::chrono::milliseconds(stoi(*++it));
                    } else if (*it == "wtime") {
                        wtime = std::chrono::milliseconds(stoi(*++it));
                    } else if (*it == "btime") {
                        btime = std::chrono::milliseconds(stoi(*++it));
                    } else if (*it == "winc") {
                        winc = std::chrono::milliseconds(stoi(*++it));
                    } else if (*it == "binc") {
                        binc = std::chrono::milliseconds(stoi(*++it));
                    }
                }

                std::chrono::milliseconds search_time = std::chrono::seconds(10);
                int moves_left;
                if (plies_made / 2 < 60) {
                    moves_left = ((-2 / 3) * (plies_made / 2)) + 50;
                } else if (plies_made / 2 >= 60) {
                    moves_left = (0.1 * ((float) plies_made / 2 - 60)) + 10;
                }

                Move best_move;
                if (pos.turn() == WHITE) {
                    if (movetime != std::chrono::milliseconds(-1)) {
                        search_time = movetime;
                    } else if (wtime != std::chrono::milliseconds(-1)) {
                        search_time = std::chrono::milliseconds(wtime.count() / moves_left);
                    }

                    if (search_time - std::chrono::milliseconds(500) < winc) {
                        search_time = winc - std::chrono::milliseconds(500);
                    }

                    best_move = find_best_move<WHITE>(this, pos, search_time, pool);
                } else if (pos.turn() == BLACK) {
                    if (movetime != std::chrono::milliseconds(-1)) {
                        search_time = movetime;
                    } else if (btime != std::chrono::milliseconds(-1)) {
                        search_time = std::chrono::milliseconds(btime.count() / moves_left);
                    }

                    if (search_time - std::chrono::milliseconds(500) < binc) {
                        search_time = binc - std::chrono::milliseconds(500);
                    }

                    best_move = find_best_move<BLACK>(this, pos, search_time, pool);
                } else {
                    throw std::logic_error("Invalid side to move");
                }
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

            str_case("eval") :
                str_case("evaluate") :
            {
                if (pos.turn() == WHITE) {
                    this->send_message("evaluation", {std::to_string(evaluate<WHITE>(pos, true))});
                } else if (pos.turn() == BLACK) {
                    this->send_message("evaluation", {std::to_string(evaluate<BLACK>(pos, true))});
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

    // Position pos("rnbqkb1r/ppp2ppp/4pn2/3p4/8/2N5/PPPPPPPP/R1BQKBNR w KQkq - 0 1");
    // std::cout << see(pos, Move(c3, d5, CAPTURE), -100) << std::endl;

    return 0;
}
