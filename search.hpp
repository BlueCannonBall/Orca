#pragma once

#include "evaluation.hpp"
#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include "uci.hpp"
#include <atomic>
#include <chrono>
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
    TTEntry(int score, int depth, Move best_move, TTEntryFlag flag) :
        score(score),
        depth(depth),
        best_move(best_move),
        flag(flag) { }
};

typedef std::unordered_map<uint64_t, TTEntry> TT;
typedef Move KillerMoves[NCOLORS][NHISTORY][3];
typedef std::unordered_map<uint64_t, unsigned short> RT;

struct Search {
    Position pos;
    RT rt;
    std::chrono::milliseconds time;
    int target_depth = -1;
    bool quit = false;
};

class Finder {
public:
    std::chrono::steady_clock::time_point start_time;
    Search search;
    int starting_depth;
    unsigned long long nodes = 0;
    int last_score;
    const std::atomic<bool>& stop;
    TT tt;
    KillerMoves killer_moves;

    Finder(std::chrono::steady_clock::time_point start_time, const Search& search, const std::atomic<bool>& stop) :
        start_time(start_time),
        search(search),
        stop(stop) { }

    template <Color Us>
    int alpha_beta(int alpha, int beta, int depth);

    template <Color Us>
    int quiesce(int alpha, int beta, int depth);

    bool is_stopping() const {
        return starting_depth > 1 && (std::chrono::steady_clock::now() - start_time > search.time || stop.load(std::memory_order_relaxed));
    }

protected:
    template <Color C>
    void add_killer_move(Move move, int depth);

    template <Color Us>
    bool is_killer_move(Move move, int depth) const;
};

std::vector<Move> get_pv(Position pos, const TT& tt);
