#ifndef SMASM_SERDE_H
#define SMASM_SERDE_H

#include <smasm/sect.h>

struct SmSerde {
    FILE *hnd;
    SmBuf name;
};
typedef struct SmSerde SmSerde;

void smSerializeU8(SmSerde *ser, U8 byte);
void smSerializeU16(SmSerde *ser, U16 word);
void smSerializeU32(SmSerde *ser, U32 num);
void smSerializeBuf(SmSerde *ser, SmBuf buf);

void smSerializeBufIntern(SmSerde *ser, SmBufIntern const *in);
void smSerializeExprIntern(SmSerde *ser, SmExprIntern const *in,
                           SmBufIntern const *strin);
void smSerializeSymTab(SmSerde *ser, SmSymTab const *tab,
                       SmBufIntern const *strin, SmExprIntern const *exprin);
void smSerializeSectBuf(SmSerde *ser, SmSectBuf sects, SmBufIntern const *strin,
                        SmExprIntern const *exprin);

U8   smDeserializeU8(SmSerde *ser);
U16  smDeserializeU16(SmSerde *ser);
U32  smDeserializeU32(SmSerde *ser);
void smDeserializeBuf(SmSerde *ser, SmBuf *buf);

void smDeserializeBufIntern(SmSerde *ser, SmBufIntern *in);
void smDeserializeExprIntern(SmSerde *ser, SmExprIntern *in,
                             SmBufIntern const *strin);
void smDeserializeSymTab(SmSerde *ser, SmSymTab *tab, SmBufIntern const *strin,
                         SmExprIntern const *exprin);

#endif // SMASM_SERDE_H
