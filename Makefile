Examples=$(wildcard examples/*.stacky)
Compiled_Examples=$(basename $(Examples))

Compiler=g++
Options=-std=c++20 -Wall -Wextra -Werror=switch

.PHONY:
all: stacky run-tests

stacky: stacky.cc utilities.cc errors.cc stdlib-symbols.cc stdlib.o
	$(Compiler) $(Options) $< -o $@

run-tests: run-tests.cc errors.cc utilities.cc ipstream.hh
	$(Compiler) $(Options) $< -o $@

stdlib.o: stdlib.cc
	$(Compiler) -nostdlib -c  $< -o $@ $(Options) -fno-rtti -fno-exceptions -fno-stack-protector

stdlib-symbols.cc: gen-stdlib-symbols stdlib.cc
	 ./gen-stdlib-symbols > stdlib-symbols.cc

examples/%: examples/%.stacky stacky stdlib.o
	./stacky $<

.PHONY: clean
clean:
	rm -f stacky run-tests tests/*.asm tests/*.o examples/*.asm examples/*.o stdlib-symbols.cc \
		$(shell find tests examples -type f -executable -not -name "*.stacky" -print)

.PHONY: test
test: run-tests
	./$< --quiet
