syn clear
syn case ignore

syn match smasmIdentifier "[a-z_\.][a-z0-9_\.]*"
syn match smasmGlobalLabel "[a-z_][a-z0-9_\.]*::\?"
syn match smasmLocalLabel "\.[a-z_][a-z0-9_]*::\?"

syn keyword smasmRegister a b c d e h l af bc de hl sp
syn keyword smasmConditions z c nc nz

syn match smasmOperator display "\%(+\|-\|/\|*\|\^\|\~\|&\||\|!\|>\|<\|%\|=\)=\?"
syn match smasmOperator display "&&\|||\|<<\|>>\|\~>"

syn keyword smasmOpcode ld ldd ldi ldh push pop add adc sub sbc and or xor cp inc dec swap
syn keyword smasmOpcode daa cpl ccf scf nop halt stop di ei rlca rla rrca rra rlc rl rrc rr
syn keyword smasmOpcode sla sra srl bit set res jp jr call rst ret reti

syn match smasmDirective "@db"
syn match smasmDirective "@dw"
syn match smasmDirective "@ds"
syn match smasmDirective "@section"
syn match smasmDirective "@include"
syn match smasmDirective "@incbin"
syn match smasmDirective "@if"
syn match smasmDirective "@end"
syn match smasmDirective "@macro"
syn match smasmDirective "@repeat"
syn match smasmDirective "@struct"
syn match smasmDirective "@strfmt"
syn match smasmDirective "@idfmt"
syn match smasmDirective "@create"
syn match smasmDirective "@fatal"

syn match smasmDirective "@defined"
syn match smasmDirective "@strlen"
syn match smasmDirective "@tag"
syn match smasmDirective "@rel"

syn match smasmDirective "@[0-9]\+"
syn match smasmDirective "@narg"
syn match smasmDirective "@shift"
syn match smasmDirective "@unique"

syn match smasmComment ";.*" contains=smasmTodo
syn match smasmDocComment ";;.*" contains=smasmTodo
syn keyword smasmTodo contained todo fixme xxx warning danger note notice bug
syn match smasmEscape contained "\\."
syn region smasmString start=+"+ end=+"+ contains=smasmEscape
syn region smasmChar start=+'+ end=+'+ contains=smasmEscape

syn match smasmNumber "[0-9][0-9_]*"
syn match smasmNumber "\$[0-9a-fA-F_]\+"
syn match smasmNumber "%[01_]\+"

syn case match

hi def link smasmComment      Comment
hi def link smasmDocComment   SpecialComment
hi def link smasmNumber       Number
hi def link smasmString	      String
hi def link smasmChar         Character
hi def link smasmIdentifier   Identifier
hi def link smasmRegister     SpecialChar
hi def link smasmConditions   SpecialChar
hi def link smasmOpcode       Keyword
hi def link smasmEscape       SpecialChar
hi def link smasmDirective    PreProc
hi def link smasmGlobalLabel  Function
hi def link smasmLocalLabel   Function
hi def link smasmTodo         Todo

let b:current_syntax = "smasm"
set ts=4
set sw=4
set et

