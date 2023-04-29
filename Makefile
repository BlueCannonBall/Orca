CXX = g++
CXXFLAGS = -Wall -std=c++14 -Iprophet-nnue/nnue/include -Ofast -flto -march=native -mtune=native -pthread
RUSTFLAGS = -C target-cpu=native
LDLIBS = -lboost_fiber -lboost_thread -lbz2
HEADERS = $(shell find . -name "*.h" -o -name "*.hpp")
OBJDIR = obj
OBJS = $(OBJDIR)/main.o $(OBJDIR)/position.o $(OBJDIR)/tables.o $(OBJDIR)/types.o $(OBJDIR)/util.o $(OBJDIR)/evaluation.o $(OBJDIR)/search.o
TARGET = orca
PREFIX = /usr/local

$(TARGET): $(OBJS) prophet-nnue/target/release/libprophet.a
	$(CXX) $^ $(CXXFLAGS) $(LDLIBS) -o $@

$(OBJDIR)/main.o: main.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -DORCA_TIMESTAMP=\"$(shell date --iso=seconds)\" "-DORCA_COMPILER=\"$(CXX) $(shell $(CXX) -dumpversion)\"" -o $@

prophet-nnue/target/release/libprophet.a: prophet-nnue/nnue/Cargo.toml $(shell find prophet-nnue/nnue/src -name "*.rs") prophet-nnue/nnue/nnue.npz
	cd prophet-nnue/nnue && RUSTFLAGS="$(RUSTFLAGS)" cargo build --release

$(OBJDIR)/position.o: surge/src/position.cpp surge/src/*.h
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/tables.o: surge/src/tables.cpp surge/src/*.h
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/types.o: surge/src/types.cpp surge/src/*.h
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/util.o: util.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/evaluation.o: evaluation.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/search.o: search.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

.PHONY: clean install

clean:
	rm -rf $(TARGET) $(OBJDIR)
	rm -rf prophet-nnue/target

install:
	cp $(TARGET) $(PREFIX)/bin/
