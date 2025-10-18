#include <smasm/buf.h>

#include <assert.h>
#include <stdlib.h>

int main() {
    assert(smViewEqual(SM_VIEW("hello"), SM_VIEW("hello")));
    assert(smViewEqualIgnoreAsciiCase(SM_VIEW("Hello"), SM_VIEW("hELLo")));
    assert(smViewStartsWith(SM_VIEW("smasm"), SM_VIEW("sm")));
    assert(smViewHash(SM_VIEW("test")) == smViewHash(SM_VIEW("test")));

    assert(smViewParse(SM_VIEW("255")) == 255);
    assert(smViewParse(SM_VIEW("$FF")) == 255);
    assert(smViewParse(SM_VIEW("%11111111")) == 255);

    assert(smViewParse(SM_VIEW("0")) == 0);
    assert(smViewParse(SM_VIEW("$0")) == 0);
    assert(smViewParse(SM_VIEW("%0")) == 0);

    assert(smViewParse(SM_VIEW("16")) == 16);
    assert(smViewParse(SM_VIEW("$10")) == 16);
    assert(smViewParse(SM_VIEW("%10000")) == 16);

    return EXIT_SUCCESS;
}
