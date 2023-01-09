#include "surge/src/position.h"
#include "surge/src/types.h"

template <Color C>
MoveFlags generate_move_flags(const Position& pos, Square from, Square to);

template <Color C>
MoveFlags generate_attack_move_flags(const Position& pos, Square from, Square to);
