Examples=$(wildcard examples/**/*.stacky) $(wildcard examples/*.stacky)
Compiled_Examples=$(basename $(Examples))

CXX=g++
CXXFLAGS=-std=c++20 -Wall -Wextra -Werror=switch -Wno-parentheses

Objects=build/arguments.o \
				build/lexer.o \
				build/unicode.o \
				build/parser.o \
				build/linux-x86_64.o \
				build/optimizer.o \
				build/debug.o \
				build/types.o

.PHONY: all
all: stacky test $(Compiled_Examples)

# ------------ COMPILER COMPILATION ------------

build:
	mkdir -p build

stacky: src/stacky.cc $(Objects) src/stacky.hh src/errors.hh src/enum-names.cc
	$(CXX) $(CXXFLAGS) $< -o $@ -O3 -lboost_program_options -lfmt $(Objects)

build/%.o: src/%.cc src/stacky.hh src/errors.hh | build
	$(CXX) $(CXXFLAGS) $< -o $@ -c -O3

# ------------ C++ CODE GENERATION ------------

src/enum-names.cc: enum2string.sh src/stacky.hh
	./enum2string.sh src/stacky.hh > $@

# ------------ STACKY COMPILATION ------------

examples/%: examples/%.stacky stacky
	./stacky build $<

.PHONY: test
test: run-tests.sh stacky
	./$< all

# ------------ UTILITIES ------------

.PHONY: clean
clean:
	rm -f stacky run-tests tests/*.asm tests/*.o *.o *.asm *.svg *.dot
	rm -f examples/**/*.dot examples/**/*.svg examples/**/*.asm examples/**/*.o examples/*.asm examples/*.o
	rm -f std/*.asm std/*.o
	rm -f $(shell find tests examples std -type f -executable -not -name "*.stacky" -print)
	rm -f src/enum-names.cc
	rm -rf build

.PHONY: stat
stat:
	cloc --read-lang-def=etc/cloc-stacky-definition.txt --exclude-lang=Zig .

.PHONY: stat-cpp
stat-cpp:
	cloc --include-lang="C++,C/C++ Header" .

.PHONY: install-nvim
install-nvim: etc/stacky.vim
	cp $< /usr/share/nvim/runtime/syntax/
