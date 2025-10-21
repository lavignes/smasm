#include "expr.h"
#include "fmt.h"
#include "macro.h"
#include "mne.h"
#include "state.h"
#include "struct.h"

#include <smasm/fatal.h>
#include <smasm/serde.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void help() {
    fprintf(stderr,
            "SMASM: An assembler for the SM83 (Gameboy) CPU\n"
            "\n"
            "Usage: smasm [OPTIONS] <SOURCE>\n"
            "\n"
            "Arguments:\n"
            "  <SOURCE>  Assembly source file\n"
            "\n"
            "Options:\n"
            "  -o, --output <OUTPUT>        Output file (default: stdout)\n"
            "  -D, --define <KEY1=val>      Pre-defined symbols (repeatable)\n"
            "  -I, --include <INCLUDE>      Search directories for included "
            "files (repeatable)\n"
            "  -MD                          Output Makefile dependencies\n"
            "  -MF <DEPFILE>                Make dependencies file (default: "
            "<SOURCE>.d)\n"
            "  -h, --help                   Print help\n");
}

static SmExprView constExprBuf(I32 num);
static FILE      *openFileCstr(char const *name, char const *modes);
static void       closeFile(FILE *hnd);
static void       pushFile(SmView path);
static void       pass();
static void       rewindPass();
static void       writeDepend();
static void       serialize();

static FILE *infile       = NULL;
static char *infile_name  = NULL;
static FILE *outfile      = NULL;
static char *outfile_name = NULL;
static char *depfile_name = NULL;
static Bool  makedepend   = false;

int main(int argc, char **argv) {
    outfile = stdout;
    if (argc == 1) {
        help();
        return EXIT_SUCCESS;
    }
    DEFINES_SECTION = intern(SM_VIEW("@DEFINES"));
    CODE_SECTION    = intern(SM_VIEW("CODE"));
    STATIC_UNIT     = intern(SM_VIEW("@STATIC"));
    EXPORT_UNIT     = intern(SM_VIEW("@EXPORT"));
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
        if (!strcmp(argv[argi], "-D") || !strcmp(argv[argi], "--define")) {
            ++argi;
            if (argi == argc) {
                smFatal("expected symbol definition\n");
            }
            char const *offset = strchr(argv[argi], '=');
            if (!offset) {
                smFatal("expected `=` in %s\n", argv[argi]);
            }
            UInt   name_len  = offset - argv[argi];
            SmView name      = intern((SmView){(U8 *)argv[argi], name_len});
            UInt   value_len = strlen(argv[argi]) - name_len - 1;
            // TODO: should expose general-purpose expression parsing
            // for use here and for expressions in the linker scripts
            // Could even use a tok stream here.
            UInt   num       = smViewParse(
                (SmView){(U8 *)(argv[argi] + name_len + 1), value_len});
            smSymTabAdd(&SYMS, (SmSym){
                                   .lbl     = lblGlobal(name),
                                   .value   = constExprBuf(num),
                                   .unit    = STATIC_UNIT,
                                   .section = DEFINES_SECTION,
                                   .pos     = {DEFINES_SECTION, 1, 1},
                                   .flags   = SM_SYM_EQU,
                               });
            continue;
        }
        if (!strcmp(argv[argi], "-I") || !strcmp(argv[argi], "--include")) {
            ++argi;
            if (argi == argc) {
                smFatal("expected file name\n");
            }
            smPathSetAdd(&IPATHS,
                         (SmView){(U8 *)argv[argi], strlen(argv[argi])});
            continue;
        }
        if (!strcmp(argv[argi], "-MD")) {
            makedepend = true;
            continue;
        }
        if (!strcmp(argv[argi], "-MF")) {
            ++argi;
            if (argi == argc) {
                smFatal("expected file name\n");
            }
            depfile_name = argv[argi];
            continue;
        }
        infile      = openFileCstr(argv[argi], "rb");
        infile_name = argv[argi];
        ++argi;
        if (argi != argc) {
            smFatal("unexpected option: %s\n", argv[argi]);
        }
    }

    pushFile(
        smPathIntern(&STRS, (SmView){(U8 *)infile_name, strlen(infile_name)}));
    pass();
    rewindPass();
    pass();
    popStream();

    if (outfile_name) {
        outfile = openFileCstr(outfile_name, "wb+");
    } else {
        outfile_name = "stdout";
    }

    if (makedepend) {
        writeDepend();
    }

    serialize();
    closeFile(outfile);
    return EXIT_SUCCESS;
}

static void rewindPass() {
    smTokStreamRewind(ts);
    sectRewind();
    macroTabFini();
    scope     = SM_VIEW_NULL;
    nonce     = 0;
    emit      = true;
    streamdef = false;
}

static void expectEOL() {
    switch (peek()) {
    case SM_TOK_EOF:
    case '\n':
        return;
    default:
        fatal("expected end of line\n");
    }
}

static void expectReprU8(SmPos pos, I32 num) {
    if (!exprCanReprU8(num)) {
        fatalPos(pos, "expression does not fit in byte: $%08X\n", num);
    }
}

static void expectReprU16(SmPos pos, I32 num) {
    if (!exprCanReprU16(num)) {
        fatalPos(pos, "expression does not fit in word: $%08X\n", num);
    }
}

static void emitView(SmView view) { smBufCat(&sectGet()->data, view); }
static void emit8(U8 byte) { emitView((SmView){&byte, 1}); }
static void emit16(U16 word) {
    U8 bytes[2] = {word & 0x00FF, word >> 8};
    emitView((SmView){bytes, 2});
}

static void reloc(U16 offset, U8 width, SmExprView view, SmPos pos, U8 flags) {
    smRelocBufAdd(&sectGet()->relocs, (SmReloc){
                                          .offset = getPC() + offset,
                                          .width  = width,
                                          .value  = view,
                                          .unit   = STATIC_UNIT,
                                          .pos    = pos,
                                          .flags  = flags,
                                      });
}

