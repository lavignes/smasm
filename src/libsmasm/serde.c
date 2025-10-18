#include <smasm/fatal.h>
#include <smasm/serde.h>

#include <stdlib.h>
#include <string.h>

static _Noreturn void fatal(SmSerde const *ser, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, SM_VIEW_FMT ": ", SM_VIEW_FMT_ARG(ser->name));
    smFatalV(fmt, args);
}

void smSerializeU8(SmSerde *ser, U8 byte) {
    if (fwrite(&byte, 1, 1, ser->hnd) != 1) {
        int err = ferror(ser->hnd);
        fatal(ser, "failed to write file: %s\n", strerror(err));
    }
}

void smSerializeU16(SmSerde *ser, U16 word) {
    if (fwrite(&word, 1, sizeof(U16), ser->hnd) != sizeof(U16)) {
        int err = ferror(ser->hnd);
        fatal(ser, "failed to write file: %s\n", strerror(err));
    }
}

void smSerializeU32(SmSerde *ser, U32 num) {
    if (fwrite(&num, 1, sizeof(U32), ser->hnd) != sizeof(U32)) {
        int err = ferror(ser->hnd);
        fatal(ser, "failed to write file: %s\n", strerror(err));
    }
}

void smSerializeView(SmSerde *ser, SmView view) {
    if (fwrite(view.bytes, 1, view.len, ser->hnd) != view.len) {
        int err = ferror(ser->hnd);
        fatal(ser, "failed to write file: %s\n", strerror(err));
    }
}

void smSerializeViewIntern(SmSerde *ser, SmViewIntern const *in) {
    UInt len = 0;
    for (UInt i = 0; i < in->len; ++i) {
        len += in->bufs[i].view.len;
    }
    smSerializeU32(ser, len);
    for (UInt i = 0; i < in->len; ++i) {
        smSerializeView(ser, in->bufs[i].view);
    }
}

static UInt totalViewOffset(SmViewIntern const *in, SmView view) {
    UInt total = 0;
    for (UInt i = 0; i < in->len; ++i) {
        SmBuf *buf = in->bufs + i;
        U8    *offset =
            memmem(buf->view.bytes, buf->view.len, view.bytes, view.len);
        if (offset == NULL) {
            total += buf->view.len;
            continue;
        }
        total += offset - buf->view.bytes;
        break;
    }
    return total;
}

static void writeViewRef(SmSerde *ser, SmViewIntern const *in, SmView view) {
    smSerializeU32(ser, totalViewOffset(in, view));
    smSerializeU16(ser, view.len);
}

static void writeLbl(SmSerde *ser, SmViewIntern const *in, SmLbl lbl) {
    if (!smLblIsGlobal(lbl)) {
        smSerializeU8(ser, 0);
        writeViewRef(ser, in, lbl.scope);
    } else {
        smSerializeU8(ser, 1);
    }
    writeViewRef(ser, in, lbl.name);
}

void smSerializeExprIntern(SmSerde *ser, SmExprIntern const *in,
                           SmViewIntern const *strin) {
    UInt len = 0;
    for (UInt i = 0; i < in->len; ++i) {
        len += in->bufs[i].view.len;
    }
    smSerializeU32(ser, len);
    for (UInt i = 0; i < in->len; ++i) {
        SmExprBuf *buf = in->bufs + i;
        for (UInt j = 0; j < buf->view.len; ++j) {
            SmExpr *expr = buf->view.items + j;
            smSerializeU8(ser, expr->kind);
            switch (expr->kind) {
            case SM_EXPR_CONST:
                smSerializeU32(ser, expr->num);
                break;
            case SM_EXPR_ADDR:
                writeViewRef(ser, strin, expr->addr.sect);
                smSerializeU16(ser, expr->addr.pc);
                break;
            case SM_EXPR_OP:
                smSerializeU32(ser, expr->op.tok);
                smSerializeU8(ser, expr->op.unary);
                break;
            case SM_EXPR_LABEL:
            case SM_EXPR_REL:
                writeLbl(ser, strin, expr->lbl);
                break;
            case SM_EXPR_TAG:
                writeLbl(ser, strin, expr->tag.lbl);
                writeViewRef(ser, strin, expr->tag.name);
                break;
            default:
                SM_UNREACHABLE();
            }
        }
    }
}

static UInt totalExprBufOffset(SmExprIntern const *in, SmExprView view) {
    UInt total = 0;
    for (UInt i = 0; i < in->len; ++i) {
        SmExprBuf *buf = in->bufs + i;
        SmExpr *offset = memmem(buf->view.items, sizeof(SmExpr) * buf->view.len,
                                view.items, sizeof(SmExpr) * view.len);
        if (offset == NULL) {
            total += buf->view.len;
            continue;
        }
        total += offset - buf->view.items;
        break;
    }
    return total;
}

