#include <smasm/fatal.h>
#include <smasm/sect.h>

#include <stdlib.h>

void smRelocGBufAdd(SmRelocGBuf *buf, SmReloc item) {
    SM_GBUF_ADD_IMPL(SmReloc);
}

void smSectGBufAdd(SmSectGBuf *buf, SmSect item) { SM_GBUF_ADD_IMPL(SmSect); }
