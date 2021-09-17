Examples=$(wildcard examples/*.stacky)
Compiled_Examples=$(basename $(Examples))

.PHONY:
all: $(Compiled_Examples) stacky

stacky: stacky.cc utilities.cc errors.cc stdlib-symbols.cc stdlib.o
	g++ -std=c++20 -Wall -Wextra -Werror=switch $< -o $@

stdlib.o: stdlib.cc
	g++ -nostdlib -c -std=c++20 $< -o $@ -Wall -Wextra -fno-rtti -fno-exceptions -fno-stack-protector

stdlib-symbols.cc: gen-stdlib-symbols stdlib.cc
	 ./gen-stdlib-symbols > stdlib-symbols.cc

examples/%: examples/%.stacky stacky stdlib.o
	./stacky $<

.PHONY: clean
clean:
	rm -f stacky $(Compiled_Examples) examples/*.asm examples/*.o stdlib-symbols.cc
