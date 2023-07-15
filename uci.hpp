#pragma once

#include "chess.hpp"
#include "util.hpp"
#include <iostream>
#include <string>
#include <vector>

#define str_switch(str) switch (uci::detail::hash(str))
#define str_case(str)   case uci::detail::hash(str)

namespace uci {
    namespace detail {
        inline unsigned int hash(const std::string& s, size_t i = 0) {
            return !s[i] ? 5381 : (hash(s, i + 1) * 33) ^ s[i];
        }

        constexpr unsigned int hash(const char* s, size_t i = 0) {
            return !s[i] ? 5381 : (hash(s, i + 1) * 33) ^ s[i];
        }
    } // namespace detail

    struct PollResult {
        std::string command;
        std::vector<std::string> args;
    };

    inline PollResult poll() {
        std::string line;
        std::getline(std::cin, line);
        chess::utils::trim(line);

        logger.debug("Got UCI message: " + line);

        std::vector<std::string> line_split = chess::utils::splitString(line, ' ');

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
            for (size_t i = 0; i < args.size(); ++i) {
                ss << args[i];
                if (i != args.size() - 1) {
                    ss << ' ';
                }
            }
        }
        logger.debug("Sent UCI command: " + ss.str());
        std::cout << ss.str() << std::endl;
    }

    inline void bestmove(const chess::Move& best_move) {
        send_message("bestmove", {chess::uci::moveToUci(best_move)});
    }

    inline void bestmove(const chess::Move& best_move, const chess::Move& ponder_move) {
        send_message("bestmove", {chess::uci::moveToUci(best_move), "ponder", chess::uci::moveToUci(ponder_move)});
    }
} // namespace uci
