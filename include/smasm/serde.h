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

void smSerializeViewIntern(SmSerde *ser, SmViewIntern const *in);
void smSerializeExprIntern(SmSerde *ser, SmExprIntern const *in,
                           SmViewIntern const *strin);
void smSerializeSymTab(SmSerde *ser, SmSymTab const *tab,
                       SmViewIntern const *strin, SmExprIntern const *exprin);
void smSerializeSectView(SmSerde *ser, SmSectView sects,
                         SmViewIntern const *strin, SmExprIntern const *exprin);

U8   smDeserializeU8(SmSerde *ser);
U16  smDeserializeU16(SmSerde *ser);
U32  smDeserializeU32(SmSerde *ser);
void smDeserializeView(SmSerde *ser, SmView *view);

SmViewIntern smDeserializeViewIntern(SmSerde *ser);
SmExprIntern smDeserializeExprIntern(SmSerde *ser, SmViewIntern const *strin);
SmSymTab     smDeserializeSymTab(SmSerde *ser, SmViewIntern const *strin,
                                 SmExprIntern const *exprin);
SmSectBuf    smDeserializeSectBuf(SmSerde *ser, SmViewIntern const *strin,
                                  SmExprIntern const *exprin);
void         smDeserializeToEnd(SmSerde *ser, SmBuf *buf);

#endif // SMASM_SERDE_H
