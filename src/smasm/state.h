#ifndef STATE_H
#define STATE_H

#include <smasm/path.h>
#include <smasm/sect.h>

extern SmBufIntern       STRS;
extern SmSymTab          SYMS;
extern SmExprIntern      EXPRS;
extern SmPathSet         IPATHS;
extern SmPathSet         INCS;
extern SmRepeatTokIntern REPEATS;

SmBuf intern(SmBuf buf);

extern SmBuf DEFINES_SECTION;
extern SmBuf CODE_SECTION;
extern SmBuf STATIC_UNIT;
extern SmBuf EXPORT_UNIT;

extern SmBuf scope;
extern UInt  if_level;
extern UInt  nonce;
extern Bool  emit;

SmLbl lblGlobal(SmBuf name);
SmLbl lblLocal(SmBuf name);
SmLbl lblAbs(SmBuf scope, SmBuf name);

#define STACK_SIZE 64
extern SmTokStream  STACK[STACK_SIZE];
extern SmTokStream *ts;

_Noreturn void fatal(char const *fmt, ...);
_Noreturn void fatalPos(SmPos pos, char const *fmt, ...);

void popStream();
U32  peek();
void eat();
void expect(U32 tok);

SmBuf tokBuf();
I32   tokNum();
SmPos tokPos();
SmLbl tokLbl();

extern SmSectGBuf SECTS;

SmSect *sectGet();
void    sectSet(SmBuf name);
void    sectRewind();

void setPC(U16 num);
U16  getPC();
void addPC(U16 offset);

#endif // STATE_H
