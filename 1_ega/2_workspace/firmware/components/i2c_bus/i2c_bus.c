#include "i2c_bus.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";

#define I2C_SDA_GPIO     8
#define I2C_SCL_GPIO     9
#define I2C_FREQ_HZ      400000
#define MUTEX_TIMEOUT_MS 100
#define MAX_DEVICES      8

typedef struct {
    uint8_t addr;
    i2c_master_dev_handle_t handle;
} dev_entry_t;

static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t       s_mutex;
static dev_entry_t             s_devices[MAX_DEVICES];
static int                     s_dev_count;

static i2c_master_dev_handle_t find_or_add_device(uint8_t addr)
{
    for (int i = 0; i < s_dev_count; i++) {
        if (s_devices[i].addr == addr)
            return s_devices[i].handle;
    }

    if (s_dev_count >= MAX_DEVICES) {
        ESP_LOGE(TAG, "Device table full, cannot add 0x%02X", addr);
        return NULL;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };

    i2c_master_dev_handle_t handle;
    esp_err_t err = i2c_master_bus_add_device(s_bus, &cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device 0x%02X: %s", addr, esp_err_to_name(err));
        return NULL;
    }

    s_devices[s_dev_count].addr   = addr;
    s_devices[s_dev_count].handle = handle;
    s_dev_count++;

    ESP_LOGI(TAG, "Registered device 0x%02X", addr);
    return handle;
}

static esp_err_t take_mutex(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) return ret;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_dev_count = 0;
    ESP_LOGI(TAG, "Bus initialized (SDA=%d SCL=%d %d Hz)", I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_FREQ_HZ);
    return ESP_OK;
}

esp_err_t i2c_bus_write(uint8_t dev_addr, const uint8_t *data, size_t len)
{
    esp_err_t ret = take_mutex();
    if (ret != ESP_OK) return ret;

    i2c_master_dev_handle_t h = find_or_add_device(dev_addr);
    if (!h) { xSemaphoreGive(s_mutex); return ESP_ERR_NOT_FOUND; }

    ret = i2c_master_transmit(h, data, len, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t i2c_bus_read(uint8_t dev_addr, uint8_t *data, size_t len)
{
    esp_err_t ret = take_mutex();
    if (ret != ESP_OK) return ret;

    i2c_master_dev_handle_t h = find_or_add_device(dev_addr);
    if (!h) { xSemaphoreGive(s_mutex); return ESP_ERR_NOT_FOUND; }

    ret = i2c_master_receive(h, data, len, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t i2c_bus_write_read(uint8_t dev_addr,
                              const uint8_t *write_data, size_t write_len,
                              uint8_t *read_data, size_t read_len)
{
    esp_err_t ret = take_mutex();
    if (ret != ESP_OK) return ret;

    i2c_master_dev_handle_t h = find_or_add_device(dev_addr);
    if (!h) { xSemaphoreGive(s_mutex); return ESP_ERR_NOT_FOUND; }

    ret = i2c_master_transmit_receive(h, write_data, write_len, read_data, read_len, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_mutex);
    return ret;
}
