# SMASM Object File Format (Version 0.0) 

The SMASM assembler produces object files in a custom format rather than
a standard format like ELF or COFF.

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

The string table is a contiguous block of memory that contains all the strings
used in the object file. Later parts of the file will refer to strings in the
string table by a 32-bit offset from the start of the string table and a 32-bit
length in bytes.

The beginning of the string table is a 32-bit number representing the size of
the string table in bytes. Following that is the contiguous string data itself.

### Expression Table

The expression table is a contiguous block of memory that contains all the
expressions used in the object file. Later parts of the file will refer to
expressions in the expression table by a 32-bit offset from the start of the
expression table and a 32-bit length in "expression nodes".

The beginning of the expression table is a 32-bit number representing the size
of the expression table as a count of expression nodes. Following that is the
contiguous list of expression nodes themselves.

#### Expression Nodes

Each expression node starts with an 8-bit kind byte. The kind byte is used to
determine how to interpret the rest of the node.

The following kinds of expression nodes are defined:

* `$00` - Constant
* `$01` - Address
* `$02` - Operator
* `$03` - Label
* `$04` - Tag
* `$05` - Relative Label

##### Constant Expression Node

The constant expression node is used to represent a constant value in the
expression. It is a 32-bit signed integer value.

##### Address Expression Node

The address expression node is used to represent an address in the expression.
It is made up of 2 parts:

* A string reference to a section name. (A reference in the string table, 
which is a 32-bit offset from the start of the string table and a 32-bit length)
* A 32-bit unsigned integer offset from the start of that section.

##### Operator Expression Node

The operator expression node is used to represent an operator in the expression.
It is made up of 2 parts:

* A 32-bit unsigned integer representing the operator. Most operators are
represented using their unicode codepoint. (See below)
* An 8-bit boolean (0 or 1) representing whether the operator is binary (0) or
unary (1).

Besides the standard ASCII codepoints for operators, SMASM leverages the unicode
public use area to define additional multi-byte operators:

* `$F0030` - `<<` (Arithmethic left shift) 
* `$F0031` - `>>` (Arithmethic right shift)
* `$F0032` - `~>` (Logical right shift)
* `$F0033` - `<=` (Less than or equal to)
* `$F0034` - `>=` (Greater than or equal to)
* `$F0035` - `==` (Equal to)
* `$F0036` - `!=` (Not equal to)
* `$F0037` - `&&` (Logical AND)
* `$F0038` - `||` (Logical OR)

##### Label Expression Node

The label expression node is used to represent a label in the expression.

##### Tag Expression Node

The tag expression node is used to represent a `@tag` directive in the
expression.

##### Relative Label Expression Node

The relative label expression node is used to represent a `@rel` directive
in the expression. It is otherwise identical to the label expression node.

### Symbol Table

### Section Table
