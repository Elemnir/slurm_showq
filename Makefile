CXX=g++
CXXFLAGS=--std=c++11
INCLUDE=-Iinclude
OBJ=-lslurm
PROG=showq

all: prog

debug: main.o
	$(CXX) $(CXXFLAGS) -g -o $(PROG) main.o $(OBJ)

prog: main.o
	$(CXX) $(CXXFLAGS) -o $(PROG) main.o $(OBJ)

main.o: main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c main.cpp

clean:
	rm -rf *.o $(PROG)
