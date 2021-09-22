# stacky

**WIP** stack-based compiled programming language with purpose of beeing a learning expirience. **Currently Linux-only**

## Example
Program printing "HELLO" to stdout, that was transformed from "hello"

```
println fun
	1 1 syscall3
	nl
end

5 hello-count constant
hello-count hello []byte

0 while dup hello-count < do
	dup dup "hello" + read8 32 -
	swap hello + swap write8
	1 +
end

hello println
```

## Makefile
- `make install-nvim` - installs Stacky's syntax highlighting for Neovim
- `make stacky` - makes only compiler
- `make test` - runs all tests
- `make clean` - cleans all intermidiate files

## Requirements
- [NASM](https://nasm.us/)
- [LD](https://linux.die.net/man/1/ld)
- [GCC with C++ compiler](https://gcc.gnu.org/) for compiler and stdlib compilation
- [Boost Program\_Options](https://www.boost.org/)

For Arch-based users:
```shell
pacman -S gcc binutils nasm boost
```

## Language reference

###  Comments
Comments starts with `#` and ends at `\n`. Only single-line comments are supported.

### Conditions

`if` jumps to `end` or `else` if 0, otherwise continues execution and on `else` jumps to `end`. For example code below prints 10:

```
1 if 10 . end
0 if 20 . end
```

### Loops

`while <condition> do <ops> end` if condition is 0 then jump to end, otherwise at the end jump to while.

```
# Print natural numbers in [0,10]

0 while dup 11 = ! do
	dup .
	1 +
end
```

### Functions

`<identifier> fun <code> end` - when identifier outside of definition execute `code`

```
factorial fun
	dup 1 > if
		dup 1 - factorial *
	else
		drop 1
	end
end
```

### Supported operations

- `!` - if top = 1 then push 0 else push 1
- `"<string>"` - push address of the null terminated string
- `*` - multiply 2 elements and push result
- `+` - add 2 elements and push result
- `-` - subtracts top from one before top and pushes to stack
- `.` - print top to stdout
- `<int>` - push integer literal (currently only natural numbers up to 2^63-1) onto a stack
- `=` - if top = one before top then push 1 onto stack, else push 0
- `[]byte` - static array declaration where size(cell) = 8bit
- `and` - boolean and
- `constant` - integer constant declaration
- `div` - divide one before top by top and pushes division result
- `divmod` - divides one before top by top and pushes division result and modulo result
- `drop` - drop top element from the stack
- `dup` - duplicate top element
- `mod` - pops 2 numbers and gives modulo
- `nl` - prints newline
- `or` - boolean or
- `over` - `( a b -- a b a )`
- `read<n>` - read from memory n bits where n = 8, 16, 32 or 64 from top element on stack
- `write<n>` - write to memory n bits where n = 8, 16, 32 or 64 from top element on stack
- `print` - print null terminated string
- `rot` - `( a b c -- b c a )`
- `swap` - swap top with before top stack element
- `syscall<n>` - where n >= 0 and n <= 6, e.g. syscall exit(123) `123 60 syscall3`
- `tuck` - `( x1 x2 -- x2 x1 x2 )`

for more documentation currently only source is a source code of compiler.

## See also

- Tsoding [Porth](https://github.com/tsoding/porth)
- Classic stack based language [Forth](https://en.wikipedia.org/wiki/Forth_(programming_language))
- Modern stack based functional language [Factor](https://en.wikipedia.org/wiki/Factor_(programming_language))
