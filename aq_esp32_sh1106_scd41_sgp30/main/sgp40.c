/*
 * SGP40 Sensor Driver Implementation for ESP32
 */

#include "sgp40.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SGP40";

// Read response from SGP40 using simple I2C read
static esp_err_t sgp40_read_response_simple(int i2c_num, uint8_t *data, size_t len) {
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Wait before reading to ensure data is ready
    vTaskDelay(pdMS_TO_TICKS(50));

    esp_err_t ret = ESP_OK;

    // Simple I2C read using single transaction
    ret = i2c_master_read_from_device(i2c_num, SGP40_I2C_ADDR, data, len, pdMS_TO_TICKS(5000));

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s (address=0x%02X, len=%zu)",
                esp_err_to_name(ret), SGP40_I2C_ADDR, len);
    } else {
        ESP_LOGV(TAG, "Successfully read %zu bytes from SGP40", len);
        // Log the received data for debugging
        ESP_LOGV(TAG, "Read data: ");
        for (size_t i = 0; i < len; i++) {
            ESP_LOGV(TAG, "  [%zu]: 0x%02X", i, data[i]);
        }
    }

    return ret;
}

// CRC8 calculation for SGP40 communication (Sensirion CRC)
static uint8_t sgp40_calculate_crc(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;  // Initial value

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;  // Polynomial: x^8 + x^5 + x^4 + 1
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

// Send command to SGP40
static esp_err_t sgp40_send_command(int i2c_num, uint16_t command, const uint8_t *data, size_t data_len) {
    uint8_t buffer[16];
    size_t buf_len = 0;

    // Add command (big-endian)
    buffer[buf_len++] = (command >> 8) & 0xFF;
    buffer[buf_len++] = command & 0xFF;

    // Add data with CRC if provided
    if (data && data_len > 0) {
        for (size_t i = 0; i < data_len; i += 2) {
            buffer[buf_len++] = data[i];
            if (i + 1 < data_len) {
                buffer[buf_len++] = data[i + 1];
                // Calculate CRC for the 2-byte data word
                uint8_t word_data[2] = {data[i], data[i + 1]};
                uint8_t crc = sgp40_calculate_crc(word_data, 2);
                buffer[buf_len++] = crc;
            } else {
                // Single byte - should not happen for SGP40, but handle it
                buffer[buf_len++] = 0; // Padding
                uint8_t crc = sgp40_calculate_crc(&data[i], 1);
                buffer[buf_len++] = crc;
            }
        }
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SGP40_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, buffer, buf_len, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    return ret;
}

// Convert temperature and humidity to SGP40 format
static void sgp40_convert_conditions(float temperature, float humidity, uint8_t *temp_data, uint8_t *hum_data) {
    // Convert temperature to SGP40 format: T_ticks = (T_celsius + 45) * 65535 / 175
    uint16_t temp_ticks = (uint16_t)((temperature + 45.0f) * 65535.0f / 175.0f);
    temp_data[0] = (temp_ticks >> 8) & 0xFF;
    temp_data[1] = temp_ticks & 0xFF;

    // Convert humidity to SGP40 format: RH_ticks = RH_percent * 65535 / 100
    uint16_t hum_ticks = (uint16_t)(humidity * 65535.0f / 100.0f);
    hum_data[0] = (hum_ticks >> 8) & 0xFF;
    hum_data[1] = hum_ticks & 0xFF;
}

esp_err_t sgp40_init(sgp40_t *sgp40, int i2c_num) {
    if (!sgp40) {
        ESP_LOGE(TAG, "Invalid SGP40 handle");
        return ESP_ERR_INVALID_ARG;
    }

    memset(sgp40, 0, sizeof(sgp40_t));

    ESP_LOGI(TAG, "Initializing SGP40 sensor...");

    // Initialize Gas Index Algorithm for VOC
    GasIndexAlgorithm_init(&sgp40->gas_index_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC);

    // Try to get serial number first (simpler command, good connectivity test)
    esp_err_t ret = sgp40_get_serial_number(sgp40, i2c_num);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SGP40 serial number - sensor may not be connected");
        return ret;
    }

    // Execute self-test
    ret = sgp40_self_test(sgp40, i2c_num);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SGP40 self-test failed");
        return ret;
    }

    sgp40->initialized = true;
    ESP_LOGI(TAG, "SGP40 initialized successfully, Serial: 0x%016llX", sgp40->serial_number);

    return ESP_OK;
}

