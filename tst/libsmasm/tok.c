#include <smasm/tok.h>

#include <assert.h>
#include <stdlib.h>

int main() {
    SmTokStream ts;
    smTokStreamViewInit(&ts, SM_VIEW("test"),
                        SM_VIEW("example"
                                " "
                                "1234"
                                " "
                                "$1aBcD"
                                " "
                                "%10101"));

    assert(smTokStreamPeek(&ts) == SM_TOK_ID);
    assert(smViewEqual(smTokStreamView(&ts), SM_VIEW("example")));
    smTokStreamEat(&ts);

    assert(smTokStreamPeek(&ts) == SM_TOK_NUM);
    assert(smTokStreamNum(&ts) == 1234);
    smTokStreamEat(&ts);

    assert(smTokStreamPeek(&ts) == SM_TOK_NUM);
    assert(smTokStreamNum(&ts) == 0x1ABCD);
    smTokStreamEat(&ts);

    assert(smTokStreamPeek(&ts) == SM_TOK_NUM);
    assert(smTokStreamNum(&ts) == 0b10101);
    smTokStreamEat(&ts);

    assert(smTokStreamPeek(&ts) == SM_TOK_EOF);
    smTokStreamFini(&ts);

    return EXIT_SUCCESS;
}
