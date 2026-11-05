#define EGA_CLI_TEST
#include "main.c"

#include <assert.h>

static void expect_translation(const char *input, const char *expected) {
    char wire[COMMAND_SIZE];
    assert(translate_command(input, wire, sizeof(wire)) == 0);
    assert(strcmp(wire, expected) == 0);
}

int main(void) {
    expect_translation("GET", "GET");
    expect_translation("SAVE", "SAVE");
    expect_translation("SET P 5", "V 0 5");
    expect_translation("SET N 12.5", "V 1 12.5");
    expect_translation("SET ILP 0.50", "IL 0 0.50");
    expect_translation("SET ILN 0.55", "IL 1 0.55");
    expect_translation("SET VLP 35", "VL 0 35");
    expect_translation("SET VLN 36", "VL 1 36");
    expect_translation("OUT P ON", "OUT 0 ON");
    expect_translation("OUT N OFF", "OUT 1 OFF");
    expect_translation("RESET P", "RST 0");
    expect_translation("RESET N", "RST 1");
    assert(is_async_notification("PROT 0 OC"));
    assert(!is_async_notification("OK QUEUED V 0 5"));
    return 0;
}