static void writeExprBufRef(SmSerde *ser, SmExprIntern const *in,
                            SmExprView buf) {
    smSerializeU32(ser, totalExprBufOffset(in, buf));
    smSerializeU16(ser, buf.len);
}

void smSerializeSymTab(SmSerde *ser, SmSymTab const *tab,
                       SmViewIntern const *strin, SmExprIntern const *exprin) {
    smSerializeU32(ser, tab->len);
    for (UInt i = 0; i < tab->cap; ++i) {
        SmSym *sym = tab->syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        writeLbl(ser, strin, sym->lbl);
        writeExprBufRef(ser, exprin, sym->value);
        writeViewRef(ser, strin, sym->unit);
        writeViewRef(ser, strin, sym->section);
        writeViewRef(ser, strin, sym->pos.file);
        smSerializeU16(ser, sym->pos.line);
        smSerializeU16(ser, sym->pos.col);
        smSerializeU8(ser, sym->flags);
    }
}

void smSerializeSectView(SmSerde *ser, SmSectView buf,
                         SmViewIntern const *strin,
                         SmExprIntern const *exprin) {
    UInt len = 0;
    for (UInt i = 0; i < buf.len; ++i) {
        // dont write empty sections
        if (buf.items[i].data.view.len == 0) {
            continue;
        }
        ++len;
    }
    smSerializeU32(ser, len);
    for (UInt i = 0; i < buf.len; ++i) {
        SmSect *sect = buf.items + i;
        // dont write empty sections
        if (sect->data.view.len == 0) {
            continue;
        }
        writeViewRef(ser, strin, sect->name);
        smSerializeU32(ser, sect->data.view.len);
        smSerializeView(ser, sect->data.view);
        smSerializeU32(ser, sect->relocs.view.len);
        for (UInt j = 0; j < sect->relocs.view.len; ++j) {
            SmReloc *reloc = sect->relocs.view.items + j;
            smSerializeU16(ser, reloc->offset);
            smSerializeU8(ser, reloc->width);
            writeExprBufRef(ser, exprin, reloc->value);
            writeViewRef(ser, strin, reloc->unit);
            writeViewRef(ser, strin, reloc->pos.file);
            smSerializeU16(ser, reloc->pos.line);
            smSerializeU16(ser, reloc->pos.col);
            smSerializeU8(ser, reloc->flags);
        }
    }
}

U8 smDeserializeU8(SmSerde *ser) {
    U8 num;
    if (fread(&num, 1, 1, ser->hnd) != 1) {
        int err = ferror(ser->hnd);
        if (err) {
            fatal(ser, "failed to read file: %s\n", strerror(err));
        }
        if (feof(ser->hnd)) {
            fatal(ser, "unexpected end of file\n");
        }
    }
    return num;
}

U16 smDeserializeU16(SmSerde *ser) {
    U16 num;
    if (fread(&num, 1, sizeof(U16), ser->hnd) != sizeof(U16)) {
        int err = ferror(ser->hnd);
        if (err) {
            fatal(ser, "failed to read file: %s\n", strerror(err));
        }
        if (feof(ser->hnd)) {
            fatal(ser, "unexpected end of file\n");
        }
    }
    return num;
}

U32 smDeserializeU32(SmSerde *ser) {
    U32 num;
    if (fread(&num, 1, sizeof(U32), ser->hnd) != sizeof(U32)) {
        int err = ferror(ser->hnd);
        if (err) {
            fatal(ser, "failed to read file: %s\n", strerror(err));
        }
        if (feof(ser->hnd)) {
            fatal(ser, "unexpected end of file\n");
        }
    }
    return num;
}

void smDeserializeView(SmSerde *ser, SmView *view) {
    if (fread(view->bytes, 1, view->len, ser->hnd) != view->len) {
        int err = ferror(ser->hnd);
        if (err) {
            fatal(ser, "failed to read file: %s\n", strerror(err));
        }
        if (feof(ser->hnd)) {
            fatal(ser, "unexpected end of file\n");
        }
    }
}

SmViewIntern smDeserializeViewIntern(SmSerde *ser) {
    static SmBuf buf = {0};
    UInt         len = smDeserializeU32(ser);
    if (!buf.view.bytes) {
        buf.view.bytes = malloc(len);
        if (!buf.view.bytes) {
            smFatal("out of memory\n");
        }
        buf.cap = len;
    }
    if (len > buf.cap) {
        buf.view.bytes = realloc(buf.view.bytes, len);
        if (!buf.view.bytes) {
            smFatal("out of memory\n");
        }
        buf.cap = len;
    }
    buf.view.len = len;
    smDeserializeView(ser, &buf.view);
    SmViewIntern in = {0};
    smViewIntern(&in, buf.view);
    return in;
}

static SmView readViewRef(SmSerde *ser, SmViewIntern const *in) {
    UInt offset = smDeserializeU32(ser);
    UInt len    = smDeserializeU16(ser);
    return (SmView){in->bufs[0].view.bytes + offset, len};
}

