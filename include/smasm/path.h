#ifndef SMASM_PATH_H
#define SMASM_PATH_H

#include <smasm/buf.h>

SmBuf smPathIntern(SmBufIntern *in, SmBuf path);

struct SmPathSet {
    SmBufIntern in;
    SmBufGBuf   bufs;
};
typedef struct SmPathSet SmPathSet;

SmBuf smPathSetAdd(SmPathSet *set, SmBuf path);
Bool  smPathSetContains(SmPathSet *set, SmBuf path);

#endif // SMASM_PATH_H
