CXX = g++
CXXFLAGS = -Wall -std=c++17 -Iprophet-nnue/nnue/include -Ofast -flto -march=native -mtune=native -pthread
RUSTFLAGS = -C target-cpu=native
LDLIBS = -lboost_thread -lboost_fiber -ldl -lbz2
HEADERS = $(shell find . -name "*.h" -o -name "*.hpp")
OBJDIR = obj
OBJS = $(OBJDIR)/main.o $(OBJDIR)/util.o $(OBJDIR)/evaluation.o $(OBJDIR)/search.o $(OBJDIR)/nnue.o
TARGET = orca
PREFIX = /usr/local

$(TARGET): $(OBJS) prophet-nnue/target/release/libprophet.a
	$(CXX) $^ $(CXXFLAGS) $(LDLIBS) -o $@

$(OBJDIR)/main.o: main.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -DORCA_TIMESTAMP=\"$(shell date --iso=seconds)\" "-DORCA_COMPILER=\"$(CXX) $(shell $(CXX) -dumpversion)\"" -o $@

prophet-nnue/target/release/libprophet.a: prophet-nnue/nnue/Cargo.toml $(shell find prophet-nnue/nnue/src -name "*.rs") prophet-nnue/nnue/nnue.npz
	cd prophet-nnue/nnue && RUSTFLAGS="$(RUSTFLAGS)" cargo build --release

$(OBJDIR)/util.o: util.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/evaluation.o: evaluation.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/search.o: search.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/nnue.o: nnue.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

.PHONY: clean install age

clean:
	rm -rf $(TARGET) $(OBJDIR)
	rm -rf prophet-nnue/target

install:
	cp $(TARGET) $(PREFIX)/bin/

age:
	mv $(TARGET) $(TARGET)_old
