#pragma once

#include "evaluation.hpp"
#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include "uci.hpp"
#include <boost/atomic.hpp>
#include <chrono>
#include <prophet.h>
#include <unordered_map>
#include <vector>

enum TTEntryFlag {
    EXACT,
    LOWERBOUND,
    UPPERBOUND,
};

class TTEntry {
public:
    int score;
    int depth;
    Move best_move;
    TTEntryFlag flag;

    TTEntry() = default;
    TTEntry(int score, int depth, Move best_move, TTEntryFlag flag):
        score(score),
        depth(depth),
        best_move(best_move),
        flag(flag) {}
};

typedef std::unordered_map<uint64_t, TTEntry> TT;
typedef Move KillerMoves[NCOLORS][NHISTORY][3];
typedef std::unordered_map<uint64_t, unsigned short> RT;

struct Search {
    Position pos;
    RT rt;
    std::chrono::milliseconds time;
    int target_depth = -1;
    bool new_game = false;
    bool quit = false;
};

class Finder {
public:
    std::chrono::steady_clock::time_point start_time;
    Search search;
    int starting_depth;
    unsigned long long nodes = 0;
    const boost::atomic<bool>& stop;
    TT* tt;
    KillerMoves killer_moves;
    int history_scores[NSQUARES][NSQUARES];

    Finder(std::chrono::steady_clock::time_point start_time, const Search& search, const boost::atomic<bool>& stop):
        start_time(start_time),
        search(search),
        stop(stop),
        starting_ply(search.pos.game_ply) {
        memset(this->killer_moves, 0, sizeof(this->killer_moves));
        memset(this->history_scores, 0, sizeof(this->history_scores));
    }

    inline void raise_prophet(const char* net_path = nullptr) {
        accept_prophet(::raise_prophet(net_path));
    }

    void accept_prophet(Prophet* prophet) {
        this->search.pos.data = prophet;
        prophet_reset(prophet);
        prophet_activate_all(prophet, generate_prophet_board(this->search.pos));
        this->search.pos.activate_piece_hook = [](Piece piece, Square sq, void* data) {
            auto prophet = (Prophet*) data;
            prophet_activate(prophet, type_of(piece), color_of(piece), sq);
        };
        this->search.pos.deactivate_piece_hook = [](Piece piece, Square sq, void* data) {
            auto prophet = (Prophet*) data;
            prophet_deactivate(prophet, type_of(piece), color_of(piece), sq);
        };
    }

    template <Color Us>
    int alpha_beta(int alpha, int beta, int depth, bool do_null_move = true);

    template <Color Us>
    int quiesce(int alpha, int beta, int depth);

    bool is_stopping() const {
        return starting_depth > 1 && (std::chrono::steady_clock::now() - start_time > search.time || stop.load(boost::memory_order_relaxed));
    }

    int current_ply() const {
        return search.pos.game_ply - starting_ply;
    }

    int current_ply(int game_ply) const {
        return game_ply - starting_ply;
    }

protected:
    int starting_ply;

    template <Color C>
    void add_killer_move(Move move, int ply);

    template <Color Us>
    bool is_killer_move(Move move, int ply) const;

    int get_history_score(Move move) const;
    void update_history_score(Move move, int depth);
};

std::vector<Move> get_pv(Position pos, const TT* tt);
