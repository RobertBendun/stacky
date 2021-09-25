Examples=$(wildcard examples/*.stacky)
Compiled_Examples=$(basename $(Examples))

Compiler=g++
Options=-std=c++20 -Wall -Wextra -Werror=switch

.PHONY:
all: stacky run-tests $(Compiled_Examples)

# ------------ COMPILER COMPILATION ------------

stacky: src/stacky.cc src/utilities.cc src/errors.cc src/stdlib-symbols.cc stdlib.o src/enum-names.cc src/arguments.cc src/parser.cc src/lexer.cc
	$(Compiler) $(Options) $< -o $@ -O3 -lboost_program_options

run-tests: src/run-tests.cc src/errors.cc src/utilities.cc src/ipstream.hh
	$(Compiler) $(Options) $< -o $@ -O3

stdlib.o: stdlib.cc
	$(Compiler) -nostdlib -c  $< -o $@ $(Options) -fno-rtti -fno-exceptions -fno-stack-protector -ggdb -O0

# ------------ C++ CODE GENERATION ------------

src/stdlib-symbols.cc: stdlib-symbols.sh stdlib.cc
	 ./stdlib-symbols.sh > src/stdlib-symbols.cc

src/enum-names.cc: enum2string.sh src/stacky.cc
	./enum2string.sh src/stacky.cc > src/enum-names.cc

# ------------ STACKY COMPILATION ------------

examples/%: examples/%.stacky stacky stdlib.o
	./stacky build $<


.PHONY: test
test: run-tests stacky stdlib.o
	./$<

# ------------ UTILITIES ------------

.PHONY: clean
clean:
	rm -f stacky run-tests tests/*.asm tests/*.o examples/*.asm examples/*.o *.o *.asm
	rm -f $(shell find tests examples -type f -executable -not -name "*.stacky" -print)
	rm -f src/stdlib-symbols.cc src/enum-names.cc

.PHONY: stat
stat:
	cloc --read-lang-def=etc/cloc-stacky-definition.txt --exclude-lang=Zig .

.PHONY: install-nvim
install-nvim: etc/stacky.vim
	cp $< /usr/share/nvim/runtime/syntax/
