/*
 * SGP40 Sensor Driver for ESP32
 *
 * This driver provides functionality to read VOC raw signals from the SGP40 sensor
 * and process them using Sensirion's Gas Index Algorithm to calculate VOC Index.
 */

#ifndef SGP40_H
#define SGP40_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sensirion_gas_index_algorithm.h"

#ifdef __cplusplus
extern "C" {
#endif

// SGP40 I2C address
#define SGP40_I2C_ADDR                  0x59

// SGP40 Commands
#define SGP40_CMD_MEASURE_RAW           0x260F
#define SGP40_CMD_EXECUTE_SELF_TEST     0x280E
#define SGP40_CMD_TURN_HEATER_OFF       0x3615
#define SGP40_CMD_GET_SERIAL_NUMBER     0x3682

// SGP40 timing constants
#define SGP40_MEASURE_DELAY_MS          100  // Increased to 100ms for better reliability
#define SGP40_SELFTEST_DELAY_MS         350  // Increased from 320ms  
#define SGP40_SERIAL_DELAY_MS           10   // Increased from 1ms0  // Increased for better reliability
#define SGP40_WARMUP_DELAY_MS           1000

// SGP40 data structure
typedef struct {
    bool initialized;
    uint16_t voc_raw;
    int32_t voc_index;
    uint64_t serial_number;
    GasIndexAlgorithmParams gas_index_params;
} sgp40_t;

/**
 * @brief Initialize SGP40 sensor
 *
 * @param sgp40 Pointer to SGP40 data structure
 * @param i2c_num I2C port number
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t sgp40_init(sgp40_t *sgp40, int i2c_num);

/**
 * @brief Execute SGP40 self-test
 *
 * @param sgp40 Pointer to SGP40 data structure
 * @param i2c_num I2C port number
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t sgp40_self_test(sgp40_t *sgp40, int i2c_num);

/**
 * @brief Read SGP40 serial number
 *
 * @param sgp40 Pointer to SGP40 data structure
 * @param i2c_num I2C port number
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t sgp40_get_serial_number(sgp40_t *sgp40, int i2c_num);

/**
 * @brief Measure VOC raw signal and calculate VOC index
 *
 * @param sgp40 Pointer to SGP40 data structure
 * @param i2c_num I2C port number
 * @param temperature Temperature for compensation (°C), use 25.0 if not available
 * @param humidity Humidity for compensation (%RH), use 50.0 if not available
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t sgp40_measure_voc(sgp40_t *sgp40, int i2c_num, float temperature, float humidity);

/**
 * @brief Get the latest VOC index
 *
 * @param sgp40 Pointer to SGP40 data structure
 * @return VOC index (0-500), or -1 if not available
 */
int32_t sgp40_get_voc_index(sgp40_t *sgp40);

/**
 * @brief Get the latest VOC raw signal
 *
 * @param sgp40 Pointer to SGP40 data structure
 * @return VOC raw signal, or 0 if not available
 */
uint16_t sgp40_get_voc_raw(sgp40_t *sgp40);

/**
 * @brief Turn off SGP40 heater (for power saving)
 *
 * @param i2c_num I2C port number
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t sgp40_turn_heater_off(int i2c_num);

/**
 * @brief Check if SGP40 is ready to send data
 *
 * @param i2c_num I2C port number
 * @return true if device is ready, false otherwise
 */
bool sgp40_is_data_ready(int i2c_num);

#ifdef __cplusplus
}
#endif

#endif // SGP40_H
