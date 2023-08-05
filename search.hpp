#pragma once

#include "chess.hpp"
#include "nnue.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

enum TTEntryFlag : int8_t {
    TT_FLAG_NONE,
    TT_FLAG_EXACT,
    TT_FLAG_LOWERBOUND,
    TT_FLAG_UPPERBOUND,
};

class TTEntry {
public:
    chess::U64 hash = 0;
    int score = 0;
    int depth = 0;
    chess::Move best_move = chess::Move(0);
    TTEntryFlag flag = TT_FLAG_NONE;

    TTEntry() = default;
    TTEntry(chess::U64 hash, int score, int depth, const chess::Move& best_move, TTEntryFlag flag):
        hash(hash),
        score(score),
        depth(depth),
        best_move(best_move),
        flag(flag) {}
};

class TT {
protected:
    std::vector<TTEntry> entries;

public:
    TT(size_t size):
        entries(size) {}

    const TTEntry* probe(chess::U64 hash) const {
        const TTEntry* ret = &entries[hash % size()];
        if (ret->hash != hash) {
            return nullptr;
        }
        return ret;
    }

    TTEntry* probe(chess::U64 hash) {
        TTEntry* ret = &entries[hash % size()];
        if (ret->hash != hash) {
            return nullptr;
        }
        return ret;
    }

    void insert(const TTEntry& entry) {
        if (entry.depth >= entries[entry.hash % size()].depth) {
            entries[entry.hash % size()] = entry;
        }
    }

    void resize(size_t size) {
        entries.resize(size);
    }

    void clear() {
        std::fill(entries.begin(), entries.end(), TTEntry());
    }

    size_t size() const {
        return entries.size();
    }
};

typedef chess::Move KillerMoves[2][1024][3];

struct SearchRequest {
    nnue::Board board = nnue::Board(chess::STARTPOS);
    uint8_t multipv = 1;
    uint8_t threads = 1;
    uint16_t hash_size = 64;
    std::chrono::milliseconds time;
    int target_depth = -1;
    bool new_game = false;
    bool quit = false;
};

class SearchInfo {
public:
    const int starting_depth;
    const int starting_ply;
    int seldepth = 0;
    unsigned long long nodes = 0;

    SearchInfo(int starting_depth, int starting_ply):
        starting_depth(starting_depth),
        starting_ply(starting_ply) {}

    int current_ply(int full_move_number) const {
        return full_move_number - starting_ply;
    }
};

class SearchAgent {
public:
    TT* tt;
    KillerMoves killer_moves;
    int history_scores[chess::MAX_SQ][chess::MAX_SQ];

    SearchAgent(TT* tt):
        tt(tt) {
        memset(killer_moves, 0, sizeof killer_moves);
        memset(history_scores, 0, sizeof history_scores);
    }

    int search(nnue::Board& board, int alpha, int beta, SearchInfo& info, std::function<bool(int)> is_stopping) {
        int score = alpha_beta(board, alpha, beta, info.starting_depth - 1, info, is_stopping);
        return score;
    }

protected:
    int alpha_beta(nnue::Board& board, int alpha, int beta, int depth, SearchInfo& info, std::function<bool(int)> is_stopping, bool do_null_move = true, bool do_lmr = true);
    int quiesce(nnue::Board& board, int alpha, int beta, int depth, SearchInfo& info, std::function<bool(int)> is_stopping);

    void add_killer_move(const chess::Move& move, chess::Color color, int ply);
    bool is_killer_move(const chess::Move& move, chess::Color color, int ply) const;

    int get_history_score(const chess::Move& move) const;
    void update_history_score(const chess::Move& move, int depth);
};

std::vector<chess::Move> get_pv(chess::Board board, const TT& tt);
