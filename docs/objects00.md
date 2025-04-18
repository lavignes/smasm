# SMASM Object File Format (Version 0.0) 

The SMASM assembler produces object files in a custom format rather than
a standard format like ELF or COFF.

**Note that all integers in this format are stored in little-endian byte order.**

## Header

Every object file begins with a 4 byte magic string that identifies the
file and the version of the format.

For example:

* `SM00` - Version 0.0 (the format described in this file)
* `SM01` - Version 0.1
* `SM12` - Version 1.2

## Tables

Immediately following the header is a contigious list of tables.
In order, they are:

* String Table
* Expression Table
* Symbol Table
* Section Table

### String Table

| Size | Description                       |
|------|-----------------------------------|
| 4    | Size of the string table in bytes |
| N    | String data                       |

The string table is a contiguous block of memory that contains all the strings
used in the object file. Later parts of the file will refer to strings in the
string table by a 32-bit offset from the start of the string table and a 16-bit
length in bytes:

The beginning of the string table is a 32-bit number representing the size of
the string table in bytes. Following that is the contiguous string data itself.

#### String Reference

| Size | Description                        |
|------|------------------------------------|
| 4    | Offset from the start of the table |
| 2    | Length of the string in bytes      |

As mentioned above. Strings in the string table are referred to by a 32-bit
offset from the start of the string table and a 16-bit length in bytes.

### Expression Table

| Size | Description                             |
|------|-----------------------------------------|
| 4    | Size of the expression table in "nodes" |
| ???  | Expression nodes                        |

The expression table is a contiguous block of memory that contains all the
expressions used in the object file. Later parts of the file will refer to
expressions in the expression table by a 32-bit offset from the start of the
expression table and a 16-bit length in "nodes":

The beginning of the expression table is a 32-bit number representing the size
of the expression table as a count of expression nodes. Following that is the
contiguous list of expression nodes themselves.

#### Expression Reference

| Size | Description                         |
|------|-------------------------------------|
| 4    | Offset from the start of the table  |
| 2    | Length of the expression in "nodes" |

As mentioned above. Expressions in the expression table are referred to by a
32-bit offset from the start of the expression table and a 16-bit length in
"nodes".

#### Expression Nodes

Each expression node starts with an 8-bit kind byte. The kind byte is used to
determine how to interpret the rest of the node.

The following kinds of expression nodes are defined:

| Kind | Description                       |
|------|-----------------------------------|
| $00  | Constant                          |
| $01  | Address                           |
| $02  | Operator                          |
| $03  | Label                             |
| $04  | Tag                               |
| $05  | Relative Label                    |

##### Constant Expression Node

| Size | Description |
|------|-------------|
| 1    | Kind ($00)  |
| 4    | Value       |

The constant expression node is used to represent a constant value in the
expression. It is a 32-bit signed integer value.

##### Address Expression Node

| Size | Description                     |
|------|---------------------------------|
| 1    | Kind ($01)                      |
| 6    | Section name (String reference) |
| 2    | Offset                          |

The address expression node is used to represent an address in the expression.
It is made up of 2 parts:

* A string reference to a section name. (A reference in the string table, 
which is a 32-bit offset from the start of the string table and a 16-bit length)
* A 16-bit unsigned integer offset from the start of that section.

##### Operator Expression Node

| Size | Description        |
|------|--------------------|
| 1    | Kind ($02)         |
| 4    | Operator codepoint |
| 1    | Is binary? (0/1)   |

The operator expression node is used to represent an operator in the expression.
It is made up of 2 parts:

* A 32-bit unsigned integer representing the operator. Most operators are
represented using their unicode codepoint. (See below)
* An 8-bit boolean (0 or 1) representing whether the operator is binary (0) or
unary (1).

Besides the standard ASCII codepoints for operators, SMASM leverages the unicode
public use area to define additional multi-byte operators:

