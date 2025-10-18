#ifndef SMASM_PATH_H
#define SMASM_PATH_H

#include <smasm/buf.h>

SmView smPathIntern(SmViewIntern *in, SmView path);

typedef struct {
    SmViewIntern in;
    SmViewBuf    bufs;
} SmPathSet;

SmView smPathSetAdd(SmPathSet *set, SmView path);
Bool   smPathSetContains(SmPathSet *set, SmView path);

#endif // SMASM_PATH_H
