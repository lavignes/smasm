#include <smasm/fatal.h>
#include <smasm/serde.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void help() {
    fprintf(stderr,
            "SMFIX: Corrects the header checksum for a Gameboy ROM\n"
            "\n"
            "Usage: smfix [OPTIONS] <ROM>\n"
            "\n"
            "Arguments:\n"
            "  <ROM>  Gameboy ROM\n"
            "\n"
            "Options:\n"
            "  -o, --output <OUTPUT>        Output file (default: stdout)\n"
            "  -h, --help                   Print help\n");
}

static FILE *openFileCstr(char const *name, char const *modes);
static void  closeFile(FILE *hnd);

static FILE *infile       = NULL;
static char *infile_name  = NULL;
static FILE *outfile      = NULL;
static char *outfile_name = NULL;

int main(int argc, char **argv) {
    outfile = stdout;
    if (argc == 1) {
        help();
        return EXIT_SUCCESS;
    }
    for (int argi = 1; argi < argc; ++argi) {
        if ((strcmp(argv[argi], "-h") == 0) ||
            (strcmp(argv[argi], "--help") == 0)) {
            help();
            return EXIT_SUCCESS;
        }
        if ((strcmp(argv[argi], "-o") == 0) ||
            (strcmp(argv[argi], "--output") == 0)) {
            ++argi;
            if (argi == argc) {
                smFatal("expected file name\n");
            }
            outfile_name = argv[argi];
            continue;
        }
        infile      = openFileCstr(argv[argi], "rb");
        infile_name = argv[argi];
        ++argi;
        if (argi != argc) {
            smFatal("unexpected option: %s\n", argv[argi]);
        }
    }
    SmGBuf  buf   = {0};
    SmSerde serin = {infile, {(U8 *)infile_name, strlen(infile_name)}};
    smDeserializeToEnd(&serin, &buf);

    if (buf.inner.len < 0x014C) {
        smFatal("ROM file too small\n");
    }

    U8 checksum = 0;
    for (UInt i = 0x0134; i < 0x014C; ++i) {
        checksum -= buf.inner.bytes[i];
        --checksum;
    }
    buf.inner.bytes[0x014D] = checksum;

    if (outfile_name) {
        outfile = openFileCstr(outfile_name, "wb+");
    } else {
        outfile_name = "stdout";
    }
    SmSerde serout = {outfile, {(U8 *)outfile_name, strlen(outfile_name)}};
    smSerializeBuf(&serout, buf.inner);
    closeFile(outfile);
}

static FILE *openFileCstr(char const *name, char const *modes) {
    FILE *hnd = fopen(name, modes);
    if (!hnd) {
        smFatal("could not open file: %s: %s\n", name, strerror(errno));
    }
    return hnd;
}

static void closeFile(FILE *hnd) {
    if (fclose(hnd) == EOF) {
        smFatal("failed to close file: %s\n", strerror(errno));
    }
}