| Codepoint | Operator | Description              |
|-----------|----------|--------------------------|
| $F0030    | `<<`     | Arithmetic left shift    |
| $F0031    | `>>`     | Arithmetic right shift   |
| $F0032    | `~>`     | Logical right shift      |
| $F0033    | `<=`     | Less than or equal to    |
| $F0034    | `>=`     | Greater than or equal to |
| $F0035    | `==`     | Equal to                 |
| $F0036    | `!=`     | Not equal to             |
| $F0037    | `&&`     | Logical AND              |
| $F0038    | `\|\|`   | Logical OR               |
   
##### Label Expression Node

| Size | Description                   |
|------|-------------------------------|
| 1    | Kind ($03)                    |
| 1    | Global (0)                    |
| 6    | Scope name (String reference) |
| 6    | Label name (String reference) |

| Size | Description                   |
|------|-------------------------------|
| 1    | Kind ($03)                    |
| 1    | Local (1)                     |
| 6    | Label name (String reference) |

The label expression node is used to represent a label in the expression.

##### Tag Expression Node

| Size | Description                   |
|------|-------------------------------|
| 1    | Kind ($04)                    |
| 1    | Global (0)                    |
| 6    | Scope name (String reference) |
| 6    | Label name (String reference) |
| 6    | Tag name (String reference)   |

| Size | Description                   |
|------|-------------------------------|
| 1    | Kind ($04)                    |
| 1    | Local (1)                     |
| 6    | Label name (String reference) |
| 6    | Tag name (String reference)   |

The tag expression node is used to represent a `@tag` directive in the
expression.

##### Relative Label Expression Node

The relative label expression node is used to represent a `@rel` directive
in the expression. It is otherwise identical to the label expression node.

### Symbol Table

| Size | Description                           |
|------|---------------------------------------|
| 4    | Size of the symbol table in "symbols" |
| ???  | Symbol nodes                          |

The symbol table is a contiguous block of memory that contains all the symbols
used in the object file. Later parts of the file will refer to symbols in the
symbol table by name rather than by an offset-length pair.

The beginning of the symbol table is a 32-bit number representing the size of
the symbol table as a count of symbols. Following that is the contiguous list
of symbol nodes themselves.

#### Symbol Nodes

| Size | Description                       |
|------|-----------------------------------|
| 1    | Global (0)                        |
| 6    | Scope name (String reference)     |
| 6    | Label name (String reference)     |
| 6    | Expression (Expression reference) |
| 6    | Section name (String reference)   |
| 6    | File name (String reference)      |
| 2    | Line number                       |
| 2    | Column number                     |
| 1    | Flags                             |

| Size | Description                              |
|------|------------------------------------------|
| 1    | Local (1)                                |
| 6    | Label name (String reference)            |
| 6    | Expression (Expression reference)        |
| 6    | Translation unit name (String reference) |
| 6    | Section name (String reference)          |
| 6    | File name (String reference)             |
| 2    | Line number                              |
| 2    | Column number                            |
| 1    | Flags                                    |

### Section Table

| Size | Description                           |
|------|---------------------------------------|
| 4    | Number of sections                    |
| ???  | Sections                              |

The section table is a contiguous block of memory that contains all the sections
used in the object file.

The beginning of the section table is a 32-bit number representing the number of
sections in the table. Following that is the contiguous list of sections
themselves.

#### Section

| Size | Description                            |
|------|----------------------------------------|
| 4    | Section name (String reference)        |
| 4    | Section data length in bytes           |
| N    | Section data                           |
| 4    | Number of "relocations" in the section |
| ???  | Relocations list                       |

#### Relocation

| Size | Description                              |
|------|------------------------------------------|
| 2    | Offset from the start of the section     |
| 1    | Length of the relocation in bytes        |
| 6    | Expression (Expression reference)        |
| 6    | Translation unit name (String reference) |
| 6    | File name (String reference)             |
| 2    | Line number                              |
| 2    | Column number                            |
| 1    | Flags                                    |

