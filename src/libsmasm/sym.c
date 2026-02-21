#include <smasm/fatal.h>
#include <smasm/sym.h>

#include <stdlib.h>
#include <string.h>

Bool smLblEqual(SmLbl lhs, SmLbl rhs) {
    return smViewEqual(lhs.scope, rhs.scope) && smViewEqual(lhs.name, rhs.name);
}

Bool smLblIsGlobal(SmLbl lbl) { return smViewEqual(lbl.scope, SM_VIEW_NULL); }

SmView smLblFullName(SmLbl lbl, SmViewIntern *in) {
    static SmBuf buf = {};
    buf.view.len     = 0;
    if (!smViewEqual(lbl.scope, SM_VIEW_NULL)) {
        smBufCat(&buf, lbl.scope);
        smBufCat(&buf, SM_VIEW("."));
    }
    smBufCat(&buf, lbl.name);
    return smViewIntern(in, buf.view);
}

void smOpBufAdd(SmOpBuf *buf, SmOp item) { SM_BUF_ADD_IMPL(); }

void smExprBufAdd(SmExprBuf *buf, SmExpr item) { SM_BUF_ADD_IMPL(); }

void smExprBufFini(SmExprBuf *buf) { SM_BUF_FINI_IMPL(); }

SmExprView smExprIntern(SmExprIntern *in, SmExprView view) { SM_INTERN_IMPL(); }

void smExprInternFini(SmExprIntern *in) { SM_INTERN_FINI_IMPL(smExprBufFini); }

void smI32BufAdd(SmI32Buf *buf, I32 item) { SM_BUF_ADD_IMPL(); }

void smI32BufFini(SmI32Buf *buf) { SM_BUF_FINI_IMPL(); }

static UInt hashLbl(SmLbl lbl) {
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
    UInt   hash = hashLbl(lbl);
    UInt   i    = hash % tab->cap;
    SmSym *sym  = tab->syms + i;
    while (hash != hashLbl(sym->lbl)) {
        if (smLblEqual(sym->lbl, SM_LBL_NULL)) {
            break;
        }
        if (smLblEqual(sym->lbl, lbl)) {
            break;
        }
        i   = (i + 1) % tab->cap;
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
        tab->len = 0;
        tab->cap = 16;
    }
    // We always want at least 1 empty slot
    if ((tab->cap - tab->len) == 1) {
        SmSym *old_syms = tab->syms;
        UInt   old_size = tab->cap;
        tab->cap *= 2;
        tab->syms = calloc(tab->cap, sizeof(SmSym));
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

void smSymTabFini(SmSymTab *tab) {
    if (!tab->syms) {
        return;
    }
    free(tab->syms);
    memset(tab, 0, sizeof(SmSymTab));
}