static Bool loadIndirect(U32 tok, U8 *op) {
    switch (tok) {
    case SM_TOK_BC:
        *op = 0x0A;
        return true;
    case SM_TOK_DE:
        *op = 0x1A;
        return true;
    case SM_TOK_HL:
        *op = 0x7E;
        return true;
    default:
        return false;
    }
}

static Bool storeIndirect(U32 tok, U8 *op) {
    switch (tok) {
    case SM_TOK_BC:
        *op = 0x02;
        return true;
    case SM_TOK_DE:
        *op = 0x12;
        return true;
    default:
        return false;
    }
}

static Bool reg8Offset(U32 tok, U8 base, U8 *op) {
    switch (tok) {
    case 'B':
        *op = base + 0;
        return true;
    case 'C':
        *op = base + 1;
        return true;
    case 'D':
        *op = base + 2;
        return true;
    case 'E':
        *op = base + 3;
        return true;
    case 'H':
        *op = base + 4;
        return true;
    case 'L':
        *op = base + 5;
        return true;
    case 'A':
        *op = base + 7;
        return true;
    default:
        return false;
    }
}

static Bool reg16OffsetSP(U32 tok, U8 base, U8 *op) {
    switch (tok) {
    case SM_TOK_BC:
        *op = base + 0x00;
        return true;
    case SM_TOK_DE:
        *op = base + 0x10;
        return true;
    case SM_TOK_HL:
        *op = base + 0x20;
        return true;
    case SM_TOK_SP:
        *op = base + 0x30;
        return true;
    default:
        return false;
    }
}

static void loadStoreIncDec(U8 load, U8 store) {
    eat();
    switch (peek()) {
    case 'A':
        eat();
        expect(',');
        eat();
        expect('[');
        eat();
        expect(SM_TOK_HL);
        eat();
        expect(']');
        eat();
        if (emit) {
            emit8(load);
        }
        addPC(1);
        return;
    default:
        expect('[');
        eat();
        expect(SM_TOK_HL);
        eat();
        expect(']');
        eat();
        expect(',');
        eat();
        expect('A');
        eat();
        if (emit) {
            emit8(store);
        }
        addPC(1);
        return;
    }
}

static Bool reg16OffsetAF(U32 tok, U8 base, U8 *op) {
    switch (tok) {
    case SM_TOK_BC:
        *op = base + 0x00;
        return true;
    case SM_TOK_DE:
        *op = base + 0x10;
        return true;
    case SM_TOK_HL:
        *op = base + 0x20;
        return true;
    case SM_TOK_AF:
        *op = base + 0x30;
        return true;
    default:
        return false;
    }
}

static void pushPop(U8 base) {
    U8 op;
    eat();
    if (!reg16OffsetAF(peek(), base, &op)) {
        fatal("illegal operand\n");
    }
    eat();
    if (emit) {
        emit8(op);
    }
    addPC(1);
}

static void aluReg8(U8 base, U8 imm) {
    U8         op;
    SmPos      pos;
    SmExprView view;
    I32        num;
    eat();
    expect(',');
    eat();
    if (reg8Offset(peek(), base, &op)) {
        eat();
        if (emit) {
            emit8(op);
        }
        addPC(1);
        return;
    }
    if (peek() == '[') {
        eat();
        expect(SM_TOK_HL);
        eat();
        expect(']');
        eat();
        if (emit) {
            emit8(base + 6);
        }
        addPC(1);
        return;
    }
    view = exprEatPos(&pos);
    if (emit) {
        emit8(imm);
        if (exprSolve(view, &num)) {
            expectReprU8(pos, num);
            emit8(num);
        } else {
            emit8(0xFD);
            reloc(1, 1, view, pos, 0);
        }
    }
    addPC(2);
}

static void doAluReg8Cb(U8 base) {
    U8 op;
    eat();
    if (emit) {
        emit8(0xCB);
    }
    if (reg8Offset(peek(), base, &op)) {
        eat();
        if (emit) {
            emit8(op);
        }
        addPC(2);
        return;
    }
    expect('[');
    eat();
    expect(SM_TOK_HL);
    eat();
    expect(']');
    eat();
    if (emit) {
        emit8(base + 6);
    }
    addPC(2);
}

static void doBitCb(U8 base) {
    U8         op;
    SmPos      pos;
    SmExprView view;
    I32        num;
    eat();
    view = exprEatPos(&pos);
    expect(',');
    eat();
    if (reg8Offset(peek(), base, &op)) {
        eat();
    } else {
        expect('[');
        eat();
        expect(SM_TOK_HL);
        eat();
        expect(']');
        eat();
        op = base + 6;
    }
    if (emit) {
        emit8(0xCB);
        if (!exprSolve(view, &num)) {
            fatalPos(pos, "expression must be constant\n");
        }
        if ((num < 0) || (num > 7)) {
            fatalPos(pos, "bit number must be between 0 and 7\n");
        }
        emit8(op + (((U8)num) * 8));
    }
    addPC(2);
}

static Bool flag(U32 tok, U8 base, U8 *op) {
    switch (tok) {
    case SM_TOK_NZ:
        *op = base + 0x00;
        return true;
    case 'Z':
        *op = base + 0x08;
        return true;
    case SM_TOK_NC:
        *op = base + 0x10;
        return true;
    case 'C':
        *op = base + 0x18;
        return true;
    default:
        return false;
    }
}

