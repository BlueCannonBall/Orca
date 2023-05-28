![logo banner](https://raw.githubusercontent.com/BlueCannonBall/Orca/main/banner.svg)

# Orca Chess NNUE
Orca is a C++14 UCI-compliant chess engine utilizing threading, alpha beta pruning, magic bitboards, principle variation search, quiescence search, check extensions, mate distance pruning, reverse futility pruning, delta pruning, a transposition table using zobrist hashing, late move reduction, hash move ordering, SEE (static exchange evaluation) move ordering, MVV-LVA move ordering, killer move heuristic, history heuristic, and a positional evaluation function with 10+ unique evaluation heuristics along with an NNUE evaluation function.

## Compilation
Download the Boost C++ libraries and Rust and then compile using make.
```
$ make
```

## Installation
```
$ sudo make install
```
