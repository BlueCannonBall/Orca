#include "surge/src/position.h"
#include "surge/src/types.h"
#include "uci.hpp"

class Engine: public uci::Engine {
protected:
    Position pos;

public:
    Engine() :
        uci::Engine("orca", "BlueCannonBall") { }
    
protected:
    void on_message(const std::string& command, const std::vector<std::string>& args) override {
        str_switch(command) {
            str_case("position") :
            {
                if (args[0] == "startpos") {
                    Position::set(DEFAULT_FEN, pos);
                    if (args.size() > 2) {
                        for (size_t i = 3; i < args.size(); i++) {
                            if ((i - 3) % 2 == 0) {
                                pos.play<WHITE>(Move(args[i]));
                            } else {
                                pos.play<BLACK>(Move(args[i]));
                            }
                        }
                    }
                } else if (args[0] == "fen") {
                    std::string fen;
                    for (unsigned short i = 2; i < args.size(); i++) {
                        fen += args[i];
                        if (i + 1 != args.size()) {
                            fen.push_back(' ');
                        }
                    }
                    Position::set(fen, pos);
                }
                break;
            }

            str_case("go") : {
                
            }
        }
    }
};

int main() {
    initialise_all_databases();
    zobrist::initialise_zobrist_keys();
}