static void eatMne(U8 mne) {
    U8         op;
    SmPos      pos;
    SmExprView view;
    I32        num;
    switch (mne) {
    case MNE_LD:
        eat();
        switch (peek()) {
        case 'A':
            eat();
            expect(',');
            eat();
            switch (peek()) {
            case '[':
                eat();
                if (loadIndirect(peek(), &op)) {
                    eat();
                    expect(']');
                    eat();
                    if (emit) {
                        emit8(op);
                    }
                    addPC(1);
                    return;
                }
                view = exprEatPos(&pos);
                expect(']');
                eat();
                if (emit) {
                    emit8(0xFA);
                    if (exprSolve(view, &num)) {
                        expectReprU16(pos, num);
                        emit16(num);
                    } else {
                        emit16(0xFDFD);
                        reloc(1, 2, view, pos, 0);
                    }
                }
                addPC(3);
                return;
            default:
                if (reg8Offset(peek(), 0x78, &op)) {
                    eat();
                    if (emit) {
                        emit8(op);
                    }
                    addPC(1);
                    return;
                }
                view = exprEatPos(&pos);
                if (emit) {
                    emit8(0x3E);
                    if (exprSolve(view, &num)) {
                        expectReprU8(pos, num);
                        emit8(num);
                    } else {
                        emit8(0xFD);
                        reloc(1, 1, view, pos, 0);
                    }
                }
                addPC(2);
                return;
            }
        case 'B':
            aluReg8(0x40, 0x06);
            return;
        case 'C':
            aluReg8(0x48, 0x0E);
            return;
        case 'D':
            aluReg8(0x50, 0x16);
            return;
        case 'E':
            aluReg8(0x58, 0x1E);
            return;
        case 'H':
            aluReg8(0x60, 0x26);
            return;
        case 'L':
            aluReg8(0x68, 0x2E);
            return;
        case '[':
            eat();
            if (storeIndirect(peek(), &op)) {
                eat();
                expect(']');
                eat();
                expect(',');
                eat();
                expect('A');
                eat();
                if (emit) {
                    emit8(op);
                }
                addPC(1);
                return;
            }
            if (peek() == SM_TOK_HL) {
                eat();
                expect(']');
                eat();
                expect(',');
                eat();
                if (reg8Offset(peek(), 0x70, &op)) {
                    eat();
                    if (emit) {
                        emit8(op);
                    }
                    addPC(1);
                    return;
                }
                view = exprEatPos(&pos);
                if (emit) {
                    emit8(0x36);
                    if (exprSolve(view, &num)) {
                        expectReprU8(pos, num);
                        emit8(num);
                    } else {
                        emit8(0xFD);
                        reloc(1, 1, view, pos, 0);
                    }
                }
                addPC(2);
                return;
            }
            view = exprEatPos(&pos);
            expect(']');
            eat();
            expect(',');
            eat();
            if (peek() == SM_TOK_SP) {
                eat();
                op = 0x08;
            } else {
                expect('A');
                eat();
                op = 0xEA;
            }
            if (emit) {
                emit8(op);
                if (exprSolve(view, &num)) {
                    expectReprU16(pos, num);
                    emit16(num);
                } else {
                    emit16(0xFDFD);
                    reloc(1, 2, view, pos, 0);
                }
            }
            addPC(3);
            return;
        default:
            if (reg16OffsetSP(peek(), 0x01, &op)) {
                eat();
                expect(',');
                eat();
                view = exprEatPos(&pos);
                if (emit) {
                    emit8(op);
                    if (exprSolve(view, &num)) {
                        expectReprU16(pos, num);
                        emit16(num);
                    } else {
                        emit16(0xFDFD);
                        reloc(1, 2, view, pos, 0);
                    }
                }
                addPC(3);
                return;
            }
            fatal("illegal operand\n");
        }
    case MNE_LDD:
        loadStoreIncDec(0x3A, 0x32);
        return;
    case MNE_LDI:
        loadStoreIncDec(0x2A, 0x22);
        return;
    case MNE_LDH:
        eat();
        if (peek() == 'A') {
            eat();
            expect(',');
            eat();
            expect('[');
            eat();
            if (peek() == 'C') {
                eat();
                expect(']');
                eat();
                if (emit) {
                    emit8(0xE2);
                }
                addPC(1);
                return;
            }
            view = exprEatPos(&pos);
            expect(']');
            eat();
            if (emit) {
                emit8(0xF0);
                if (exprSolve(view, &num)) {
                    if ((num < 0xFF00) || (num > 0xFFFF)) {
                        fatalPos(pos, "address not in high memory: $%08X\n",
                                 num);
                    }
                    emit8(num & 0x00FF);
                } else {
                    emit8(0xFD);
                    reloc(1, 1, view, pos, SM_RELOC_HRAM);
                }
            }
            addPC(2);
            return;
        }
        expect('[');
        eat();
        if (peek() == 'C') {
            eat();
            expect(']');
            eat();
            expect(',');
            eat();
            expect('A');
            eat();
            if (emit) {
                emit8(0xF2);
            }
            addPC(1);
            return;
        }
        view = exprEatPos(&pos);
        expect(']');
        eat();
        expect(',');
        eat();
        expect('A');
        eat();
        if (emit) {
            emit8(0xE0);
            if (exprSolve(view, &num)) {
                if ((num < 0xFF00) || (num > 0xFFFF)) {
                    fatalPos(pos, "address not in high memory: $%08X\n", num);
                }
                emit8(num & 0x00FF);
            } else {
                emit8(0xFD);
                reloc(1, 1, view, pos, SM_RELOC_HRAM);
            }
        }
        addPC(2);
        return;
    case MNE_PUSH:
        pushPop(0xC5);
        return;
    case MNE_POP:
        pushPop(0xC1);
        return;
    case MNE_ADD:
        eat();
        switch (peek()) {
        case SM_TOK_HL:
            eat();
            expect(',');
            eat();
            if (!reg16OffsetSP(peek(), 0x09, &op)) {
                fatal("illegal operand\n");
            }
            eat();
            if (emit) {
                emit8(op);
            }
            addPC(1);
            return;
        case SM_TOK_SP:
            eat();
            expect(',');
            eat();
            view = exprEatPos(&pos);
            if (emit) {
                emit8(0xE8);
                if (exprSolve(view, &num)) {
                    expectReprU8(pos, num);
                    emit8(num);
                } else {
                    emit8(0xFD);
                    reloc(1, 1, view, pos, 0);
                }
            }
            addPC(2);
            return;
        default:
            expect('A');
            aluReg8(0x80, 0xC6);
            return;
        }
    case MNE_ADC:
        eat();
        expect('A');
        aluReg8(0x88, 0xCE);
        return;
    case MNE_SUB:
        eat();
        expect('A');
        aluReg8(0x90, 0xD6);
        return;
    case MNE_SBC:
        eat();
        expect('A');
        aluReg8(0x98, 0xDE);
        return;
    case MNE_AND:
        eat();
        expect('A');
        aluReg8(0xA0, 0xE6);
        return;
    case MNE_XOR:
        eat();
        expect('A');
        aluReg8(0xA8, 0xEE);
        return;
    case MNE_OR:
        eat();
        expect('A');
        aluReg8(0xB0, 0xF6);
        return;
    case MNE_CP:
        eat();
        expect('A');
        aluReg8(0xB8, 0xFE);
        return;
    case MNE_INC:
        eat();
        if (reg16OffsetSP(peek(), 0x03, &op)) {
            eat();
            if (emit) {
                emit8(op);
            }
            addPC(1);
            return;
        }
        switch (peek()) {
        case 'B':
            op = 0x04;
            break;
        case 'C':
            op = 0x0C;
            break;
        case 'D':
            op = 0x14;
            break;
        case 'E':
            op = 0x1C;
            break;
        case 'H':
            op = 0x24;
            break;
        case 'L':
            op = 0x2C;
            break;
        case '[':
            eat();
            expect(SM_TOK_HL);
            eat();
            expect(']');
            op = 0x34;
            break;
        case 'A':
            op = 0x3C;
            break;
        default:
            fatal("illegal operand\n");
        }
        eat();
        if (emit) {
            emit8(op);
        }
        addPC(1);
        return;
    case MNE_DEC:
        eat();
        if (reg16OffsetSP(peek(), 0x0B, &op)) {
            eat();
            if (emit) {
                emit8(op);
            }
            addPC(1);
            return;
        }
        switch (peek()) {
        case 'B':
            op = 0x05;
            break;
        case 'C':
            op = 0x0D;
            break;
        case 'D':
            op = 0x15;
            break;
        case 'E':
            op = 0x1D;
            break;
        case 'H':
            op = 0x25;
            break;
        case 'L':
            op = 0x2D;
            break;
        case '[':
            eat();
            expect(SM_TOK_HL);
            eat();
            expect(']');
            op = 0x35;
            break;
        case 'A':
            op = 0x3D;
            break;
        default:
            fatal("illegal operand\n");
        }
        eat();
        if (emit) {
            emit8(op);
        }
        addPC(1);
        return;
    case MNE_DAA:
        eat();
        if (emit) {
            emit8(0x27);
        }
        addPC(1);
        return;
    case MNE_CPL:
        eat();
        if (emit) {
            emit8(0x2F);
        }
        addPC(1);
        return;
    case MNE_CCF:
        eat();
        if (emit) {
            emit8(0x3F);
        }
        addPC(1);
        return;
    case MNE_SCF:
        eat();
        if (emit) {
            emit8(0x37);
        }
        addPC(1);
        return;
    case MNE_NOP:
        eat();
        if (emit) {
            emit8(0x00);
        }
        addPC(1);
        return;
    case MNE_HALT:
        eat();
        if (emit) {
            emit8(0x76);
            emit8(0x00);
        }
        addPC(2);
        return;
    case MNE_STOP:
        eat();
        if (emit) {
            emit8(0x10);
            emit8(0x00);
        }
        addPC(2);
        return;
    case MNE_DI:
        eat();
        if (emit) {
            emit8(0xF3);
        }
        addPC(1);
        return;
    case MNE_EI:
        eat();
        if (emit) {
            emit8(0xFB);
        }
        addPC(1);
        return;
    case MNE_RETI:
        eat();
        if (emit) {
            emit8(0xD9);
        }
        addPC(1);
        return;
    case MNE_RLCA:
        eat();
        if (emit) {
            emit8(0x07);
        }
        addPC(1);
        return;
    case MNE_RLA:
        eat();
        if (emit) {
            emit8(0x17);
        }
        addPC(1);
        return;
    case MNE_RRCA:
        eat();
        if (emit) {
            emit8(0x0F);
        }
        addPC(1);
        return;
    case MNE_RRA:
        eat();
        if (emit) {
            emit8(0x1F);
        }
        addPC(1);
        return;
    case MNE_RLC:
        doAluReg8Cb(0x00);
        return;
    case MNE_RRC:
        doAluReg8Cb(0x08);
        return;
    case MNE_RL:
        doAluReg8Cb(0x10);
        return;
    case MNE_RR:
        doAluReg8Cb(0x18);
        return;
    case MNE_SLA:
        doAluReg8Cb(0x20);
        return;
    case MNE_SRA:
        doAluReg8Cb(0x28);
        return;
    case MNE_SWAP:
        doAluReg8Cb(0x30);
        return;
    case MNE_SRL:
        doAluReg8Cb(0x38);
        return;
    case MNE_BIT:
        doBitCb(0x40);
        return;
    case MNE_RES:
        doBitCb(0x80);
        return;
    case MNE_SET:
        doBitCb(0xC0);
        return;
    case MNE_JP:
        eat();
        if (flag(peek(), 0xC2, &op)) {
            eat();
            expect(',');
            eat();
            view = exprEatPos(&pos);
            if (emit) {
                emit8(op);
                if (exprSolve(view, &num)) {
                    expectReprU16(pos, num);
                    emit16(num);
                } else {
                    emit16(0xFDFD);
                    reloc(1, 2, view, pos, SM_RELOC_JP);
                }
            }
            addPC(3);
            return;
        }
        if (peek() == SM_TOK_HL) {
            eat();
            if (emit) {
                emit8(0xE9);
            }
            addPC(1);
            return;
        }
        view = exprEatPos(&pos);
        if (emit) {
            emit8(0xC3);
            if (exprSolve(view, &num)) {
                expectReprU16(pos, num);
            } else {
                emit16(0xFDFD);
                reloc(1, 2, view, pos, SM_RELOC_JP);
            }
        }
        addPC(3);
        return;
    case MNE_JR:
        eat();
        if (flag(peek(), 0x20, &op)) {
            eat();
            expect(',');
            eat();
            view = exprEatPos(&pos);
            if (emit) {
                emit8(op);
                if (!exprSolveRelative(view, &num)) {
                    fatalPos(pos, "branch distance must be constant\n");
                }
                I32 offset = num - ((I32)(U32)getPC()) - 2;
                if (!exprCanReprI8(offset)) {
                    fatalPos(pos, "branch distance too far\n");
                }
                emit8(offset);
            }
            addPC(2);
            return;
        }
        view = exprEatPos(&pos);
        if (emit) {
            emit8(0x18);
            if (!exprSolveRelative(view, &num)) {
                fatalPos(pos, "branch distance must be constant\n");
            }
            I32 offset = num - ((I32)(U32)getPC()) - 2;
            if (!exprCanReprI8(offset)) {
                fatalPos(pos, "branch distance too far\n");
            }
            emit8(offset);
        }
        addPC(2);
        return;
    case MNE_CALL:
        eat();
        if (flag(peek(), 0xC4, &op)) {
            eat();
            expect(',');
            eat();
            view = exprEatPos(&pos);
            if (emit) {
                emit8(op);
                if (exprSolve(view, &num)) {
                    expectReprU16(pos, num);
                    emit16(num);
                } else {
                    emit16(0xFDFD);
                    reloc(1, 2, view, pos, SM_RELOC_JP);
                }
            }
            addPC(3);
            return;
        }
        view = exprEatPos(&pos);
        if (emit) {
            emit8(0xCD);
            if (exprSolve(view, &num)) {
                expectReprU16(pos, num);
                emit16(num);
            } else {
                emit16(0xFDFD);
                reloc(1, 2, view, pos, SM_RELOC_JP);
            }
        }
        addPC(3);
        return;
    case MNE_RET:
        eat();
        if (flag(peek(), 0xC0, &op)) {
            eat();
            if (emit) {
                emit8(op);
            }
            addPC(1);
            return;
        }
        if (emit) {
            emit8(0xC9);
        }
        addPC(1);
        return;
    case MNE_RST:
        eat();
        view = exprEatPos(&pos);
        if (emit) {
            if (exprSolve(view, &num)) {
                switch (num) {
                case 0x00:
                    break;
                case 0x08:
                    break;
                case 0x10:
                    break;
                case 0x18:
                    break;
                case 0x20:
                    break;
                case 0x28:
                    break;
                case 0x30:
                    break;
                case 0x38:
                    break;
                default:
                    fatalPos(pos, "illegal reset vector: $%08X\n", num);
                }
                emit8(0xC7 + num);
            } else {
                emit8(0xFD);
                reloc(0, 1, view, pos, SM_RELOC_RST);
            }
        }
        addPC(1);
        return;
    default:
        SM_UNREACHABLE();
    }
}

