#include "surge/src/position.h"
#include "surge/src/types.h"
#include "threadpool.hpp"
#include <unordered_map>

class TranspositionCompound;

typedef std::unordered_map<uint64_t, TranspositionCompound> TranspositionTable;

enum GameProgress {
    MIDGAME,
    ENDGAME
};

constexpr int piece_values[NPIECE_TYPES] = {
    100,    // PAWN
    300,    // KNIGHT
    300,    // BISHOP
    500,    // ROOK
    900,    // QUEEN
    2000000 // KING
};

GameProgress get_progress(int mv1, int mv2);

template <Color C>
int evaluate(const Position& pos);

template <Color Us>
int negamax(Position& pos, int alpha, int beta, unsigned int depth);

template <Color Us>
Move find_best_move(Position& pos, unsigned int depth, tp::ThreadPool& pool, int* score);
