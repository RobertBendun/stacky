Examples=$(wildcard examples/**/*.stacky) $(wildcard examples/*.stacky)
Compiled_Examples=$(basename $(Examples))

Compiler=g++
Options=-std=c++20 -Wall -Wextra -Werror=switch -Wno-parentheses

Compiler_Sources=src/stacky.cc \
								 src/utilities.cc \
								 src/errors.cc \
								 src/enum-names.cc \
								 src/arguments.cc \
								 src/parser.cc \
								 src/lexer.cc \
								 src/linux-x86_64.cc \
								 src/optimizer.cc

.PHONY:
all: stacky test $(Compiled_Examples)

# ------------ COMPILER COMPILATION ------------

stacky: $(Compiler_Sources)
	$(Compiler) $(Options) $< -o $@ -O3 -lboost_program_options -lfmt

run-tests: src/run-tests.cc src/errors.cc src/utilities.cc src/ipstream.hh
	$(Compiler) $(Options) $< -o $@ -O3 -lfmt

# ------------ C++ CODE GENERATION ------------

src/enum-names.cc: enum2string.sh src/stacky.cc
	./enum2string.sh src/stacky.cc > src/enum-names.cc

# ------------ STACKY COMPILATION ------------

examples/%: examples/%.stacky stacky
	./stacky build $<

.PHONY: test
test: run-tests stacky
	./$<

# ------------ UTILITIES ------------

.PHONY: clean
clean:
	rm -f stacky run-tests tests/*.asm tests/*.o *.o *.asm
	rm -f examples/**/*.asm examples/**/*.o examples/*.asm examples/*.o
	rm -f std/*.asm std/*.o
	rm -f $(shell find tests examples std -type f -executable -not -name "*.stacky" -print)
	rm -f src/enum-names.cc

.PHONY: stat
stat:
	cloc --read-lang-def=etc/cloc-stacky-definition.txt --exclude-lang=Zig .

.PHONY: stat-cpp
stat-cpp:
	cloc --include-lang="C++,C/C++ Header" .

.PHONY: install-nvim
install-nvim: etc/stacky.vim
	cp $< /usr/share/nvim/runtime/syntax/