static FILE *openFile(SmView path, char const *modes) {
    static SmBuf buf = {0};
    buf.view.len     = 0;
    smBufCat(&buf, path);
    smBufCat(&buf, SM_VIEW("\0"));
    return openFileCstr((char const *)buf.view.bytes, modes);
}

static Bool fileExists(SmView path) {
    static SmBuf buf = {0};
    buf.view.len     = 0;
    smBufCat(&buf, path);
    smBufCat(&buf, SM_VIEW("\0"));
    FILE *hnd = fopen((char const *)buf.view.bytes, "rb");
    if (hnd) {
        closeFile(hnd);
        return true;
    }
    return false;
}

static SmView findInclude(SmView path) {
    static SmBuf buf      = {0};
    SmView       fullpath = smPathIntern(&STRS, path);
    if (!fileExists(fullpath)) {
        for (UInt i = 0; i < IPATHS.bufs.view.len; ++i) {
            SmView inc   = IPATHS.bufs.view.items[i];
            buf.view.len = 0;
            smBufCat(&buf, inc);
            smBufCat(&buf, SM_VIEW("/"));
            smBufCat(&buf, path);
            fullpath = smPathIntern(&STRS, buf.view);
            if (fileExists(fullpath)) {
                return fullpath;
            }
        }
    }
    fatal("could not find include file: " SM_VIEW_FMT "\n",
          SM_VIEW_FMT_ARG(path));
}

