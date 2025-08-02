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

// CRC8 calculation for SGP40 communication
static uint8_t sgp40_calculate_crc(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
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

    // Add command
    buffer[buf_len++] = (command >> 8) & 0xFF;
    buffer[buf_len++] = command & 0xFF;

    // Add data with CRC if provided
    if (data && data_len > 0) {
        for (size_t i = 0; i < data_len; i += 2) {
            buffer[buf_len++] = data[i];
            if (i + 1 < data_len) {
                buffer[buf_len++] = data[i + 1];
                uint8_t crc = sgp40_calculate_crc(&data[i], 2);
                buffer[buf_len++] = crc;
            } else {
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

// Read response from SGP40
static esp_err_t sgp40_read_response(int i2c_num, uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SGP40_I2C_ADDR << 1) | I2C_MASTER_READ, true);

    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
        i2c_master_read_byte(cmd, &data[len - 1], I2C_MASTER_NACK);
    } else {
        i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
    }

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

    // Initialize Gas Index Algorithm for VOC
    GasIndexAlgorithm_init(&sgp40->gas_index_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC);

    // Execute self-test
    esp_err_t ret = sgp40_self_test(sgp40, i2c_num);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SGP40 self-test failed");
        return ret;
    }

    // Get serial number
    ret = sgp40_get_serial_number(sgp40, i2c_num);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SGP40 serial number");
        return ret;
    }

    sgp40->initialized = true;
    ESP_LOGI(TAG, "SGP40 initialized successfully, Serial: 0x%016llX", sgp40->serial_number);

    return ESP_OK;
}

esp_err_t sgp40_self_test(sgp40_t *sgp40, int i2c_num) {
    esp_err_t ret = sgp40_send_command(i2c_num, SGP40_CMD_EXECUTE_SELF_TEST, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send self-test command");
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(SGP40_SELFTEST_DELAY_MS));

    uint8_t response[3];
    ret = sgp40_read_response(i2c_num, response, sizeof(response));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read self-test response");
        return ret;
    }

    // Verify CRC
    uint8_t expected_crc = sgp40_calculate_crc(response, 2);
    if (response[2] != expected_crc) {
        ESP_LOGE(TAG, "Self-test CRC mismatch");
        return ESP_FAIL;
    }

    uint16_t test_result = (response[0] << 8) | response[1];
    if (test_result != 0xD400) {
        ESP_LOGE(TAG, "Self-test failed, result: 0x%04X", test_result);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SGP40 self-test passed");
    return ESP_OK;
}

esp_err_t sgp40_get_serial_number(sgp40_t *sgp40, int i2c_num) {
    esp_err_t ret = sgp40_send_command(i2c_num, SGP40_CMD_GET_SERIAL_NUMBER, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send get serial number command");
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(SGP40_SERIAL_DELAY_MS));

    uint8_t response[9]; // 3 words x 3 bytes (2 data + 1 CRC)
    ret = sgp40_read_response(i2c_num, response, sizeof(response));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read serial number response");
        return ret;
    }

    // Verify CRCs and extract serial number
    uint64_t serial = 0;
    for (int i = 0; i < 3; i++) {
        uint8_t expected_crc = sgp40_calculate_crc(&response[i * 3], 2);
        if (response[i * 3 + 2] != expected_crc) {
            ESP_LOGE(TAG, "Serial number CRC mismatch at word %d", i);
            return ESP_FAIL;
        }
        serial = (serial << 16) | (response[i * 3] << 8) | response[i * 3 + 1];
    }

    sgp40->serial_number = serial;
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

    esp_err_t ret = sgp40_send_command(i2c_num, SGP40_CMD_MEASURE_RAW, conditions, sizeof(conditions));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send measure command");
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(SGP40_MEASURE_DELAY_MS));

    uint8_t response[3];
    ret = sgp40_read_response(i2c_num, response, sizeof(response));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read measurement response");
        return ret;
    }

    // Verify CRC
    uint8_t expected_crc = sgp40_calculate_crc(response, 2);
    if (response[2] != expected_crc) {
        ESP_LOGE(TAG, "Measurement CRC mismatch");
        return ESP_FAIL;
    }

    sgp40->voc_raw = (response[0] << 8) | response[1];

    // Process with Gas Index Algorithm
    GasIndexAlgorithm_process(&sgp40->gas_index_params, sgp40->voc_raw, &sgp40->voc_index);

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
