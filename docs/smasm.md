# SMASM: An assembler for the SM83 (Gameboy) CPU

```
Usage: smasm [OPTIONS] <SOURCE>

Arguments:
  <SOURCE>  Assembly source file

Options:
  -o, --output <OUTPUT>        Output file (default: stdout)
  -D, --define <KEY1=val>      Pre-defined symbols (repeatable)
  -I, --include <INCLUDE>      Search directories for included files (repeatable)
  -MD                          Output Makefile dependencies
  -MF <DEPFILE>                Make dependencies file (default: <SOURCE>.d)
  -h, --help                   Print help
```

## Syntax

### Token Types

* Identifiers: Start with a `.` (dot), `_` (underscore), or a letter, followed
by any number of `.`, `_`, letters, or digits:
  * `example`
  * `.example`
  * `_example`
  * `exam.ple`
* Strings: Any text enclosed in `"` (double-quotes). Most C-style string
escapes are supported (e.g. `\n`, `\r`, `\\`, etc):
  * `"this is a string"`
  * `"this is\nalso a string"`
* Numbers:
  * Base-10: Any text consisting of a series of base-10 digits (e.g. `1234`)
  * Base-16: To specify numbers in base-16 (hexadecimal) use a `$` (dollar-sign)
prefix (e.g. `$dadcafe`).
  * Base-2: Similar to base-16, you can specify base-2 (binary) with a `%`
(modulus or percent-sign) prefix (e.g. `%01101001`).
  * All numbers can contain `_` (underscores) for better readability
(e.g. `%0001_1001_0101_1111`).
  * Single ASCII characters can be used to represent numbers as well. Enclose
any character or single character escape sequence in `'` (single-quotes)
and it will be treated by the assembler as an 8-bit number (e.g. `'Q'`).
* Directives: Any identifier that starts with `@` (asperand or at-sign).
Directives are generally reserved for use by the assembler:
  * `@include`
  * `@section`
  * `@macro`
  * `@strlen`
* Single and multi-character symbols. These include mathematical operators
(e.g. `+`, `-`, `*`, `/` etc) and semantic symbols (e.g. `:` and `::`).
* Comments: Any text starting with a `;` (semi-colon) until the end of the line:
  * `; this is a comment`

### Labels

Any identifier is also a valid label. Labels use `.` (dot) characters to
provide additional semantic meaning.

For example, when you define this subroutine:

```
MemCopy:
    inc b
    inc c
    jr .Decrement
.CopyByte:
    ldi a, [hl]
    ld [de], a
    inc de
.Decrement:
    dec c
    jr nz, .CopyByte
    dec b
    jr nz, .CopyByte
    ret
```

Two symbols will be defined:
* `MemCopy` equal to the address of the subroutine.
* `MemCopy.CopyByte` equal to the address of the `ldi` opcode.
* `MemCopy.Decrement` equal, of course, to the address of the `dec` opcode.

`.CopyByte` and `.Decrement` above are examples of a "local" label.
Whenever you create a label without a `.` (dot) a new "scope" is created,
under which every local label will be prefixed with in the symbol table.

Expressions can refer to local labels as long as they fall under the
same scope:

```
Delay:
  ld a, 100
.Loop:
  dec a
  jr nz, .Loop
  ret
```

In this case, `.Loop` is simply syntactic sugar for `Delay.Loop` and you
can always still refer to `Delay.Loop` from other subroutines.

### Symbol Visibility

