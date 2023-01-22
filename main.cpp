#include "evaluation.hpp"
#include "logger.hpp"
#include "search.hpp"
#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include "uci.hpp"
#include "util.hpp"
#include <cmath>
#include <iostream>
#include <unordered_map>

class Engine: public uci::Engine {
protected:
    Position pos;
    RT rt;
    tp::ThreadPool pool;
    Logger logger;

public:
    Engine() :
        uci::Engine("orca", "BlueCannonBall"),
        pos(DEFAULT_FEN),
        logger("/tmp/orca.log") { }

protected:
    void declare_options() override {
        this->send_message("option", {"name", "UCI_Chess960", "type", "check", "default", "false"});
    }

    void on_message(const std::string& command, const std::vector<std::string>& args) override {
        std::string full_line;
        full_line += command;
        for (const auto& arg : args) {
            full_line += ' ' + arg;
        }
        logger.info("Got UCI command: " + full_line);

        str_switch(command) {
            str_case("position") :
            {
                rt.clear();
                if (args[0] == "startpos") {
                    pos = Position(DEFAULT_FEN);
                    rt[pos.get_hash()] = 1;
                    if (args.size() > 1) {
                        for (size_t i = 2; i < args.size(); i++) {
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

                            RT::iterator entry_it;
                            if ((entry_it = rt.find(pos.get_hash())) != rt.end()) {
                                entry_it->second++;
                            } else {
                                rt[pos.get_hash()] = 1;
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
                    rt[pos.get_hash()] = 1;
                    if (args.size() > 7) {
                        for (size_t i = 8; i < args.size(); i++) {
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

                            RT::iterator entry_it;
                            if ((entry_it = rt.find(pos.get_hash())) != rt.end()) {
                                entry_it->second++;
                            } else {
                                rt[pos.get_hash()] = 1;
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
                if ((float) pos.game_ply / 2.f < 60.f) {
                    moves_left = std::round(((-2.f / 3.f) * ((float) pos.game_ply / 2.f)) + 50.f);
                } else if ((float) pos.game_ply / 2.f >= 60.f) {
                    moves_left = std::round((0.1f * ((float) pos.game_ply / 2.f - 60.f)) + 10.f);
                }

                if (pos.turn() == WHITE) {
                    if (movetime != std::chrono::milliseconds(-1)) {
                        search_time = movetime;
                    } else if (wtime != std::chrono::milliseconds(-1)) {
                        search_time = std::chrono::milliseconds(std::min(wtime.count() / moves_left, 30000l));
                    }

                    if (search_time - std::chrono::milliseconds(500) < winc) {
                        search_time = winc - std::chrono::milliseconds(500);
                    }

                    go<WHITE>(this, pos, search_time, rt, pool);
                } else if (pos.turn() == BLACK) {
                    if (movetime != std::chrono::milliseconds(-1)) {
                        search_time = movetime;
                    } else if (btime != std::chrono::milliseconds(-1)) {
                        search_time = std::chrono::milliseconds(std::min(btime.count() / moves_left, 30000l));
                    }

                    if (search_time - std::chrono::milliseconds(500) < binc) {
                        search_time = binc - std::chrono::milliseconds(500);
                    }

                    go<BLACK>(this, pos, search_time, rt, pool);
                } else {
                    throw std::logic_error("Invalid side to move");
                }
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
