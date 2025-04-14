#include <smasm/fatal.h>
#include <smasm/sym.h>

#include <stdlib.h>
#include <string.h>

Bool smLblEqual(SmLbl lhs, SmLbl rhs) {
    return smBufEqual(lhs.scope, rhs.scope) && smBufEqual(lhs.name, rhs.name);
}

Bool smLblIsGlobal(SmLbl lbl) { return smBufEqual(lbl.scope, SM_BUF_NULL); }

void smOpGBufAdd(SmOpGBuf *buf, SmOp item) { SM_GBUF_ADD_IMPL(SmOp); }

void smExprGBufAdd(SmExprGBuf *buf, SmExpr item) { SM_GBUF_ADD_IMPL(SmExpr); }

SmExprBuf smExprIntern(SmExprIntern *in, SmExprBuf buf) {
    SM_INTERN_IMPL(SmExpr, SmExprBuf, SmExprGBuf);
}

void smI32GBufAdd(SmI32GBuf *buf, I32 item) { SM_GBUF_ADD_IMPL(I32); }

void smI32GBufFini(SmI32GBuf *buf) {
    if (!buf->inner.items) {
        return;
    }
    free(buf->inner.items);
    memset(buf, 0, sizeof(SmI32GBuf));
}

static UInt hashLabel(SmLbl lbl) {
    UInt hash = 5381;
    for (UInt i = 0; i < lbl.scope.len; ++i) {
        hash = ((hash << 5) + hash) + lbl.scope.bytes[i];
    }
    hash = ((hash << 5) + hash) + '.';
    for (UInt i = 0; i < lbl.name.len; ++i) {
        hash = ((hash << 5) + hash) + lbl.name.bytes[i];
    }
    return hash;
}

static SmSym *whence(SmSymTab *tab, SmLbl lbl) {
    UInt   hash = hashLabel(lbl);
    UInt   i    = hash % tab->size;
    SmSym *sym  = tab->syms + i;
    while (hash != hashLabel(sym->lbl)) {
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            break;
        }
        if (smLblEqual(sym->lbl, lbl)) {
            break;
        }
        i   = (i + 1) % tab->size;
        sym = tab->syms + i;
    }
    return sym;
}

static void tryGrow(SmSymTab *tab) {
    if (!tab->syms) {
        tab->syms = calloc(16, sizeof(SmSym));
        if (!tab->syms) {
            smFatal("out of memory\n");
        }
        tab->len  = 0;
        tab->size = 16;
    }
    // We always want at least 1 empty slot
    if ((tab->size - tab->len) == 1) {
        SmSym *old_syms = tab->syms;
        UInt   old_size = tab->size;
        tab->size *= 2;
        tab->syms = calloc(tab->size, sizeof(SmSym));
        if (!tab->syms) {
            smFatal("out of memory\n");
        }
        for (UInt i = 0; i < old_size; ++i) {
            SmSym *sym = old_syms + i;
            if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
                continue;
            }
            *whence(tab, sym->lbl) = *sym;
        }
        free(old_syms);
    }
}

SmSym *smSymTabAdd(SmSymTab *tab, SmSym sym) {
    tryGrow(tab);
    SmSym *wh = whence(tab, sym.lbl);
    *wh       = sym;
    ++tab->len;
    return wh;
}

SmSym *smSymTabFind(SmSymTab *tab, SmLbl lbl) {
    if (!tab->syms) {
        return NULL;
    }
    SmSym *wh = whence(tab, lbl);
    if (smLblEqual(wh->lbl, SM_LBL_NULL)) {
        return NULL;
    }
    return wh;
}
