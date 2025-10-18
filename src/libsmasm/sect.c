#include <smasm/fatal.h>
#include <smasm/sect.h>

#include <stdlib.h>
#include <string.h>

void smRelocBufAdd(SmRelocBuf *buf, SmReloc item) { SM_GBUF_ADD_IMPL(); }

void smRelocBufFini(SmRelocBuf *buf) { SM_GBUF_FINI_IMPL(); }

void smSectBufAdd(SmSectBuf *buf, SmSect item) { SM_GBUF_ADD_IMPL(); }

void smSectBufFini(SmSectBuf *buf) { SM_GBUF_FINI_IMPL(); }