static SmExprView addrExprBuf(SmView section, U16 offset) {
    return smExprIntern(
        &EXPRS,
        (SmExprView){&(SmExpr){.kind = SM_EXPR_ADDR, .addr = {section, offset}},
                     1});
}

static SmExprView constExprBuf(I32 num) {
    return smExprIntern(
        &EXPRS, (SmExprView){&(SmExpr){.kind = SM_EXPR_CONST, .num = num}, 1});
}

static void eatDirective() {
    SmPos      pos;
    SmExprView view;
    I32        num;
    switch (peek()) {
    case SM_TOK_DB:
        eat();
        while (true) {
            switch (peek()) {
            case SM_TOK_STR:
                if (emit) {
                    emitView(tokView());
                }
                addPC(tokView().len);
                eat();
                break;
            default: {
                view = exprEatPos(&pos);
                if (emit) {
                    if (exprSolve(view, &num)) {
                        expectReprU8(pos, num);
                        emit8(num);
                    } else {
                        emit8(0xFD);
                        reloc(0, 1, view, pos, 0);
                    }
                }
                addPC(1);
            }
            }
            if (peek() != ',') {
                break;
            }
            eat();
        }
        expectEOL();
        eat();
        return;
    case SM_TOK_DW:
        eat();
        while (true) {
            view = exprEatPos(&pos);
            if (emit) {
                if (exprSolve(view, &num)) {
                    expectReprU16(pos, num);
                    emit16(num);
                } else {
                    emit16(0xFDFD);
                    reloc(0, 2, view, pos, 0);
                }
            }
            addPC(2);
            if (peek() != ',') {
                break;
            }
            eat();
        }
        expectEOL();
        eat();
        return;
    case SM_TOK_DS: {
        eat();
        U16 space = exprEatSolvedU16();
        if (emit) {
            for (UInt i = 0; i < space; ++i) {
                emit8(0x00);
            }
        }
        addPC(space);
        expectEOL();
        eat();
        return;
    }
    case SM_TOK_SECTION:
        eat();
        expect(SM_TOK_STR);
        sectSet(intern(tokView()));
        eat();
        expectEOL();
        eat();
        return;
    case SM_TOK_SECTPUSH:
        eat();
        expect(SM_TOK_STR);
        sectPush(intern(tokView()));
        eat();
        expectEOL();
        eat();
        return;
    case SM_TOK_SECTPOP:
        eat();
        sectPop();
        expectEOL();
        eat();
        return;
    case SM_TOK_INCLUDE: {
        eat();
        expect(SM_TOK_STR);
        SmView path = findInclude(tokView());
        eat();
        expectEOL();
        eat();
        pushFile(path);
        smPathSetAdd(&INCS, path);
        return;
    }
    case SM_TOK_INCBIN: {
        static SmBuf buf = {0};
        buf.view.len     = 0;
        eat();
        expect(SM_TOK_STR);
        SmView path = findInclude(tokView());
        eat();
        expectEOL();
        eat();
        FILE   *hnd = openFile(path, "rb");
        SmSerde ser = {hnd, path};
        smDeserializeToEnd(&ser, &buf);
        if (emit) {
            emitView(buf.view);
        }
        addPC(buf.view.len);
        smPathSetAdd(&INCS, path);
        return;
    }
    case SM_TOK_MACRO: {
        pos                      = tokPos();
        static SmMacroTokBuf buf = {0};
        buf.view.len             = 0;
        eat();
        expect(SM_TOK_ID);
        SmLbl lbl = tokLbl();
        if (!smLblIsGlobal(lbl)) {
            fatal("macro name must be global\n");
        }
        Macro *macro = macroFind(lbl.name);
        if (macro) {
            fatal("macro " SM_VIEW_FMT
                  " already defined\n\toriginally defined at " SM_VIEW_FMT
                  ":" UINT_FMT ":" UINT_FMT "\n",
                  SM_VIEW_FMT_ARG(lbl.name), SM_VIEW_FMT_ARG(macro->pos.file),
                  macro->pos.line, macro->pos.col);
        }
        eat();
        UInt depth = 0;
        streamdef  = true;
        while (true) {
            switch (peek()) {
            case SM_TOK_IF:
            case SM_TOK_MACRO:
            case SM_TOK_REPEAT:
            case SM_TOK_STRUCT:
            case SM_TOK_UNION:
                ++depth;
                break;
            case SM_TOK_END:
                if (depth == 0) {
                    eat();
                    goto macdone;
                }
                --depth;
                break;
            default:
                break;
            }
            switch (peek()) {
            case SM_TOK_EOF:
                fatal("unexpected end of file\n");
            case SM_TOK_ID:
                smMacroTokBufAdd(&buf, (SmMacroTok){.kind = SM_MACRO_TOK_ID,
                                                    .pos  = tokPos(),
                                                    .view = intern(tokView())});
                break;
            case SM_TOK_NUM:
                smMacroTokBufAdd(&buf, (SmMacroTok){.kind = SM_MACRO_TOK_NUM,
                                                    .pos  = tokPos(),
                                                    .num  = tokNum()});
                break;
            case SM_TOK_STR:
                smMacroTokBufAdd(&buf, (SmMacroTok){.kind = SM_MACRO_TOK_STR,
                                                    .pos  = tokPos(),
                                                    .view = intern(tokView())});
                break;
            case SM_TOK_ARG:
                smMacroTokBufAdd(&buf, (SmMacroTok){.kind = SM_MACRO_TOK_ARG,
                                                    .pos  = tokPos(),
                                                    .num  = tokNum()});
                break;
            case SM_TOK_NARG:
                smMacroTokBufAdd(&buf, (SmMacroTok){
                                           .kind = SM_MACRO_TOK_NARG,
                                           .pos  = tokPos(),
                                       });
                break;
            case SM_TOK_SHIFT:
                smMacroTokBufAdd(&buf, (SmMacroTok){
                                           .kind = SM_MACRO_TOK_SHIFT,
                                           .pos  = tokPos(),
                                       });
                break;
            case SM_TOK_UNIQUE:
                smMacroTokBufAdd(&buf, (SmMacroTok){
                                           .kind = SM_MACRO_TOK_UNIQUE,
                                           .pos  = tokPos(),
                                       });
                break;
            default:
                smMacroTokBufAdd(&buf, (SmMacroTok){.kind = SM_MACRO_TOK_TOK,
                                                    .pos  = tokPos(),
                                                    .tok  = peek()});
                break;
            }
            eat();
        }
    macdone:
        streamdef = false;
        macroAdd(lbl.name, pos, buf.view);
        return;
    }
    case SM_TOK_REPEAT: {
        SmPos start = tokPos();
        eat();
        num = exprEatSolvedPos(&pos);
        if (num < 0) {
            fatalPos(pos, "repeat count must be positive\n");
        }
        SmLbl lbl = SM_LBL_NULL;
        if (peek() == ',') {
            eat();
            expect(SM_TOK_ID);
            lbl = tokLbl();
            if (!smLblIsGlobal(lbl)) {
                fatal("variable name must be global\n");
            }
            // TODO should I check if this shadows an existing label?
            eat();
        }
        UInt           depth = 0;
        SmRepeatTokBuf buf   = {0};
        streamdef            = true;
        while (true) {
            switch (peek()) {
            case SM_TOK_IF:
            case SM_TOK_MACRO:
            case SM_TOK_REPEAT:
            case SM_TOK_STRUCT:
            case SM_TOK_UNION:
                ++depth;
                break;
            case SM_TOK_END:
                if (depth == 0) {
                    eat();
                    goto rptdone;
                }
                --depth;
                break;
            default:
                break;
            }
            switch (peek()) {
            case SM_TOK_EOF:
                fatal("unexpected end of file\n");
            case SM_TOK_ID:
                // referencing the variable
                if (smViewEqual(tokView(), lbl.name)) {
                    smRepeatTokBufAdd(&buf,
                                      (SmRepeatTok){.kind = SM_REPEAT_TOK_ITER,
                                                    .pos  = tokPos()});
                    break;
                }
                smRepeatTokBufAdd(&buf,
                                  (SmRepeatTok){.kind = SM_REPEAT_TOK_ID,
                                                .pos  = tokPos(),
                                                .view = intern(tokView())});
                break;
            case SM_TOK_NUM:
                smRepeatTokBufAdd(&buf, (SmRepeatTok){.kind = SM_REPEAT_TOK_NUM,
                                                      .pos  = tokPos(),
                                                      .num  = tokNum()});
                break;
            case SM_TOK_STR:
                smRepeatTokBufAdd(&buf,
                                  (SmRepeatTok){.kind = SM_REPEAT_TOK_STR,
                                                .pos  = tokPos(),
                                                .view = intern(tokView())});
                break;
            default:
                smRepeatTokBufAdd(&buf, (SmRepeatTok){.kind = SM_REPEAT_TOK_TOK,
                                                      .pos  = tokPos(),
                                                      .tok  = peek()});
                break;
            }
            eat();
        }
    rptdone:
        streamdef = false;
        ++ts;
        if (ts >= (STACK + STACK_SIZE)) {
            smFatal("too many open files\n");
        }
        smTokStreamRepeatInit(ts, start, buf, num);
        return;
    }
    case SM_TOK_STRUCT: {
        SmPos start = tokPos();
        eat();
        expect(SM_TOK_ID);
        SmLbl lbl = tokLbl();
        if (!smLblIsGlobal(lbl)) {
            fatal("structure name must be global\n");
        }
        // TODO check if this struct is already defined
        eat();
        UInt      size      = 0;
        SmViewBuf fields    = {0};
        Bool      inunion   = false;
        UInt      unionsize = 0;
        while (true) {
            switch (peek()) {
            case '\n':
                eat();
                continue;
            case SM_TOK_UNION:
                if (inunion) {
                    fatal("a @UNION is already being defined\n");
                }
                inunion   = true;
                unionsize = 0;
                eat();
                continue;
            case SM_TOK_END:
                eat();
                if (inunion) {
                    inunion = false;
                    size += unionsize;
                    eat();
                    continue;
                }
                goto structdone;
            default:
                break;
            }
            expect(SM_TOK_ID);
            if (!smViewStartsWith(tokView(), SM_VIEW("."))) {
                fatal("structure field name must be local\n");
            }
            SmLbl fieldlbl = tokLbl();
            if (smViewEqual(fieldlbl.name, SM_VIEW("SIZE"))) {
                fatal("structure field name cannot be `.SIZE`\n");
            }
            pos = tokPos();
            eat();
            fieldlbl.scope = lbl.name;
            expect(':');
            eat();
            num = exprEatSolvedU16();
            if (!emit) {
                // TODO should probably check for redefinition with different
                // values
                smViewBufAdd(&fields, fieldlbl.name);
                smSymTabAdd(&SYMS, (SmSym){.lbl     = fieldlbl,
                                           .value   = constExprBuf(size),
                                           .unit    = STATIC_UNIT,
                                           .section = DEFINES_SECTION,
                                           .pos     = pos,
                                           .flags   = SM_SYM_EQU});
            }
            if (!inunion) {
                size += num;
            } else {
                unionsize = uIntMax(unionsize, num);
            }
            expectEOL();
            eat();
        }
    structdone:
        if (!emit) {
            SmLbl sizelbl = lblAbs(lbl.name, intern(SM_VIEW("SIZE")));
            structAdd(lbl.name, pos, fields);
            smSymTabAdd(&SYMS, (SmSym){.lbl     = sizelbl,
                                       .value   = constExprBuf(size),
                                       .unit    = STATIC_UNIT,
                                       .section = DEFINES_SECTION,
                                       .pos     = start,
                                       .flags   = SM_SYM_EQU});
        }
        expectEOL();
        eat();
        return;
    }
    case SM_TOK_ALLOC: {
        pos = tokPos();
        eat();
        if (smViewEqual(scope, SM_VIEW_NULL)) {
            fatal("@ALLOC must be used under a global label\n");
        }
        SmSym *scopesym = smSymTabFind(&SYMS, lblGlobal(scope));
        assert(scopesym);
        assert(scopesym->value.len == 1);
        SmExpr *scopeexpr = scopesym->value.items;
        assert(scopeexpr->kind == SM_EXPR_ADDR);
        I32 base = scopeexpr->addr.pc;
        expect(SM_TOK_ID);
        SmView  name  = intern(tokView());
        Struct *strct = structFind(name);
        if (!strct) {
            fatal("structure " SM_VIEW_FMT " not found\n",
                  SM_VIEW_FMT_ARG(name));
        }

        eat();
        if (!emit) {
            for (UInt i = 0; i < strct->fields.view.len; ++i) {
                SmView field = strct->fields.view.items[i];
                SmLbl  lbl   = lblAbs(name, field);
                SmSym *sym   = smSymTabFind(&SYMS, lbl);
                assert(sym);
                assert(exprSolve(sym->value, &num));
                smSymTabAdd(&SYMS, (SmSym){.lbl   = lblAbs(scope, field),
                                           .value = addrExprBuf(
                                               scopesym->section, base + num),
                                           .unit    = scopesym->unit,
                                           .section = scopesym->section,
                                           .pos     = pos,
                                           .flags   = 0});
            }
        }
        SmView size = intern(SM_VIEW("SIZE"));
        SmLbl  lbl  = lblAbs(name, size);
        SmSym *sym  = smSymTabFind(&SYMS, lbl);
        assert(sym);
        assert(exprSolve(sym->value, &num));
        addPC(num);
        if (!emit) {
            smSymTabAdd(&SYMS, (SmSym){.lbl     = lblAbs(scope, size),
                                       .value   = sym->value,
                                       .unit    = scopesym->unit,
                                       .section = scopesym->section,
                                       .pos     = pos,
                                       .flags   = SM_SYM_EQU});
        }
        expectEOL();
        eat();
        return;
    }
    case SM_TOK_FATAL:
        fmtInvoke(SM_TOK_STR);
        expect(SM_TOK_STR);
        fatal("explicit fatal error: " SM_VIEW_FMT, SM_VIEW_FMT_ARG(tokView()));
    case SM_TOK_PRINT:
        fmtInvoke(SM_TOK_STR);
        expect(SM_TOK_STR);
        if (emit) {
            fprintf(stderr, SM_VIEW_FMT, SM_VIEW_FMT_ARG(tokView()));
        }
        eat();
        expectEOL();
        eat();
        return;
    default: {
        SmView name = smTokName(peek());
        fatal("unexpected: " SM_VIEW_FMT "\n", SM_VIEW_FMT_ARG(name));
    }
    }
}

