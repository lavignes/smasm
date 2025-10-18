#ifndef STATE_H
#define STATE_H

#include <smasm/path.h>
#include <smasm/sect.h>

extern SmViewIntern STRS;
extern SmSymTab     SYMS;
extern SmExprIntern EXPRS;
extern SmPathSet    IPATHS;
extern SmPathSet    INCS;

SmView intern(SmView view);

extern SmView DEFINES_SECTION;
extern SmView CODE_SECTION;
extern SmView STATIC_UNIT;
extern SmView EXPORT_UNIT;

extern SmView scope;
extern UInt   nonce;
extern Bool   emit;
extern Bool   streamdef;

SmLbl lblGlobal(SmView name);
SmLbl lblLocal(SmView name);
SmLbl lblAbs(SmView scope, SmView name);

#define STACK_SIZE 64
extern SmTokStream  STACK[STACK_SIZE];
extern SmTokStream *ts;

SM_FORMAT(1) _Noreturn void fatal(char const *fmt, ...);
SM_FORMAT(2) _Noreturn void fatalPos(SmPos pos, char const *fmt, ...);

void popStream();
U32  peek();
void eat();
void expect(U32 tok);

SmView tokView();
I32    tokNum();
SmPos  tokPos();
SmLbl  tokLbl();

extern SmSectBuf SECTS;

SmSect *sectGet();
void    sectSet(SmView name);
void    sectPush(SmView name);
void    sectPop();
void    sectRewind();

void setPC(U16 num);
U16  getPC();
void addPC(U16 offset);

#endif // STATE_H