static SmLbl readLbl(SmSerde *ser, SmViewIntern const *in) {
    SmLbl lbl = {0};
    if (smDeserializeU8(ser) == 0) {
        lbl.scope = readViewRef(ser, in);
    }
    lbl.name = readViewRef(ser, in);
    return lbl;
}

SmExprIntern smDeserializeExprIntern(SmSerde *ser, SmViewIntern const *strin) {
    static SmExprBuf buf = {0};
    buf.view.len         = 0;
    UInt len             = smDeserializeU32(ser);
    for (UInt i = 0; i < len; ++i) {
        U8     kind = smDeserializeU8(ser);
        SmExpr expr = {0};
        expr.kind   = kind;
        switch (kind) {
        case SM_EXPR_CONST:
            expr.num = smDeserializeU32(ser);
            break;
        case SM_EXPR_ADDR:
            expr.addr.sect = readViewRef(ser, strin);
            expr.addr.pc   = smDeserializeU16(ser);
            break;
        case SM_EXPR_OP:
            expr.op.tok   = smDeserializeU32(ser);
            expr.op.unary = smDeserializeU8(ser);
            break;
        case SM_EXPR_LABEL:
        case SM_EXPR_REL:
            expr.lbl = readLbl(ser, strin);
            break;
        case SM_EXPR_TAG:
            expr.tag.lbl  = readLbl(ser, strin);
            expr.tag.name = readViewRef(ser, strin);
            break;
        default:
            fatal(ser, "unrecognized expression kind: $%02X\n", kind);
        }
        smExprBufAdd(&buf, expr);
    }
    SmExprIntern in = {0};
    smExprIntern(&in, buf.view);
    return in;
}

static SmExprView readExprBufRef(SmSerde *ser, SmExprIntern const *in) {
    UInt offset = smDeserializeU32(ser);
    UInt len    = smDeserializeU16(ser);
    return (SmExprView){in->bufs[0].view.items + offset, len};
}

SmSymTab smDeserializeSymTab(SmSerde *ser, SmViewIntern const *strin,
                             SmExprIntern const *exprin) {
    SmSymTab tab = {0};
    UInt     len = smDeserializeU32(ser);
    for (UInt i = 0; i < len; ++i) {
        SmSym sym    = {0};
        sym.lbl      = readLbl(ser, strin);
        sym.value    = readExprBufRef(ser, exprin);
        sym.unit     = readViewRef(ser, strin);
        sym.section  = readViewRef(ser, strin);
        sym.pos.file = readViewRef(ser, strin);
        sym.pos.line = smDeserializeU16(ser);
        sym.pos.col  = smDeserializeU16(ser);
        sym.flags    = smDeserializeU8(ser);
        smSymTabAdd(&tab, sym);
    }
    return tab;
}

SmSectBuf smDeserializeSectBuf(SmSerde *ser, SmViewIntern const *strin,
                               SmExprIntern const *exprin) {
    SmSectBuf buf = {0};
    UInt      len = smDeserializeU32(ser);
    for (UInt i = 0; i < len; ++i) {
        SmSect sect          = {0};
        sect.name            = readViewRef(ser, strin);
        UInt len             = smDeserializeU32(ser);
        sect.data.view.bytes = malloc(len);
        if (!sect.data.view.bytes) {
            smFatal("out of memory\n");
        }
        sect.data.cap      = len;
        sect.data.view.len = len;
        smDeserializeView(ser, &sect.data.view);
        len = smDeserializeU32(ser);
        for (UInt j = 0; j < len; ++j) {
            SmReloc reloc  = {0};
            reloc.offset   = smDeserializeU16(ser);
            reloc.width    = smDeserializeU8(ser);
            reloc.value    = readExprBufRef(ser, exprin);
            reloc.unit     = readViewRef(ser, strin);
            reloc.pos.file = readViewRef(ser, strin);
            reloc.pos.line = smDeserializeU16(ser);
            reloc.pos.col  = smDeserializeU16(ser);
            reloc.flags    = smDeserializeU8(ser);
            smRelocBufAdd(&sect.relocs, reloc);
        }
        smSectBufAdd(&buf, sect);
    }
    return buf;
}

void smDeserializeToEnd(SmSerde *ser, SmBuf *buf) {
    static U8 tmp[4096];
    while (true) {
        size_t read = fread(tmp, 1, sizeof(tmp), ser->hnd);
        if (read == sizeof(tmp)) {
            smBufCat(buf, (SmView){tmp, read});
            continue;
        }
        int err = ferror(ser->hnd);
        if (err) {
            fatal(ser, "failed to read file: %s\n", strerror(err));
        }
        if (feof(ser->hnd)) {
            smBufCat(buf, (SmView){tmp, read});
            break;
        }
    }
}
