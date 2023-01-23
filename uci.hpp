#pragma once

#include "surge/src/position.h"
#include "surge/src/types.h"
#include "util.hpp"
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <string>
#include <vector>

#define str_switch(str) switch (uci::detail::hash(str))
#define str_case(str)   case uci::detail::hash(str)

namespace uci {
    namespace detail {
        inline unsigned int hash(const std::string& s, int pos = 0) {
            return !s[pos] ? 5381 : (hash(s, pos + 1) * 33) ^ s[pos];
        }

        constexpr unsigned int hash(const char* s, int pos = 0) {
            return !s[pos] ? 5381 : (hash(s, pos + 1) * 33) ^ s[pos];
        }
    } // namespace detail

    inline std::string format_move(Move move) {
        std::ostringstream ss;
        ss << SQSTR[move.from()] << SQSTR[move.to()];
        if (move.is_promotion()) {
            switch (move.promotion()) {
                case KNIGHT:
                    ss << 'n';
                    break;
                case BISHOP:
                    ss << 'b';
                    break;
                case ROOK:
                    ss << 'r';
                    break;
                case QUEEN:
                    ss << 'q';
                    break;

                default:
                    throw std::logic_error("Invalid promotion");
            }
        }
        return ss.str();
    }

    template <Color C>
    inline Move parse_move(const Position& pos, const std::string& str) {
        Square from = create_square(File(str[0] - 'a'), Rank(str[1] - '1'));
        Square to = create_square(File(str[2] - 'a'), Rank(str[3] - '1'));
        MoveFlags flags = generate_move_flags<C>(pos, from, to);
        if (flags == PROMOTIONS) {
            switch (str[4]) {
                case 'n':
                    flags = PR_KNIGHT;
                    break;
                case 'b':
                    flags = PR_BISHOP;
                    break;
                case 'r':
                    flags = PR_ROOK;
                    break;
                case 'q':
                    flags = PR_QUEEN;
                    break;
            }
        } else if (flags == PROMOTION_CAPTURES) {
            switch (str[4]) {
                case 'n':
                    flags = PC_KNIGHT;
                    break;
                case 'b':
                    flags = PC_BISHOP;
                    break;
                case 'r':
                    flags = PC_ROOK;
                    break;
                case 'q':
                    flags = PC_QUEEN;
                    break;
            }
        }
        return Move(from, to, flags);
    }

    class Engine {
    private:
        std::string name;
        std::string author;

        void _on_message(const std::string& command, const std::vector<std::string>& args) {
            str_switch(command) {
                str_case("uci") :
                {
                    std::cout << "id name " << name << std::endl;
                    std::cout << "id author " << author << std::endl;
                    this->declare_options();
                    std::cout << "uciok" << std::endl;
                    break;
                }

                str_case("isready") :
                {
                    std::cout << "readyok\n";
                    break;
                }

                default: {
                    on_message(command, args);
                    break;
                }
            }
        }

    public:
        Engine(
            const std::string& name, const std::string& author) :
            name(name),
            author(author) {
            std::cout.setf(std::ios::unitbuf);
            std::cin.setf(std::ios::unitbuf);
        }

        void poll() {
            std::string line;
            std::getline(std::cin, line);

            std::vector<std::string> line_split;
            boost::split(line_split, line, isspace);

            std::string command = line_split[0];
            line_split.erase(line_split.begin());
            this->_on_message(command, line_split);
        }

        void send_message(const std::string& command, const std::vector<std::string>& args) {
            std::cout << command << ' ';
            for (size_t i = 0; i < args.size(); i++) {
                std::cout << args[i];
                if (i < args.size() - 1) {
                    std::cout << ' ';
                } else {
                    std::cout << std::endl;
                }
            }
        }

        void move(Move best_move, Move ponder_move = Move()) {
            if (ponder_move.is_null()) {
                this->send_message("bestmove", {format_move(best_move)});
            } else {
                this->send_message("bestmove", {format_move(best_move), "ponder", format_move(ponder_move)});
            }
        }

        virtual ~Engine() { }

    protected:
        virtual void declare_options() { }
        virtual void on_message(const std::string& command, const std::vector<std::string>& args) = 0;
    };
} // namespace uci
