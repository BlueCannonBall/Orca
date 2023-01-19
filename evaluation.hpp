#pragma once

#include "surge/src/position.h"
#include "surge/src/types.h"

template <Color Us>
int evaluate(const Position& pos, bool debug = false);

template <Color Us>
int see(const Position& pos, Move move);
