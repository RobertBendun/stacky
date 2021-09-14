# stacky

**WIP** stack-based compiled programming language with purpose of beeing a learning expirience.

## Example
Program printing "hello!" to stdout

```ruby
# write "hello!" to memory
heap
	dup 0 + 104 poke # h
	dup 1 + 101 poke # e
	dup 2 + 108 poke # l
	dup 3 + 108 poke # l
	dup 4 + 111 poke # o
	dup 5 + 33  poke # !
	dup 6 + 10  poke # \n
	dup 7 + 0   poke # \0

# print cstr starting at heap
heap print
```

## Language reference

###  Comments
Comments starts with `#` and ends at `\n`. Only single-line comments are supported.

### Conditions

`if` jumps to `end` if 0, otherwise continues execution. For example code below prints 10:

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

### Supported operations

- `!` - if top = 1 then push 0 else push 1
- `+` - add 2 elements and push result
- `.` - print top to stdout
- `<int>` - push integer literal (currently only natural numbers up to 2^63-1) onto a stack
- `=` - if top = one before top then push 1 onto stack, else push 0
- `divmod` - divides one before top by top and pushes division result and modulo result
- `dup` - duplicate top element
- `heap` - push heap address
- `mod` - pops 2 numbers and gives modulo
- `nl` - prints newline
- `peek` - read from memory
- `poke` - write to memory
- `print` - print null terminated string
- `swap` - swap top with before top stack element

for more documentation currently only source is a source code of compiler.

## See also

- Tsoding [Porth](https://github.com/tsoding/porth)
- Classic stack based language [Forth](https://en.wikipedia.org/wiki/Forth_(programming_language))
- Modern stack based functional language [Factor](https://en.wikipedia.org/wiki/Factor_(programming_language))
