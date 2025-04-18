#include "struct.h"

#include <smasm/tab.h>

#include <stdlib.h>

SM_TAB_WHENCE_IMPL(StructTab, Struct);
SM_TAB_TRYGROW_IMPL(StructTab, Struct);

static StructTab STRUCTS = {0};

Struct *structFind(SmBuf name) {
    StructTab *tab = &STRUCTS;
    SM_TAB_FIND_IMPL(StructTab, Struct);
}

static Struct *add(Struct entry) {
    StructTab *tab = &STRUCTS;
    SM_TAB_ADD_IMPL(StructTab, Struct);
}

void structAdd(SmBuf name, SmPos pos, SmBufGBuf fields) {
    add((Struct){
        name,
        pos,
        fields,
    });
}
