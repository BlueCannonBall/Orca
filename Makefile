CXX = g++-10
CXXFLAGS = -Wall -std=c++20 -g -pthread
HEADERS = $(shell find . -name "*.h" -o -name "*.hpp")
OBJDIR = obj
OBJS = $(OBJDIR)/main.o $(OBJDIR)/position.o $(OBJDIR)/tables.o $(OBJDIR)/types.o $(OBJDIR)/search.o
TARGET = orca

$(TARGET): $(OBJS)
	$(CXX) $^ $(CXXFLAGS) -o $@

$(OBJDIR)/main.o: main.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/position.o: surge/src/position.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/tables.o: surge/src/tables.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/types.o: surge/src/types.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@

$(OBJDIR)/search.o: search.cpp $(HEADERS)
	mkdir -p $(OBJDIR)
	$(CXX) -c $< $(CXXFLAGS) -o $@
