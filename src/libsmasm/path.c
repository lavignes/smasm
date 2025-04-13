#include <smasm/path.h>

#include <stdlib.h>
#include <string.h>

SmBuf smPathIntern(SmBufIntern *in, SmBuf path) {
    static SmGBuf buf = {0};
    buf.inner.len     = 0;
    smGBufCat(&buf, path);
    smGBufCat(&buf, SM_BUF("\0"));
    char *out = realpath((char *)buf.inner.bytes, NULL);
    if (out == NULL) {
        return smBufIntern(in, path);
    }
    SmBuf result = smBufIntern(in, (SmBuf){(U8 *)out, strlen(out)});
    free(out);
    return result;
}

SmBuf smPathSetAdd(SmPathSet *set, SmBuf path) {
    SmBuf buf = smPathIntern(&set->in, path);
    for (UInt i = 0; i < set->bufs.inner.len; ++i) {
        if (smBufEqual(set->bufs.inner.items[i], buf)) {
            return buf;
        }
    }
    smBufGBufAdd(&set->bufs, buf);
    return buf;
}

Bool smPathSetContains(SmPathSet *set, SmBuf path) {
    // we intern to abs/normalize the path
    SmBuf buf = smPathIntern(&set->in, path);
    for (UInt i = 0; i < set->bufs.inner.len; ++i) {
        if (smBufEqual(set->bufs.inner.items[i], buf)) {
            return true;
        }
    }
    return false;
}
