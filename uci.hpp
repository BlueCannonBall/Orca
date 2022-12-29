#pragma once

#include "surge/src/position.h"
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <string>
#include <vector>

#define str_switch(str) switch (uci::detail::hash(str))
#define str_case(str)   case uci::detail::hash(str)

namespace uci {
    namespace detail {
        unsigned int hash(const std::string& s, int pos = 0) { // NOLINT
            return !s[pos] ? 5381 : (hash(s, pos + 1) * 33) ^ s[pos];
        }

        constexpr unsigned int hash(const char* s, int pos = 0) {
            return !s[pos] ? 5381 : (hash(s, pos + 1) * 33) ^ s[pos];
        }
    } // namespace detail

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

        void move(Square from, Square to, bool promote = false) {
            move(Move(from, to), promote);
        }

        void move(Move move, bool promote = false) {
            std::cout << "bestmove " << SQSTR[move.from()] << SQSTR[move.to()];
            if (promote) {
                std::cout << "q" << std::endl;
            } else {
                std::cout << std::endl;
            }
        }

        virtual ~Engine() { }

    protected:
        virtual void declare_options() { }
        virtual void on_message(const std::string& command, const std::vector<std::string>& args) = 0;
    };
} // namespace uci