esp_err_t sgp40_self_test(sgp40_t *sgp40, int i2c_num) {
    ESP_LOGI(TAG, "Starting SGP40 self-test...");

    esp_err_t ret = sgp40_send_command(i2c_num, SGP40_CMD_EXECUTE_SELF_TEST, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send self-test command: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for self-test to complete (320ms as per datasheet)
    vTaskDelay(pdMS_TO_TICKS(SGP40_SELFTEST_DELAY_MS));

    uint8_t response[3];
    ret = sgp40_read_response_simple(i2c_num, response, sizeof(response));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read self-test response: %s", esp_err_to_name(ret));
        return ret;
    }

    // Log received data for debugging
    ESP_LOGD(TAG, "Self-test response: 0x%02X 0x%02X 0x%02X", response[0], response[1], response[2]);

    // Verify CRC for the 2-byte data word
    uint8_t data_word[2] = {response[0], response[1]};
    uint8_t expected_crc = sgp40_calculate_crc(data_word, 2);
    if (response[2] != expected_crc) {
        ESP_LOGE(TAG, "Self-test CRC mismatch: expected 0x%02X, got 0x%02X", expected_crc, response[2]);
        ESP_LOGE(TAG, "Data: 0x%02X 0x%02X", response[0], response[1]);
        return ESP_FAIL;
    }

    uint16_t test_result = (response[0] << 8) | response[1];
    ESP_LOGD(TAG, "Self-test result: 0x%04X", test_result);

    if (test_result != 0xD400) {
        ESP_LOGE(TAG, "Self-test failed, result: 0x%04X (expected 0xD400)", test_result);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SGP40 self-test passed");
    return ESP_OK;
}

esp_err_t sgp40_get_serial_number(sgp40_t *sgp40, int i2c_num) {
    esp_err_t ret = sgp40_send_command(i2c_num, SGP40_CMD_GET_SERIAL_NUMBER, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send get serial number command: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(SGP40_SERIAL_DELAY_MS));

    uint8_t response[9]; // 3 words x 3 bytes (2 data + 1 CRC)
    ret = sgp40_read_response_simple(i2c_num, response, sizeof(response));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read serial number response: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify CRCs and extract serial number
    uint64_t serial = 0;
    for (int i = 0; i < 3; i++) {
        uint8_t data_word[2] = {response[i * 3], response[i * 3 + 1]};
        uint8_t expected_crc = sgp40_calculate_crc(data_word, 2);
        if (response[i * 3 + 2] != expected_crc) {
            ESP_LOGE(TAG, "Serial number CRC mismatch at word %d: expected 0x%02X, got 0x%02X",
                    i, expected_crc, response[i * 3 + 2]);
            return ESP_FAIL;
        }
        serial = (serial << 16) | (response[i * 3] << 8) | response[i * 3 + 1];
    }

    sgp40->serial_number = serial;
    ESP_LOGD(TAG, "SGP40 serial number: 0x%016llX", serial);
    return ESP_OK;
}

esp_err_t sgp40_measure_voc(sgp40_t *sgp40, int i2c_num, float temperature, float humidity) {
    if (!sgp40 || !sgp40->initialized) {
        ESP_LOGE(TAG, "SGP40 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Prepare temperature and humidity data
    uint8_t conditions[4];
    sgp40_convert_conditions(temperature, humidity, &conditions[0], &conditions[2]);

    ESP_LOGV(TAG, "Sending measure command with T=%.1f°C, H=%.1f%% (data: 0x%02X%02X 0x%02X%02X)",
             temperature, humidity, conditions[0], conditions[1], conditions[2], conditions[3]);

    esp_err_t ret = sgp40_send_command(i2c_num, SGP40_CMD_MEASURE_RAW, conditions, sizeof(conditions));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send measure command: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for measurement to complete
    vTaskDelay(pdMS_TO_TICKS(SGP40_MEASURE_DELAY_MS));

    uint8_t response[3];
    ret = sgp40_read_response_simple(i2c_num, response, sizeof(response));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read measurement response: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGV(TAG, "Measurement response: 0x%02X 0x%02X 0x%02X", response[0], response[1], response[2]);

    // Verify CRC for the 2-byte data word
    uint8_t data_word[2] = {response[0], response[1]};
    uint8_t expected_crc = sgp40_calculate_crc(data_word, 2);
    if (response[2] != expected_crc) {
        ESP_LOGE(TAG, "Measurement CRC mismatch: expected 0x%02X, got 0x%02X", expected_crc, response[2]);
        ESP_LOGE(TAG, "Data: 0x%02X 0x%02X", response[0], response[1]);
        return ESP_FAIL;
    }

    sgp40->voc_raw = (response[0] << 8) | response[1];

    // Process with Gas Index Algorithm
    GasIndexAlgorithm_process(&sgp40->gas_index_params, sgp40->voc_raw, &sgp40->voc_index);

    ESP_LOGD(TAG, "VOC Raw: %d, VOC Index: %ld, Temp: %.1f°C, Humidity: %.1f%%",
             sgp40->voc_raw, sgp40->voc_index, temperature, humidity);

    return ESP_OK;
}

int32_t sgp40_get_voc_index(sgp40_t *sgp40) {
    if (!sgp40 || !sgp40->initialized) {
        return -1;
    }
    return sgp40->voc_index;
}

uint16_t sgp40_get_voc_raw(sgp40_t *sgp40) {
    if (!sgp40 || !sgp40->initialized) {
        return 0;
    }
    return sgp40->voc_raw;
}

esp_err_t sgp40_turn_heater_off(int i2c_num) {
    return sgp40_send_command(i2c_num, SGP40_CMD_TURN_HEATER_OFF, NULL, 0);
}

// Check if SGP40 is ready to send data
bool sgp40_is_data_ready(int i2c_num) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        return false;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SGP40_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return (ret == ESP_OK);
}
