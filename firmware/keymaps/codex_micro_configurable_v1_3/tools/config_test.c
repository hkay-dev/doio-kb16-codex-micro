#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "kb16_config.h"

static void assert_default_native(void) {
    static const uint8_t expected[16] = {
        KB16_NATIVE_AG00, KB16_NATIVE_AG01, KB16_NATIVE_JOY_UP, KB16_NATIVE_JOY_RIGHT,
        KB16_NATIVE_AG02, KB16_NATIVE_AG03, KB16_NATIVE_JOY_LEFT, KB16_NATIVE_JOY_DOWN,
        KB16_NATIVE_AG04, KB16_NATIVE_AG05, KB16_NATIVE_ACT06, KB16_NATIVE_ACT07,
        KB16_NATIVE_ACT08, KB16_NATIVE_ACT09, KB16_NATIVE_ACT12, KB16_NATIVE_MIC,
    };
    assert(memcmp(kb16_config_get()->native_keys, expected, sizeof(expected)) == 0);
}

int main(void) {
    kb16_config_payload_t golden = {0};
    for (uint8_t index = 0; index < KB16_CONFIG_KEY_COUNT; ++index) {
        golden.native_keys[index] = index;
    }
    assert(sizeof(golden) == 340);
    assert(kb16_config_validate(&golden));
    assert(kb16_config_crc32(&golden, sizeof(golden)) == 0xD82701A6UL);

    kb16_config_host_clear_storage();
    kb16_config_init();
    assert(kb16_config_generation() == 1);
    assert_default_native();
    assert(kb16_config_crc() == kb16_config_crc32(kb16_config_get(), sizeof(*kb16_config_get())));

    kb16_config_payload_t changed = *kb16_config_get();
    uint8_t temporary = changed.native_keys[0];
    changed.native_keys[0] = changed.native_keys[15];
    changed.native_keys[15] = temporary;
    assert(kb16_config_validate(&changed));
    assert(kb16_config_commit(&changed));
    assert(kb16_config_generation() == 2);
    assert(kb16_config_find_native(KB16_NATIVE_AG00) == 15);

    kb16_config_payload_t duplicate = changed;
    duplicate.native_keys[0] = duplicate.native_keys[1];
    assert(!kb16_config_validate(&duplicate));
    assert(!kb16_config_commit(&duplicate));

    kb16_config_payload_t invalid_action = changed;
    invalid_action.layers[0][0].kind = 0xFF;
    assert(!kb16_config_validate(&invalid_action));

    kb16_config_input_pressed();
    assert(kb16_config_input_busy());
    assert(!kb16_config_commit(&changed));
    kb16_config_input_released();
    assert(!kb16_config_input_busy());

    /* Generation 2 is slot 1. Corrupt it and verify startup falls back to slot 0. */
    kb16_config_host_corrupt_slot(1, 20);
    kb16_config_init();
    assert(kb16_config_generation() == 1);
    assert_default_native();

    assert(kb16_config_reset_defaults());
    assert_default_native();
    kb16_config_init();
    assert_default_native();
    assert(kb16_config_generation() == 1);

    kb16_config_host_corrupt_slot(0, 20);
    kb16_config_host_fail_writes(true);
    assert(!kb16_config_reset_defaults());
    kb16_config_host_fail_writes(false);
    puts("KB16 configuration tests passed");
    return 0;
}