static void pass() {
    sectSet(CODE_SECTION);
    while (peek() != SM_TOK_EOF) {
        switch (peek()) {
        case '\n':
            // skip newlines
            eat();
            continue;
        case '*':
            // setting the _relative_ PC: * = $1234
            eat();
            expect('=');
            eat();
            setPC(exprEatSolvedU16());
            expectEOL();
            eat();
            continue;
        case SM_TOK_ID: {
            U8 const *mne = mneFind(tokView());
            if (mne) {
                eatMne(*mne);
                expectEOL();
                eat();
                continue;
            }
            SmPos pos = tokPos();
            SmLbl lbl = tokLbl();
            eat();
            SmSym *sym = smSymTabFind(&SYMS, lbl);
            if (!sym) {
                // create a placeholder symbol that we'll fill in soon
                sym = smSymTabAdd(&SYMS, (SmSym){
                                             .lbl     = lbl,
                                             .value   = constExprBuf(0),
                                             .unit    = STATIC_UNIT,
                                             .section = sectGet()->name,
                                             .pos     = pos,
                                             .flags   = 0,
                                         });
            } else if (!emit) {
                // we are allowed to redefine labels for the second
                // pass.
                // TODO: but we need to ensure the value of the label
                // never changes.
                // TODO: also we want to create weak/redefinable symbols
                fatalPos(pos,
                         "symbol already defined\n\t" SM_VIEW_FMT ":" UINT_FMT
                         ":" UINT_FMT " : defined previously here\n",
                         SM_VIEW_FMT_ARG(sym->pos.file), sym->pos.line,
                         sym->pos.col);
            }
            switch (peek()) {
            case SM_TOK_DCOLON:
                sym->unit = EXPORT_UNIT;
                // fall through
            case ':':
                eat();
                break;
            case SM_TOK_EXPEQU:
                sym->unit = EXPORT_UNIT;
                // fall through
            case '=':
                eat();
                I32 num;
                if (emit) {
                    sym->value = constExprBuf(exprEatSolvedPos(&pos));
                    sym->flags = SM_SYM_EQU;
                } else if (exprSolve(exprEat(), &num)) {
                    sym->value = constExprBuf(num);
                    sym->flags = SM_SYM_EQU;
                } else {
                    sym->lbl = SM_LBL_NULL;
                }
                expectEOL();
                eat();
                continue;
            default:
                if (!smLblIsGlobal(lbl)) {
                    fatal("expected `:` or `=`\n");
                }
                fatalPos(pos, "unrecognized instruction\n");
            }
            // This just a new label then
            if (smLblIsGlobal(sym->lbl)) {
                scope = sym->lbl.name;
            }
            sym->value = addrExprBuf(sectGet()->name, getPC());
            continue;
        }
        default:
            eatDirective();
        }
    }
}

