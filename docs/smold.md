# SMASM: A Linker for the SM83 (Gameboy) CPU

```
SMOLD: A linker for the SM83 (Gameboy) CPU

Usage: smold [OPTIONS] --config <CONFIG> [OBJECTS]...

Arguments:
  [OBJECTS]...  Object files

Options:
  -c, --config <CONFIG>        Config file
  -o, --output <OUTPUT>        Output file (default: stdout)
  -g, --debug <DEBUG>          Output file for `SYM` debug symbols
      --tags <TAGS>            Output file for ctags
  -D, --define <KEY1=val>      Pre-defined symbols (repeatable)
  -h, --help                   Print help
```

## Configuring the Linker

SMOLD linker configuration files use a similar logical structure to the linker
script files used by linkers such as GNU LD. The configuration file is a text
file that contains a series of commands and directives that control how the
linker processes the input object files and generates the output binary.

At the top level of the configuration file, you can define "output" sections.
These sections specify the addresses (both logical and physical) and sizes of
chunks of data and code in the output binary.

The sections you define in your source files are "input" sections.

The most minimal form of a configuration file consists of a single output
section:

```
SECTION {
    ; The "CODE" output section
    CODE start=$0000 size=$8000 kind=RO {

        ; A "wildcard" input section ("*") implies that the input section
        ; name matches the output section name.

        * kind=CODE

        ; The above input section is equivalent to: CODE kind=CODE
        ; More input sections can be defined here if desired
    }

    ; More output sections can follow here
}
```

This example is not really realistic, but will be accepted by the linker for
an empty object file.

In the example above, the `SECTION` directive contains a single output section
named `CODE`. The `CODE` section has a logical address of `$0000` and a size of
`$8000` bytes. The `kind=RO` directive indicates that this section is read-only.

Within the `CODE` section there is a wildcard input section: (`*`). This
indicates that the linker should include all input sections that match the name
of the output section. The `kind=CODE` directive indicates that this input
section is of type `CODE`. The linker will search for input sections with the
name `CODE` and include them at the start of the output binary.
