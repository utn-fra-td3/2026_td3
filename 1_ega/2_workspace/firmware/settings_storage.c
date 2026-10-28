#include "settings_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#define SETTINGS_MAGIC 0x46505444U
#define SETTINGS_VERSION 1U
#define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
    uint32_t magic;
    uint32_t version;
    stored_settings_t settings;
    uint32_t checksum;
} flash_record_t;

static uint32_t settings_checksum(const flash_record_t *record) {
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t checksum = 2166136261U;
    for (size_t i = 0U; i < offsetof(flash_record_t, checksum); i++) checksum = (checksum ^ bytes[i]) * 16777619U;
    return checksum;
}

bool settings_storage_load(stored_settings_t *settings) {
    const flash_record_t *record = (const flash_record_t *)(XIP_BASE + SETTINGS_FLASH_OFFSET);
    if (record->magic != SETTINGS_MAGIC || record->version != SETTINGS_VERSION) return false;
    if (record->checksum != settings_checksum(record)) return false;
    *settings = record->settings;
    return true;
}

void settings_storage_save(const stored_settings_t *settings) {
    uint8_t flash_page[FLASH_PAGE_SIZE];
    memset(flash_page, 0xFF, sizeof(flash_page));
    flash_record_t record = {.magic = SETTINGS_MAGIC, .version = SETTINGS_VERSION, .settings = *settings, .checksum = 0U};
    record.checksum = settings_checksum(&record);
    memcpy(flash_page, &record, sizeof(record));
    uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_FLASH_OFFSET, flash_page, FLASH_PAGE_SIZE);
    restore_interrupts(interrupts);
}
