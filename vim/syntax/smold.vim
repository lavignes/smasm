syn clear
syn case ignore

syn match smoldIdentifier "[a-z_\.][a-z0-9_\.]*"

syn keyword smoldAttr kind align size fill define start
syn keyword smoldVal code data uninit gb_hram ro rw

syn match smoldComment ";.*" contains=smasmTodo
syn match smoldDocComment ";;.*" contains=smasmTodo
syn keyword smoldTodo contained todo fixme xxx warning danger note notice bug

syn match smoldNumber "[0-9][0-9_]*"
syn match smoldNumber "\$[0-9a-fA-F_]\+"
syn match smoldNumber "%[01_]\+"

syn case match

hi def link smoldComment      Comment
hi def link smoldDocComment   SpecialComment
hi def link smoldNumber       Number
hi def link smoldIdentifier   Identifier
hi def link smoldVal          Constant
hi def link smoldAttr         Keyword
hi def link smoldTodo         Todo

let b:current_syntax = "smold"
set ts=4
set sw=4
set et

