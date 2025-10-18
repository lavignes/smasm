#include <smasm/path.h>

#include <stdlib.h>
#include <string.h>

SmView smPathIntern(SmViewIntern *in, SmView path) {
    static SmBuf buf = {0};
    buf.view.len     = 0;
    smBufCat(&buf, path);
    smBufCat(&buf, SM_VIEW("\0"));
    char *out = realpath((char *)buf.view.bytes, NULL);
    if (out == NULL) {
        return smViewIntern(in, path);
    }
    SmView result = smViewIntern(in, (SmView){(U8 *)out, strlen(out)});
    free(out);
    return result;
}

SmView smPathSetAdd(SmPathSet *set, SmView path) {
    SmView view = smPathIntern(&set->in, path);
    for (UInt i = 0; i < set->bufs.view.len; ++i) {
        if (smViewEqual(set->bufs.view.items[i], view)) {
            return view;
        }
    }
    smBufGBufAdd(&set->bufs, view);
    return view;
}

Bool smPathSetContains(SmPathSet *set, SmView path) {
    // we intern to abs/normalize the path
    SmView view = smPathIntern(&set->in, path);
    for (UInt i = 0; i < set->bufs.view.len; ++i) {
        if (smViewEqual(set->bufs.view.items[i], view)) {
            return true;
        }
    }
    return false;
}
