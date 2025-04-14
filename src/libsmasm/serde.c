#include <smasm/fatal.h>
#include <smasm/serde.h>

#include <stdlib.h>
#include <string.h>

static _Noreturn void fatal(SmSerde *ser, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%.*s: ", (int)ser->name.len, ser->name.bytes);
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

void smSerializeBuf(SmSerde *ser, SmBuf buf) {
    if (fwrite(buf.bytes, 1, buf.len, ser->hnd) != buf.len) {
        int err = ferror(ser->hnd);
        fatal(ser, "failed to write file: %s\n", strerror(err));
    }
}

void smSerializeBufIntern(SmSerde *ser, SmBufIntern const *in) {
    UInt len = 0;
    for (UInt i = 0; i < in->len; ++i) {
        len += in->bufs[i].inner.len;
    }
    smSerializeU32(ser, len);
    for (UInt i = 0; i < in->len; ++i) {
        smSerializeBuf(ser, in->bufs[i].inner);
    }
}

static UInt totalBufOffset(SmBufIntern const *in, SmBuf buf) {
    UInt total = 0;
    for (UInt i = 0; i < in->len; ++i) {
        SmGBuf *gbuf = in->bufs + i;
        U8     *offset =
            memmem(gbuf->inner.bytes, gbuf->inner.len, buf.bytes, buf.len);
        if (offset == NULL) {
            total += gbuf->inner.len;
            continue;
        }
        total += offset - gbuf->inner.bytes;
        break;
    }
    return total;
}

static void writeBufRef(SmSerde *ser, SmBufIntern const *in, SmBuf buf) {
    smSerializeU32(ser, totalBufOffset(in, buf));
    smSerializeU32(ser, buf.len);
}

static void writeLbl(SmSerde *ser, SmBufIntern const *in, SmLbl lbl) {
    if (smLblIsGlobal(lbl)) {
        smSerializeU8(ser, 0);
        writeBufRef(ser, in, lbl.scope);
    } else {
        smSerializeU8(ser, 1);
    }
    writeBufRef(ser, in, lbl.name);
}

void smSerializeExprIntern(SmSerde *ser, SmExprIntern const *in,
                           SmBufIntern const *strin) {
    UInt len = 0;
    for (UInt i = 0; i < in->len; ++i) {
        len += in->bufs[i].inner.len;
    }
    smSerializeU32(ser, len);
    for (UInt i = 0; i < in->len; ++i) {
        SmExprGBuf *gbuf = in->bufs + i;
        for (UInt j = 0; j < gbuf->inner.len; ++j) {
            SmExpr *expr = gbuf->inner.items + j;
            smSerializeU8(ser, expr->kind);
            switch (expr->kind) {
            case SM_EXPR_CONST:
                smSerializeU32(ser, expr->num);
                break;
            case SM_EXPR_ADDR:
                writeBufRef(ser, strin, expr->addr.sect);
                smSerializeU32(ser, expr->addr.pc);
                break;
            case SM_EXPR_OP:
                smSerializeU8(ser, expr->op.tok);
                smSerializeU8(ser, expr->op.unary);
                break;
            case SM_EXPR_LABEL:
            case SM_EXPR_REL:
                writeLbl(ser, strin, expr->lbl);
                break;
            case SM_EXPR_TAG:
                writeLbl(ser, strin, expr->tag.lbl);
                writeBufRef(ser, strin, expr->tag.name);
                break;
            default:
                smUnreachable();
            }
        }
    }
}

static UInt totalExprBufOffset(SmExprIntern const *in, SmExprBuf buf) {
    UInt total = 0;
    for (UInt i = 0; i < in->len; ++i) {
        SmExprGBuf *gbuf = in->bufs + i;
        SmExpr     *offset =
            memmem(gbuf->inner.items, sizeof(SmExpr) * gbuf->inner.len,
                   buf.items, sizeof(SmExpr) * buf.len);
        if (offset == NULL) {
            total += gbuf->inner.len;
            continue;
        }
        total += (offset - gbuf->inner.items) / sizeof(SmExpr);
        break;
    }
    return total;
}

static void writeExprBufRef(SmSerde *ser, SmExprIntern const *in,
                            SmExprBuf buf) {
    smSerializeU32(ser, totalExprBufOffset(in, buf));
    smSerializeU32(ser, buf.len);
}

void smSerializeSymTab(SmSerde *ser, SmSymTab const *tab,
                       SmBufIntern const *strin, SmExprIntern const *exprin) {
    smSerializeU32(ser, tab->len);
    for (UInt i = 0; i < tab->size; ++i) {
        SmSym *sym = tab->syms + i;
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            continue;
        }
        writeLbl(ser, strin, sym->lbl);
        writeExprBufRef(ser, exprin, sym->value);
        writeBufRef(ser, strin, sym->unit);
        writeBufRef(ser, strin, sym->section);
        writeBufRef(ser, strin, sym->pos.file);
        smSerializeU32(ser, sym->pos.line);
        smSerializeU32(ser, sym->pos.col);
        smSerializeU8(ser, sym->flags);
    }
}

