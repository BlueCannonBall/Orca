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

    struct PollResult {
        std::string command;
        std::vector<std::string> args;
    };

    inline PollResult poll() {
        std::string line;
        std::getline(std::cin, line);
        boost::trim(line);

        logger.info("Got UCI message: " + line);

        std::vector<std::string> line_split;
        boost::split(line_split, line, isspace);

        std::string command = line_split[0];
        line_split.erase(line_split.begin());

        return PollResult {
            .command = command,
            .args = line_split,
        };
    }

    inline void send_message(const std::string& command, const std::vector<std::string>& args = {}) {
        std::ostringstream ss;
        ss << command;
        if (!args.empty()) {
            ss << ' ';
            for (size_t i = 0; i < args.size(); i++) {
                ss << args[i];
                if (i != args.size() - 1) {
                    ss << ' ';
                }
            }
        }
        logger.info("Sent UCI command: " + ss.str());
        std::cout << ss.str() << std::endl;
    }

    inline void bestmove(Move best_move, Move ponder_move = Move()) {
        if (ponder_move.is_null()) {
            send_message("bestmove", {format_move(best_move)});
        } else {
            send_message("bestmove", {format_move(best_move), "ponder", format_move(ponder_move)});
        }
    }
} // namespace uci
