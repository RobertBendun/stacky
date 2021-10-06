" Vim syntax file
" Language: Stacky
" Maintainer: Robert Bendun
" Latest Change: 2021-09-20
" Filenames: *.stacky

if exists("b:current_syntax")
	finish
endif

" Keywords

setlocal iskeyword=!,@,33-35,%,$,38-64,A-Z,91-96,a-z,123-126,128-255

syn keyword stackyIncludes include import

syn keyword stackyType bool ptr u64

syn keyword stackyDefinitions constant fun &fun
syn match stackyDefinitions '\[\]byte'
syn match stackyDefinitions '\[\]u8'
syn match stackyDefinitions '\[\]u16'
syn match stackyDefinitions '\[\]u32'
syn match stackyDefinitions '\[\]u64'
syn keyword stackyBlocks while if else end do return

syn keyword stackyOperators ! != * + - < << <= = > >= >> &
syn keyword stackyOperators and bit-and bit-or bit-xor div divmod mod or min max

syn keyword stackyStack 2drop 2dup 2over 2swap over top tuck rot swap drop dup
syn keyword stackyMemory store8 store16 store32 store64 load8 load16 load32 load64 call
syn keyword stackyOS syscall0 syscall1 syscall2 syscall3 syscall4 syscall5 syscall6 random32 random64

" Literals

syn keyword stackyConstant true false

syn match stackyInteger '\<-\=[0-9]\+.\=\>'

syn match	stackySpecial	display contained "\\\(x\x\+\|\o\{1,3}\|.\|$\)"
syn match	stackySpecial	display contained "\\\(u\x\{4}\|U\x\{8}\)"

syn region stackyString		start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=stackySpecial,@Spell extend
syn match	stackyCharacter	"'[^']*'" contains=stackySpecial

" Comments
syn match stackyDotCompare contained 'dot\s\+compare'
syn keyword stackyTodo contained TODO FIXME
syn match stackyComment "#.*$" contains=stackyDotCompare

let b:current_syntax = "stacky"

hi  def  link  stackyTodo         Todo
hi  def  link  stackyIncludes     Include
hi  def  link  stackyDotCompare   SpecialComment
hi  def  link  stackyDefinitions  Define
hi  def  link  stackyStack        Special
hi  def  link  stackyMemory       Identifier
hi  def  link  stackyOS           Identifier
hi  def  link  stackyOperators    Operator
hi  def  link  stackyWord         Identifier
hi  def  link  stackyString       String
hi  def  link  stackyCharacter    Character
hi  def  link  stackySpecial      SpecialChar
hi  def  link  stackyInteger      Number
hi  def  link  stackyType         Type
hi  def  link  stackyConstant     Constant
hi  def  link  stackyComment      Comment
hi  def  link  stackyBlocks       Statement
