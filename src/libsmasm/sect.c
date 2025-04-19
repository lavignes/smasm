#include <smasm/fatal.h>
#include <smasm/sect.h>

#include <stdlib.h>
#include <string.h>

void smRelocGBufAdd(SmRelocGBuf *buf, SmReloc item) { SM_GBUF_ADD_IMPL(); }

void smRelocGBufFini(SmRelocGBuf *buf) { SM_GBUF_FINI_IMPL(); }

void smSectGBufAdd(SmSectGBuf *buf, SmSect item) { SM_GBUF_ADD_IMPL(); }

void smSectGBufFini(SmSectGBuf *buf) { SM_GBUF_FINI_IMPL(); }
