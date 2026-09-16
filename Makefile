BIN=arc++
CXXFLAGS=-Wall -O3 -c -std=gnu++17
LDFLAGS=-lm -lstdc++fs

$(BIN): main.o arc.o jit.o
	$(CXX) -o $(BIN) main.o arc.o jit.o $(LDFLAGS)

readline: CXXFLAGS+=-DREADLINE
readline: LDFLAGS+=-lreadline
readline: $(BIN)

mingw: CXXFLAGS=-Wall -O3 -c -std=gnu++17
mingw: LDFLAGS=-s -lm -lstdc++fs
mingw: main.o arc.o jit.o ico.o
	$(CXX) -o $(BIN) main.o arc.o jit.o ico.o $(LDFLAGS)

ico.o: arc.rc arc.ico
	windres -o ico.o -O coff arc.rc

main.o: main.cpp arc.h jit.h
	$(CXX) $(CXXFLAGS) main.cpp
arc.o: arc.cpp arc.h library.h jit.h
	$(CXX) $(CXXFLAGS) arc.cpp
jit.o: jit.cpp jit.h arc.h
	$(CXX) $(CXXFLAGS) jit.cpp
run: $(BIN)
	$(BIN)
test: $(BIN)
	$(BIN) tests.arc
	$(BIN) --no-jit tests.arc
	$(BIN) --no-vm tests.arc
clean:
	rm -f $(BIN) *.o
tag:
	etags *.h *.cpp
