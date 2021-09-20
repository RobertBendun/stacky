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

syn keyword stackyDefinitions constant fun
syn match stackyDefinitions '\[\]byte'
syn keyword stackyBlocks while if else end do

syn keyword stackyOperators ! != * + - .  < << <= = > >= >>
syn keyword stackyOperators and bit-and bit-or bit-xor div divmod mod or min max

syn keyword stackyStack 2drop 2dup 2over 2swap over top tuck rot swap drop dup
syn keyword stackyMemory write8 write16 write32 write64 read8 read16 read32 read64
syn keyword stackyOS syscall0 syscall1 syscall2 syscall3 syscall4 syscall5 syscall6 print nl
" Literals
syn match stackyInteger '\<-\=[0-9]\+.\=\>'
syn region stackyString start='"' end='"'

" Comments
syn match stackyDotCompare contained 'dot\s\+compare'
syn keyword stackyTodo contained TODO FIXME
syn match stackyComment "#.*$" contains=stackyDotCompare

let b:current_syntax = "stacky"

hi def link stackyTodo Todo
hi def link stackyDotCompare SpecialComment
hi def link stackyDefinitions Define
hi def link stackyStack Special
hi def link stackyMemory Identifier
hi def link stackyOS Identifier
hi def link stackyOperators Operator
hi def link stackyWord Identifier
hi def link stackyString String
hi def link stackyInteger Number
hi def link stackyComment Comment
hi def link stackyBlocks Statement
