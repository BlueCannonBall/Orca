#pragma once

#include "surge/src/position.h"
#include "surge/src/types.h"

template <Color Us>
int evaluate(const Position& pos);

template <Color Us>
int see(const Position& pos, Square sq);