void smSerializeSectBuf(SmSerde *ser, SmSectBuf buf, SmBufIntern const *strin,
                        SmExprIntern const *exprin) {
    UInt len = 0;
    for (UInt i = 0; i < buf.len; ++i) {
        // dont write empty sections
        if (buf.items[i].data.inner.len == 0) {
            continue;
        }
        ++len;
    }
    smSerializeU32(ser, len);
    for (UInt i = 0; i < buf.len; ++i) {
        SmSect *sect = buf.items + i;
        // dont write empty sections
        if (sect->data.inner.len == 0) {
            continue;
        }
        writeBufRef(ser, strin, sect->name);
        smSerializeU32(ser, sect->data.inner.len);
        smSerializeBuf(ser, sect->data.inner);
        smSerializeU32(ser, sect->relocs.inner.len);
        for (UInt j = 0; j < sect->relocs.inner.len; ++j) {
            SmReloc *reloc = sect->relocs.inner.items + j;
            smSerializeU32(ser, reloc->offset);
            smSerializeU8(ser, reloc->width);
            writeExprBufRef(ser, exprin, reloc->value);
            writeBufRef(ser, strin, reloc->unit);
            writeBufRef(ser, strin, reloc->pos.file);
            smSerializeU32(ser, reloc->pos.line);
            smSerializeU32(ser, reloc->pos.col);
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

void smDeserializeBuf(SmSerde *ser, SmBuf *buf) {
    if (fread(buf->bytes, 1, buf->len, ser->hnd) != buf->len) {
        int err = ferror(ser->hnd);
        if (err) {
            fatal(ser, "failed to read file: %s\n", strerror(err));
        }
        if (feof(ser->hnd)) {
            fatal(ser, "unexpected end of file\n");
        }
    }
}

SmBufIntern smDeserializeBufIntern(SmSerde *ser) {
    static SmGBuf buf = {0};
    UInt          len = smDeserializeU32(ser);
    if (!buf.inner.bytes) {
        buf.inner.bytes = malloc(len);
        if (!buf.inner.bytes) {
            smFatal("out of memory\n");
        }
        buf.size = len;
    }
    if (len > buf.size) {
        buf.inner.bytes = realloc(buf.inner.bytes, len);
        if (!buf.inner.bytes) {
            smFatal("out of memory\n");
        }
        buf.size = len;
    }
    buf.inner.len = len;
    smDeserializeBuf(ser, &buf.inner);
    SmBufIntern in = {0};
    smBufIntern(&in, buf.inner);
    return in;
}

static SmBuf readBufRef(SmSerde *ser, SmBufIntern const *in) {
    UInt offset = smDeserializeU32(ser);
    UInt len    = smDeserializeU32(ser);
    return (SmBuf){in->bufs[0].inner.bytes + offset, len};
}

static SmLbl readLbl(SmSerde *ser, SmBufIntern const *in) {
    SmLbl lbl = {0};
    if (smDeserializeU8(ser) == 0) {
        lbl.scope = readBufRef(ser, in);
    }
    lbl.name = readBufRef(ser, in);
    return lbl;
}

SmExprIntern smDeserializeExprIntern(SmSerde *ser, SmBufIntern const *strin) {
    static SmExprGBuf buf = {0};
    buf.inner.len         = 0;
    UInt len              = smDeserializeU32(ser);
    for (UInt i = 0; i < len; ++i) {
        U8     kind = smDeserializeU8(ser);
        SmExpr expr = {0};
        expr.kind   = kind;
        switch (kind) {
        case SM_EXPR_CONST:
            expr.num = smDeserializeU32(ser);
            break;
        case SM_EXPR_ADDR:
            expr.addr.sect = readBufRef(ser, strin);
            expr.addr.pc   = smDeserializeU32(ser);
            break;
        case SM_EXPR_OP:
            expr.op.unary = smDeserializeU8(ser);
            expr.op.unary = smDeserializeU8(ser);
            break;
        case SM_EXPR_LABEL:
        case SM_EXPR_REL:
            expr.lbl = readLbl(ser, strin);
            break;
        case SM_EXPR_TAG:
            expr.tag.lbl  = readLbl(ser, strin);
            expr.tag.name = readBufRef(ser, strin);
            break;
        default:
            fatal(ser, "unrecognized expression kind: $%02X\n", kind);
        }
        smExprGBufAdd(&buf, expr);
    }
    SmExprIntern in = {0};
    smExprIntern(&in, buf.inner);
    return in;
}

static SmExprBuf readExprBufRef(SmSerde *ser, SmExprIntern const *in) {
    UInt offset = smDeserializeU32(ser);
    UInt len    = smDeserializeU32(ser);
    return (SmExprBuf){in->bufs[0].inner.items + offset, len};
}

SmSymTab smDeserializeSymTab(SmSerde *ser, SmBufIntern const *strin,
                             SmExprIntern const *exprin) {
    SmSymTab tab = {0};
    UInt     len = smDeserializeU32(ser);
    for (UInt i = 0; i < len; ++i) {
        SmSym sym    = {0};
        sym.lbl      = readLbl(ser, strin);
        sym.value    = readExprBufRef(ser, exprin);
        sym.unit     = readBufRef(ser, strin);
        sym.section  = readBufRef(ser, strin);
        sym.pos.file = readBufRef(ser, strin);
        sym.pos.line = smDeserializeU32(ser);
        sym.pos.col  = smDeserializeU32(ser);
        sym.flags    = smDeserializeU8(ser);
        smSymTabAdd(&tab, sym);
    }
    return tab;
}

SmSectBuf smDeserializeSectBuf(SmSerde *ser, SmBufIntern const *strin,
                               SmExprIntern const *exprin) {
    SmSectGBuf buf = {0};
    UInt       len = smDeserializeU32(ser);
    for (UInt i = 0; i < len; ++i) {
        SmSect sect           = {0};
        sect.name             = readBufRef(ser, strin);
        UInt len              = smDeserializeU32(ser);
        sect.data.inner.bytes = malloc(len);
        if (!sect.data.inner.bytes) {
            smFatal("out of memory\n");
        }
        sect.data.size      = len;
        sect.data.inner.len = len;
        smDeserializeBuf(ser, &sect.data.inner);
        len = smDeserializeU32(ser);
        for (UInt j = 0; j < len; ++j) {
            SmReloc reloc  = {0};
            reloc.offset   = smDeserializeU32(ser);
            reloc.width    = smDeserializeU8(ser);
            reloc.value    = readExprBufRef(ser, exprin);
            reloc.unit     = readBufRef(ser, strin);
            reloc.pos.file = readBufRef(ser, strin);
            reloc.pos.line = smDeserializeU32(ser);
            reloc.pos.col  = smDeserializeU32(ser);
            reloc.flags    = smDeserializeU8(ser);
            smRelocGBufAdd(&sect.relocs, reloc);
        }
        smSectGBufAdd(&buf, sect);
    }
    return buf.inner;
}

void smDeserializeToEnd(SmSerde *ser, SmGBuf *buf) {
    static U8 tmp[4096];
    while (true) {
        size_t read = fread(tmp, 1, sizeof(tmp), ser->hnd);
        if (read == sizeof(tmp)) {
            smGBufCat(buf, (SmBuf){tmp, read});
            continue;
        }
        int err = ferror(ser->hnd);
        if (err) {
            fatal(ser, "failed to read file: %s\n", strerror(err));
        }
        if (feof(ser->hnd)) {
            smGBufCat(buf, (SmBuf){tmp, read});
            break;
        }
    }
}
