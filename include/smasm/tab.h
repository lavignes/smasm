#ifndef SMASM_TAB_H
#define SMASM_TAB_H

// TODO delete parems from all the macros
// and infer them like we do for the GBuf macros
#define SM_TAB_WHENCE_IMPL(Type, EntryType)                                    \
    static EntryType *Type##Whence(Type *tab, SmBuf name) {                    \
        static SmBuf const NULL_BUF = {0};                                     \
        UInt               hash     = smBufHash(name);                         \
        UInt               i        = hash % tab->size;                        \
        EntryType         *entry    = tab->entries + i;                        \
        while (hash != smBufHash(entry->name)) {                               \
            if (smBufEqual(entry->name, NULL_BUF)) {                           \
                break;                                                         \
            }                                                                  \
            if (smBufEqual(entry->name, name)) {                               \
                break;                                                         \
            }                                                                  \
            i     = (i + 1) % tab->size;                                       \
            entry = tab->entries + i;                                          \
        }                                                                      \
        return entry;                                                          \
    }

#define SM_TAB_TRYGROW_IMPL(Type, EntryType)                                   \
    static void Type##TryGrow(Type *tab) {                                     \
        static SmBuf const NULL_BUF = {0};                                     \
        if (!tab->entries) {                                                   \
            tab->entries = calloc(16, sizeof(EntryType));                      \
            if (!tab->entries) {                                               \
                smFatal("out of memory\n");                                    \
            }                                                                  \
            tab->len  = 0;                                                     \
            tab->size = 16;                                                    \
        }                                                                      \
        /* we always want at least 1 empty slot */                             \
        if ((tab->size - tab->len) == 0) {                                     \
            EntryType *old_entries = tab->entries;                             \
            UInt       old_size    = tab->size;                                \
            tab->size *= 2;                                                    \
            tab->entries = calloc(tab->size, sizeof(EntryType));               \
            if (!tab->entries) {                                               \
                smFatal("out of memory\n");                                    \
            }                                                                  \
            for (UInt i = 0; i < old_size; ++i) {                              \
                EntryType *entry = old_entries + i;                            \
                if (smBufEqual(entry->name, NULL_BUF)) {                       \
                    continue;                                                  \
                }                                                              \
                *Type##Whence(tab, entry->name) = *entry;                      \
            }                                                                  \
            free(old_entries);                                                 \
        }                                                                      \
    }

#define SM_TAB_ADD_IMPL(Type, EntryType)                                       \
    Type##TryGrow(tab);                                                        \
    EntryType *whence = Type##Whence(tab, entry.name);                         \
    *whence           = entry;                                                 \
    ++tab->len;                                                                \
    return whence;

#define SM_TAB_FIND_IMPL(Type, EntryType)                                      \
    static SmBuf const NULL_BUF = {0};                                         \
    if (!tab->entries) {                                                       \
        return NULL;                                                           \
    }                                                                          \
    EntryType *whence = Type##Whence(tab, name);                               \
    if (smBufEqual(whence->name, NULL_BUF)) {                                  \
        return NULL;                                                           \
    }                                                                          \
    return whence;

#endif // SMASM_TAB_H
