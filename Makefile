stacky: stacky.cc stdlib.o stdlib-symbols.cc
	g++ -std=c++20 -Wall -Wextra -Werror=switch $< -o $@

stdlib.o: stdlib.cc
	g++ -nostdlib -c $< -o $@ -Wall -Wextra -fno-rtti -fno-exceptions -fno-stack-protector

stdlib-symbols.cc: gen-stdlib-symbols stdlib.cc
	 ./gen-stdlib-symbols > stdlib-symbols.cc

.PHONY: run
run: stacky
	./stacky 03-loops.stacky

.PHONY: clean
clean:
	rm -f stacky 01-hello 02-conditions 03-loops 04-fizzbuzz *.asm *.o stdlib-symbols.cc
