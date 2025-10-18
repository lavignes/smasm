#ifndef SMASM_PATH_H
#define SMASM_PATH_H

#include <smasm/buf.h>

SmView smPathIntern(SmBufIntern *in, SmView path);

struct SmPathSet {
    SmBufIntern in;
    SmBufGBuf   bufs;
};
typedef struct SmPathSet SmPathSet;

SmView smPathSetAdd(SmPathSet *set, SmView path);
Bool   smPathSetContains(SmPathSet *set, SmView path);

#endif // SMASM_PATH_H