The assembler begins by taking a single source file as the root of the
[translation unit](https://en.wikipedia.org/wiki/Translation_unit_(programming)).
Its output will be a single [object file](https://en.wikipedia.org/wiki/Object_file).
All symbols defined in the resulting object file will not be visible to other
object files at link-time. To make a label visible outside of the current
translation unit, you can define it with a `::` (double-colon):

```
PublicOrExportedSubroutine::
    inc a
.ExportedLocalLabel::
    rlca
    ret
```

### Constants

You can use `=` (equals) to define constant numeric symbols that you can later
reference in expressions:

```
FACTOR = 2
BIGNUM = 8 * FACTOR

AddAmount:
    add a, BIGNUM
    ret
```

Note that constant symbols are not visible outside the current translation unit.
To share constants across translation units, place them in a "header" file
and `@include` the header in each translation unit as needed.

### Expressions

Expressions operate on 32-bit signed [two's complement](https://en.wikipedia.org/wiki/Two%27s_complement)
numbers only. All numeric operators are taken from C, with identical
[precendence and associativity](https://en.cppreference.com/w/c/language/operator_precedence).

A few additional convenience operators are included:
* Unary `<` (less-than):
  * Associativity: Right-to-left
  * Precedence: Same as unary `-` (negative).
  * Returns the first (little-endian) byte of a number.
  * Equivalence:
    * `<$123456 == $56`
    * `<$123456 == ($123456 & $FF)`
* Unary `>` (greater-than):
  * Associativity: Right-to-left
  * Precedence: Same as unary `-` (negative).
  * Returns the second (little-endian) byte of a number.
  * Equivalence:
    * `>$123456 == $34`
    * `>$123456 == (($123456 & $FF00) >> 8)`
* Unary `^` (caret):
  * Associativity: Right-to-left
  * Precedence: Same as unary `-` (negative).
  * Returns the third (little-endian) byte of a number.
  * Equivalence:
    * `^$123456 == $12`
    * `^$123456 == (($123456 & $FF0000) >> 16)`
* `~>` (logical shift right):
  * Associativity: Left-to-right
  * Precedence: Same as `>>` (arithmethic shift right).
  * Shifts a number right by a given number of bits, replacing the highest
order bits with `0` (zeros).
  * Equivalence: `(%1111_1111 ~> 2) == %0011_1111`

### Sections

The default section at the start of a translation unit is named `CODE`.

Use the `@section` directive to create and switch between sections:

```
@section "ROMBANK2"

Subroutine:
    ; some code
    ret
```

You can freely switch to and from different sections at any time. All code
under a `@section` directive will be automatically appended to a section if it
already exists.

### Offsets and the Program Counter

There are two ways to reference the program counter in this assembler.

First is the classic `*` (star) typical to many assemblers. When used in place
of a value in an expression it returns the _relative_ program counter. This
is a counter that starts at zero at the start of a section and increments
as you place code and data in that section:

```
@section "code"
    clc
    bcc * ; infinite loop
```

The relative program counter can also be modified. But it does not add
any padding to the section (use `@ds` for that):

```
@section "code"
    * = $1234
```

Because `*` is a relative address from the start of the current section,
you should not use it for absolute addressesing modes. It will result in a
incorrect address unless you set it to the exact physical address that a
section will be ultimately placed during linking.

A common pattern that uses `*` is calculating the size of some data or a
subroutine:

```
SomeDataOrSubroutine::
    nop
    nop
    nop
    ret
.Len = * - @rel SomeDataOrSubroutine
```

Here `@rel` is a special operator directive used to calculate the relative
address of a label. If you don't use `@rel` here, the assembler will throw
and error because the address of `SomeDataOrSubroutine` cannot normally
be calculated until link-time.

`**` (double-star) is the _absolute_ program counter. It is available for use
in expressions to tell you the final physical address at any point in any
section. Note though, since this value results in the physical address of
any point in the code, it can only be resolved at link-time. There is little
reason to use this outside of metaprogramming or building special jump-tables.

### Section Tags

Additional metadata can be associated with all symbols defined in a section.
These are called "tags". Each tag has a name and a numeric value that can be
referenced in expressions.

The main usecase for tags is assigning a bank number to a section in
memory-banking systems. In this example, we can imagine a routine that needs
to switch a ROM bank into memory before calling a subroutine:

```
@section "FIXEDBANK"

Routine:
    ld a, [banksel]               ; Save current bank on stack.
    push af
    ld a, @tag Subroutine, "bank" ; Lookup the "bank" tag of Subroutine.
    ld [banksel], a               ; Switch banks and call Subroutine.
    call Subroutine
    pop af
    ld [banksel], a               ; Restore previous bank from stack.
    ret
``` 

The `@tag` directive can be used in place of any numeric expression.
The actual numeric value of a tag is assigned to a section (and all of its
symbols) at link-time in your linking configuration file.

### Defining Data

Like other assemblers, you can use directives to define raw data or
reserve space.

Use `@db` (define bytes) to assemble raw ASCII strings or bytes:

```
FILLBYTE = $42

MixedBytes:
    @db FILLBYTE, FILLBYTE, FILLBYTE
    @db '\t', "Hello World", $10
```

Use `@dw` (define words) to do the same with 16-bit words:

```
BunchOfWords:
    @dw $FF00 + 2
    @dw $FFFF, $1234, $5678
```

Use `@ds` (define space) to simply reserve space in RAM sections.
Typically, you'll use this to reserve addresses in RAM or zero-page:

```
TEMPCOUNT = 8
BUFFERSIZE = 16

@section "HRAM"

temp::   @ds TEMPCOUNT
buffer:: @ds BUFFERSIZE
```

Lastly, you can embed entire files in their raw binary form using the `@incbin`
directive:

```
TileData:
    @incbin "res/tiles.2bpp"
.Len = * - @rel TileData
```

### String and Identifier Formatting

The assembler supports ways to do printf-style formatting for strings and
identifiers.

The `@strfmt` directive will take a C printf format string and list of arguments
and can be used in any place a string could otherwise be used in the source
file:
* `@strfmt "test"` is equivlent to `"test"`.
* `@strfmt "test%d", 42` is equivlent to `"test42"`.
* `@strfmt "test%04x", $FF` is equivlent to `"test00ff"`.

The `@idfmt` directive performs identically, but yields an identifier.

You can enclose arguments to formatting directives in `{` and `}` to
disambiguate and improve readability:

```
; yields: "HelloWorld !"
@db @strfmt{"Hello%- 6s!", "World"} 
```

The entire C99 standard printf format string syntax is supported with
some modifications:
* No length modifiers (e.g. the `hh` in `%hhd` for 8-bit decimals).
These are extraneous as all arguments are 32-bit integers or strings.
* The `%s` (string) conversion accepts both strings and identifiers.
* No `%o` octal (base-8) conversion. _This is a hill the author will die on._
* No floating-point conversion (e.g. `%f`, `%g`, `%e`). No floats to print.
* No string length output conversion (`%n`). It could be done, but seems
esoteric.

### Repeating Code Blocks 

You can repeat blocks of code using the `@repeat` directive:

```
; inserts 3 nops
@repeat 3
    nop
@end
```

You can optionally specify an identifier to be used in expressions within
the repeating block of code. The value of this identifier starts at `0` and
increments at the end of each iteration of the loop:

```
    xor a, a
@repeat 3, i
    add a, i+1
@end

; This expands to:
    xor a, a
    add a, 1
    add a, 2
    add a, 3
```

### Macros

Macros are defined using the `@macro` directive:

```
; Macro to lookup the bank of a subroutine
@macro BANKOF
    @tag @1, "bank"
@end

Routine:
    ld a, BANKOF Subroutine
```

Macros take a variable number of arguments. Arguments are delimited by ','
(comma) and typically terminate at the end of the current line. 

To disambiguate when a set of macro arguments start and stop on the same line
you can wrap the macro aguments in `{` and `}` (left and right curly braces):

```
@macro ADD
    @0 + @1
@end

@macro QUADRUPLE
    ADD ADD{@0, @0}, ADD{@0, @0}
@end
```

As you can see, the arguments of macros can be referenced using `@0`, `@1`,
`@2`, etc.

To get the number of arguments passed to the macro you can use the `@narg`
directive in an expression. 

To "shift" the arguments, that is, remove the first argument and renumber
the arguments starting from `@1`, you can use the `@shift` directive.
Note that shifting also decreases the value returned by `@narg`:

```
; Run-length encode words
@macro RLE
    @dw @narg
    @repeat @narg
        @dw @0
        @shift ; @1 becomes the new @0, and so on...
    @end
@end

RLE 1, 2, 3, 4

; This expands to:
@dw 4
@dw 1, 2, 3, 4
```

You'll often use macros to generate symbols. Sometimes you don't actually care
about the specific names of those symbols, but you _do_ need to ensure they are
unqiue so they don't conflict across macro invocations. You can use the
`@unique` directive to return a number that is unique to that specific
invocation of that macro across the entire compilation unit.

Combine `@unique` with `@idfmt` to synthesize unique symbol names:

```
@macro DOES_SOMETHING 
    @idfmt{"TMP%d", @unique} = $1234
    ; ...
    ; Can reference it later
    ; ...
    @dw @idfmt{"TMP%d", @unique}
@end
```

### Conditional Assembly

Entire blocks of code can be "turned off" using the `@if` directive.

The most common use-case for this is to implement [include guards](https://en.wikipedia.org/wiki/Include_guard)
for header files.

The syntax for this is essentially the same as a C include guard:

`example.ssi`:
```
; If the symbol EXAMPLE_INC is not yet defined in this translation unit
; define it to the value 1.
; If the symbol is already defined, the assembler will ignore all tokens in
; the file until it finds the matching @end. 
@if !@defined EXAMPLE_SSI
EXAMPLE_SSI = 1

CONSTANT = $42

@end
```

`source.ssm`:
```
@include "example.inc"

LoadConstant:
    ld a, CONSTANT
```
