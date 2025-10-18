#include "cfg.h"

#include <smasm/fatal.h>
#include <smasm/tab.h>

#include <stdlib.h>
#include <string.h>

SM_TAB_WHENCE_IMPL(CfgI32Tab, CfgI32Entry);
SM_TAB_TRYGROW_IMPL(CfgI32Tab, CfgI32Entry);

CfgI32Entry *cfgI32TabAdd(CfgI32Tab *tab, CfgI32Entry entry) {
    SM_TAB_ADD_IMPL(CfgI32Tab, CfgI32Entry);
}

CfgI32Entry *cfgI32TabFind(CfgI32Tab *tab, SmView name) {
    SM_TAB_FIND_IMPL(CfgI32Tab, CfgI32Entry);
}

static void noop(CfgI32Entry *entry) { (void)entry; }

void cfgI32TabFini(CfgI32Tab *tab) { SM_TAB_FINI_IMPL(noop); }

void cfgInBufAdd(CfgInBuf *buf, CfgIn item) { SM_GBUF_ADD_IMPL(); }

void cfgOutBufAdd(CfgOutBuf *buf, CfgOut item) { SM_GBUF_ADD_IMPL(); }
