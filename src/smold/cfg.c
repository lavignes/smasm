#include "cfg.h"

#include <smasm/fatal.h>
#include <smasm/tab.h>

#include <stdlib.h>

SM_TAB_WHENCE_IMPL(CfgI32Tab, CfgI32Entry);
SM_TAB_TRYGROW_IMPL(CfgI32Tab, CfgI32Entry);

CfgI32Entry *cfgI32TabAdd(CfgI32Tab *tab, CfgI32Entry entry) {
    SM_TAB_ADD_IMPL(CfgI32Tab, CfgI32Entry);
}

CfgI32Entry *smI32TabFind(CfgI32Tab *tab, SmBuf name) {
    SM_TAB_FIND_IMPL(CfgI32Tab, CfgI32Entry);
}

void smCfgMemGrowBufAdd(CfgMemGBuf *buf, CfgMem item) {
    SM_GBUF_ADD_IMPL(CfgMem);
}

void smCfgSectGrowBufAdd(CfgSectGBuf *buf, CfgSect item) {
    SM_GBUF_ADD_IMPL(CfgSect);
}