static void writeDepend() {
    static SmBuf buf = {0};
    buf.view.len     = 0;
    if (!depfile_name) {
        UInt        len    = strlen(infile_name);
        char const *offset = strchr(infile_name, '.');
        if (offset) {
            len -= (offset - infile_name);
        }
        smBufCat(&buf, (SmView){(U8 *)infile_name, len});
        smBufCat(&buf, SM_VIEW(".d"));
    } else {
        smBufCat(&buf, (SmView){(U8 *)depfile_name, strlen(depfile_name)});
    }
    FILE   *hnd = openFile(buf.view, "wb+");
    SmSerde ser = {hnd, buf.view};
    smSerializeView(&ser, (SmView){(U8 *)outfile_name, strlen(outfile_name)});
    smSerializeView(&ser, SM_VIEW(": \\\n"));
    for (UInt i = 0; i < INCS.bufs.view.len; ++i) {
        smSerializeView(&ser, SM_VIEW("  "));
        smSerializeView(&ser, INCS.bufs.view.items[i]);
        smSerializeView(&ser, SM_VIEW(" \\\n"));
    }
    closeFile(hnd);
}

static void serialize() {
    SmSerde ser = {outfile, {(U8 *)outfile_name, strlen(outfile_name)}};
    smSerializeU32(&ser, *(U32 *)"SM00");
    smSerializeViewIntern(&ser, &STRS);
    smSerializeExprIntern(&ser, &EXPRS, &STRS);
    smSerializeSymTab(&ser, &SYMS, &STRS, &EXPRS);
    smSerializeSectView(&ser, SECTS.view, &STRS, &EXPRS);
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

static void pushFile(SmView path) {
    FILE *hnd = openFile(path, "rb");
    ++ts;
    if (ts >= (STACK + STACK_SIZE)) {
        smFatal("too many open files\n");
    }
    smTokStreamFileInit(ts, path, hnd);
}
