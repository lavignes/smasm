#ifndef SMASM_SERDE_H
#define SMASM_SERDE_H

#include <smasm/sect.h>

typedef struct {
    FILE  *hnd;
    SmView name;
} SmSerde;

void smSerializeU8(SmSerde *ser, U8 byte);
void smSerializeU16(SmSerde *ser, U16 word);
void smSerializeU32(SmSerde *ser, U32 num);
void smSerializeView(SmSerde *ser, SmView view);

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
void smDeserializeView(SmSerde *ser, SmView *view);

SmBufIntern  smDeserializeBufIntern(SmSerde *ser);
SmExprIntern smDeserializeExprIntern(SmSerde *ser, SmBufIntern const *strin);
SmSymTab     smDeserializeSymTab(SmSerde *ser, SmBufIntern const *strin,
                                 SmExprIntern const *exprin);
SmSectGBuf   smDeserializeSectBuf(SmSerde *ser, SmBufIntern const *strin,
                                  SmExprIntern const *exprin);
void         smDeserializeToEnd(SmSerde *ser, SmGBuf *buf);

#endif // SMASM_SERDE_H
