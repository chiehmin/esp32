/**
 * ESP32 Simple Sample Project
 *
 * This is a basic ESP32 project demonstrating:
 * - GPIO control for LED blinking
 * - Button input handling
 * - Serial communication
 * - FreeRTOS task management
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

/* WiFi includes */
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

/* HTTP Server includes */
#include "esp_http_server.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"

/* SCD41 sensor component */
#include "scd41.h"

/* Include WiFi credentials */
#include "wifi_credentials.h"

/* Constants and Definitions */
static const char *TAG = "air_quality_monitor";

// GPIO pin definitions
#define LED_PIN         GPIO_NUM_2      // Built-in LED on most ESP32 boards
#define BUTTON_PIN      GPIO_NUM_0      // Boot button on most ESP32 boards

// I2C definitions
#define I2C_MASTER_SCL_IO       22      // GPIO number for I2C master clock
#define I2C_MASTER_SDA_IO       21      // GPIO number for I2C master data
#define I2C_MASTER_NUM          0       // I2C master i2c port number
#define I2C_MASTER_FREQ_HZ      100000  // I2C master clock frequency
#define I2C_MASTER_TX_BUF_LEN   0       // I2C master doesn't need buffer
#define I2C_MASTER_RX_BUF_LEN   0       // I2C master doesn't need buffer
#define I2C_MASTER_TIMEOUT_MS   1000

// SSD1306 definitions
#define SSD1306_I2C_ADDR        0x3C    // SSD1306 I2C address
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64

/* WiFi definitions */
#define WIFI_MAXIMUM_RETRY      5

// Task priorities
#define LED_TASK_PRIORITY       2
#define DISPLAY_TASK_PRIORITY   2
#define SENSOR_TASK_PRIORITY    3

// Global variables
static bool led_state = false;
static char wifi_status[32] = "WiFi Disconnected";
static char ip_address[32] = "";
static scd41_data_t sensor_data = {0};
static httpd_handle_t server = NULL;

/* Display handles */
static esp_lcd_panel_io_handle_t panel_io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static uint8_t *frame_buffer = NULL;
static uint8_t *prev_frame_buffer = NULL;  // Previous frame for comparison

// Forward declarations
static void configure_oled(void);
static void display_text(const char* text, int x, int y);
static void clear_line(int y);
static void update_display_changes(void);
static void wifi_init_sta(void);
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void sensor_task(void *pvParameters);
static esp_err_t metrics_get_handler(httpd_req_t *req);
static httpd_handle_t start_webserver(void);
static void sensor_task(void *pvParameters);

/**
 * @brief Configure GPIO pins for LED
 */
static void configure_gpio(void)
{
    ESP_LOGI(TAG, "Configuring GPIO pins");

    // Configure LED pin as output
    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_config);

    // Initialize LED to OFF
    gpio_set_level(LED_PIN, 0);
    ESP_LOGI(TAG, "GPIO configuration complete");
}

/**
 * @brief LED blinking task
 *
 * This task toggles the LED every second and prints status to console
 */
static void led_task(void *pvParameters)
{
    ESP_LOGI(TAG, "LED task started");

    while (1) {
        // Toggle LED state
        led_state = !led_state;
        gpio_set_level(LED_PIN, led_state);

        // Log LED status
        ESP_LOGI(TAG, "LED %s", led_state ? "ON" : "OFF");

        // Wait for 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Display task
 *
 * This task updates the SSD1306 display with system information using frame buffer approach
 * with optimized updates that only refresh changed areas
 */
static void display_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Display task started");

    char line_buffer[32];

    while (1) {
        // Line 1: Title (static, only set once)
        display_text("Air Quality", 0, 0);

        // Line 2: CO2 level
        clear_line(16);
        if (sensor_data.data_ready) {
            snprintf(line_buffer, sizeof(line_buffer), "CO2: %d ppm", sensor_data.co2_ppm);
        } else {
            snprintf(line_buffer, sizeof(line_buffer), "CO2: --- ppm");
        }
        display_text(line_buffer, 0, 16);

        // Line 3: Temperature and Humidity
        clear_line(32);
        if (sensor_data.data_ready) {
            snprintf(line_buffer, sizeof(line_buffer), "T:%.1fC, H:%.1f%%", sensor_data.temperature, sensor_data.humidity);
        } else {
            snprintf(line_buffer, sizeof(line_buffer), "T:--.-C H:--.%%");
        }
        display_text(line_buffer, 0, 32);

        // Line 4: WiFi status
        clear_line(48);
        if (strlen(ip_address) > 0) {
            // Show IP if connected
            display_text(ip_address, 0, 48);
        } else {
            // Show WiFi status if not connected
            display_text(wifi_status, 0, 48);
        }

        // Now update only the changed parts of the display
        update_display_changes();

        // Update display every 2 seconds
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/**
 * @brief Basic 8x8 font bitmap array - first 32 ASCII characters are non-printable
 */
static const uint8_t FONT_8X8[] = {
    // Basic ASCII characters from 32 (space) to 90 (Z)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 32 - Space
    0x00, 0x00, 0x00, 0x4F, 0x4F, 0x00, 0x00, 0x00,   // 33 - !
    0x00, 0x07, 0x07, 0x00, 0x00, 0x07, 0x07, 0x00,   // 34 - "
    0x14, 0x7F, 0x7F, 0x14, 0x14, 0x7F, 0x7F, 0x14,   // 35 - #
    0x24, 0x2E, 0x6B, 0x6B, 0x3A, 0x12, 0x00, 0x00,   // 36 - $
    0x63, 0x33, 0x18, 0x0C, 0x06, 0x33, 0x63, 0x00,   // 37 - %
    0x32, 0x7F, 0x4D, 0x4D, 0x77, 0x72, 0x50, 0x00,   // 38 - &
    0x00, 0x00, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00,   // 39 - '
    0x00, 0x1C, 0x3E, 0x63, 0x41, 0x00, 0x00, 0x00,   // 40 - (
    0x00, 0x41, 0x63, 0x3E, 0x1C, 0x00, 0x00, 0x00,   // 41 - )
    0x08, 0x2A, 0x3E, 0x1C, 0x1C, 0x3E, 0x2A, 0x08,   // 42 - *
    0x08, 0x08, 0x3E, 0x3E, 0x08, 0x08, 0x00, 0x00,   // 43 - +
    0x00, 0x80, 0xE0, 0x60, 0x00, 0x00, 0x00, 0x00,   // 44 - ,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00,   // 45 - -
    0x00, 0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00,   // 46 - .
    0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00,   // 47 - /
    0x3E, 0x7F, 0x71, 0x59, 0x4D, 0x7F, 0x3E, 0x00,   // 48 - 0
    0x40, 0x42, 0x7F, 0x7F, 0x40, 0x40, 0x00, 0x00,   // 49 - 1
    0x62, 0x73, 0x59, 0x49, 0x6F, 0x66, 0x00, 0x00,   // 50 - 2
    0x22, 0x63, 0x49, 0x49, 0x7F, 0x36, 0x00, 0x00,   // 51 - 3
    0x18, 0x1C, 0x16, 0x13, 0x7F, 0x7F, 0x10, 0x00,   // 52 - 4
    0x27, 0x67, 0x45, 0x45, 0x7D, 0x39, 0x00, 0x00,   // 53 - 5
    0x3C, 0x7E, 0x4B, 0x49, 0x79, 0x30, 0x00, 0x00,   // 54 - 6
    0x03, 0x03, 0x71, 0x79, 0x0F, 0x07, 0x00, 0x00,   // 55 - 7
    0x36, 0x7F, 0x49, 0x49, 0x7F, 0x36, 0x00, 0x00,   // 56 - 8
    0x06, 0x4F, 0x49, 0x69, 0x3F, 0x1E, 0x00, 0x00,   // 57 - 9
    0x00, 0x00, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00,   // 58 - :
    0x00, 0x80, 0xEC, 0x6C, 0x00, 0x00, 0x00, 0x00,   // 59 - ;
    0x08, 0x1C, 0x36, 0x63, 0x41, 0x00, 0x00, 0x00,   // 60 - <
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00,   // 61 - =
    0x00, 0x41, 0x63, 0x36, 0x1C, 0x08, 0x00, 0x00,   // 62 - >
    0x02, 0x03, 0x51, 0x59, 0x0F, 0x06, 0x00, 0x00,   // 63 - ?
    0x3E, 0x7F, 0x41, 0x5D, 0x5D, 0x1F, 0x1E, 0x00,   // 64 - @
    0x7E, 0x7F, 0x09, 0x09, 0x7F, 0x7E, 0x00, 0x00,   // 65 - A
    0x41, 0x7F, 0x7F, 0x49, 0x49, 0x7F, 0x36, 0x00,   // 66 - B
    0x1C, 0x3E, 0x63, 0x41, 0x41, 0x63, 0x22, 0x00,   // 67 - C
    0x41, 0x7F, 0x7F, 0x41, 0x63, 0x3E, 0x1C, 0x00,   // 68 - D
    0x41, 0x7F, 0x7F, 0x49, 0x5D, 0x41, 0x63, 0x00,   // 69 - E
    0x41, 0x7F, 0x7F, 0x49, 0x1D, 0x01, 0x03, 0x00,   // 70 - F
    0x1C, 0x3E, 0x63, 0x41, 0x51, 0x73, 0x72, 0x00,   // 71 - G
    0x7F, 0x7F, 0x08, 0x08, 0x7F, 0x7F, 0x00, 0x00,   // 72 - H
    0x00, 0x41, 0x7F, 0x7F, 0x41, 0x00, 0x00, 0x00,   // 73 - I
    0x30, 0x70, 0x40, 0x41, 0x7F, 0x3F, 0x01, 0x00,   // 74 - J
    0x41, 0x7F, 0x7F, 0x08, 0x1C, 0x77, 0x63, 0x00,   // 75 - K
    0x41, 0x7F, 0x7F, 0x41, 0x40, 0x60, 0x70, 0x00,   // 76 - L
    0x7F, 0x7F, 0x0E, 0x1C, 0x0E, 0x7F, 0x7F, 0x00,   // 77 - M
    0x7F, 0x7F, 0x06, 0x0C, 0x18, 0x7F, 0x7F, 0x00,   // 78 - N
    0x1C, 0x3E, 0x63, 0x41, 0x63, 0x3E, 0x1C, 0x00,   // 79 - O
    0x41, 0x7F, 0x7F, 0x49, 0x09, 0x0F, 0x06, 0x00,   // 80 - P
    0x1E, 0x3F, 0x21, 0x71, 0x7F, 0x5E, 0x00, 0x00,   // 81 - Q
    0x41, 0x7F, 0x7F, 0x09, 0x19, 0x7F, 0x66, 0x00,   // 82 - R
    0x26, 0x6F, 0x4D, 0x49, 0x59, 0x73, 0x32, 0x00,   // 83 - S
    0x03, 0x41, 0x7F, 0x7F, 0x41, 0x03, 0x00, 0x00,   // 84 - T
    0x7F, 0x7F, 0x40, 0x40, 0x7F, 0x7F, 0x00, 0x00,   // 85 - U
    0x1F, 0x3F, 0x60, 0x60, 0x3F, 0x1F, 0x00, 0x00,   // 86 - V
    0x7F, 0x7F, 0x30, 0x18, 0x30, 0x7F, 0x7F, 0x00,   // 87 - W
    0x43, 0x67, 0x3C, 0x18, 0x3C, 0x67, 0x43, 0x00,   // 88 - X
    0x07, 0x4F, 0x78, 0x78, 0x4F, 0x07, 0x00, 0x00,   // 89 - Y
    0x47, 0x63, 0x71, 0x59, 0x4D, 0x67, 0x73, 0x00,   // 90 - Z
    0x00, 0x1C, 0x3E, 0x63, 0x41, 0x00, 0x00, 0x00,   // 91 - [
    0x01, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00,   // 92 - backslash
    0x00, 0x41, 0x63, 0x3E, 0x1C, 0x00, 0x00, 0x00,   // 93 - ]
    0x08, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x08, 0x00,   // 94 - ^
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,   // 95 - _
    0x00, 0x00, 0x03, 0x07, 0x04, 0x00, 0x00, 0x00,   // 96 - `
    0x20, 0x74, 0x54, 0x54, 0x3C, 0x78, 0x40, 0x00,   // 97 - a
    0x41, 0x7F, 0x3F, 0x48, 0x48, 0x78, 0x30, 0x00,   // 98 - b
    0x38, 0x7C, 0x44, 0x44, 0x6C, 0x28, 0x00, 0x00,   // 99 - c
    0x30, 0x78, 0x48, 0x49, 0x3F, 0x7F, 0x40, 0x00,   // 100 - d
    0x38, 0x7C, 0x54, 0x54, 0x5C, 0x18, 0x00, 0x00,   // 101 - e
    0x48, 0x7E, 0x7F, 0x49, 0x03, 0x02, 0x00, 0x00,   // 102 - f
    0x98, 0xBC, 0xA4, 0xA4, 0xF8, 0x7C, 0x04, 0x00,   // 103 - g
    0x41, 0x7F, 0x7F, 0x08, 0x04, 0x7C, 0x78, 0x00,   // 104 - h
    0x00, 0x44, 0x7D, 0x7D, 0x40, 0x00, 0x00, 0x00,   // 105 - i
    0x60, 0xE0, 0x80, 0x80, 0xFD, 0x7D, 0x00, 0x00,   // 106 - j
    0x41, 0x7F, 0x7F, 0x10, 0x38, 0x6C, 0x44, 0x00,   // 107 - k
    0x00, 0x41, 0x7F, 0x7F, 0x40, 0x00, 0x00, 0x00,   // 108 - l
    0x7C, 0x7C, 0x0C, 0x18, 0x0C, 0x7C, 0x78, 0x00,   // 109 - m
    0x7C, 0x7C, 0x04, 0x04, 0x7C, 0x78, 0x00, 0x00,   // 110 - n
    0x38, 0x7C, 0x44, 0x44, 0x7C, 0x38, 0x00, 0x00,   // 111 - o
    0x84, 0xFC, 0xF8, 0xA4, 0x24, 0x3C, 0x18, 0x00,   // 112 - p
    0x18, 0x3C, 0x24, 0xA4, 0xF8, 0xFC, 0x84, 0x00,   // 113 - q
    0x44, 0x7C, 0x78, 0x4C, 0x04, 0x1C, 0x18, 0x00,   // 114 - r
    0x48, 0x5C, 0x54, 0x54, 0x74, 0x24, 0x00, 0x00,   // 115 - s
    0x00, 0x04, 0x3E, 0x7F, 0x44, 0x24, 0x00, 0x00,   // 116 - t
    0x3C, 0x7C, 0x40, 0x40, 0x3C, 0x7C, 0x40, 0x00,   // 117 - u
    0x1C, 0x3C, 0x60, 0x60, 0x3C, 0x1C, 0x00, 0x00,   // 118 - v
    0x3C, 0x7C, 0x60, 0x30, 0x60, 0x7C, 0x3C, 0x00,   // 119 - w
    0x44, 0x6C, 0x38, 0x10, 0x38, 0x6C, 0x44, 0x00,   // 120 - x
    0x9C, 0xBC, 0xA0, 0xA0, 0xFC, 0x7C, 0x00, 0x00,   // 121 - y
    0x4C, 0x64, 0x74, 0x5C, 0x4C, 0x64, 0x00, 0x00,   // 122 - z
};

/**
 * @brief Initialize LCD panel using ESP-IDF LCD panel API
 */
/**
 * @brief Configure the OLED display
 *
 * Initializes the SSD1306 OLED display with I2C interface and
 * allocates frame buffer for display operations.
 */
static void configure_oled(void)
{
    ESP_LOGI(TAG, "Initialize OLED panel");

    // Initialize I2C bus
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, 0, 0, 0));

    ESP_LOGI(TAG, "I2C initialized successfully");

    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = SSD1306_I2C_ADDR,
        .control_phase_bytes = 1,    // Number of bytes for command phase
        .lcd_cmd_bits = 8,          // Number of bits for command
        .lcd_param_bits = 8,        // Number of bits for parameter
        .dc_bit_offset = 6,         // Offset of data/command bit (D/C bit)
    };

    // Create panel IO handle
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_MASTER_NUM, &io_config, &panel_io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .color_space = ESP_LCD_COLOR_SPACE_MONOCHROME,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_handle, &panel_config, &panel_handle));

    // Initialize the LCD
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Fix display orientation - mirror both X and Y axis to correct upside-down display
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, true));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Allocate frame buffer
    frame_buffer = heap_caps_calloc(1, SSD1306_WIDTH * SSD1306_HEIGHT / 8, MALLOC_CAP_DMA);
    if (!frame_buffer) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer");
        return;
    }

    // Allocate previous frame buffer for comparison
    prev_frame_buffer = heap_caps_calloc(1, SSD1306_WIDTH * SSD1306_HEIGHT / 8, MALLOC_CAP_DMA);
    if (!prev_frame_buffer) {
        ESP_LOGE(TAG, "Failed to allocate previous frame buffer");
        free(frame_buffer);
        return;
    }

    // Clear both frame buffers
    memset(frame_buffer, 0, SSD1306_WIDTH * SSD1306_HEIGHT / 8);
    memset(prev_frame_buffer, 0, SSD1306_WIDTH * SSD1306_HEIGHT / 8);

    // Update the display with the cleared frame buffer
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, frame_buffer));
    ESP_LOGI(TAG, "OLED display initialized and cleared");
}

/**
 * @brief Display text on the OLED screen
 *
 * @param text The string to display
 * @param x The x coordinate (in pixels)
 * @param y The y coordinate (in pixels)
 *
 * Uses an 8x8 font to render text to the frame buffer.
 * Supports ASCII characters from 32 (space) to 122 (z).
 * Note: This function only updates the frame buffer, not the display.
 * Call update_display_changes() to actually update the display.
 */
static void display_text(const char* text, int x, int y)
{
    if (!frame_buffer) {
        ESP_LOGE(TAG, "Frame buffer not initialized");
        return;
    }

    int page = y / 8;  // Each page is 8 pixels high
    int len = strlen(text);

    // Clear the area we're going to write to
    memset(frame_buffer + (page * SSD1306_WIDTH) + x, 0, len * 8);

    // Write text to frame buffer
    for (int i = 0; i < len && (x + i * 8) < SSD1306_WIDTH; i++) {
        char c = text[i];
        // Handle printable ASCII characters (32-122: space to z)
        if (c >= 32 && c <= 122) {
            uint8_t* char_bitmap = (uint8_t*)&FONT_8X8[(c - 32) * 8];
            for (int col = 0; col < 8; col++) {
                if ((x + i * 8 + col) < SSD1306_WIDTH) {
                    frame_buffer[page * SSD1306_WIDTH + x + i * 8 + col] = char_bitmap[col];
                }
            }
        }
    }
}

/**
 * @brief Update display with minimal changes
 *
 * Compares current frame buffer with previous frame and only updates changed areas
 */
static void update_display_changes(void)
{
    if (!frame_buffer || !prev_frame_buffer || !panel_handle) {
        ESP_LOGE(TAG, "Display buffers not initialized");
        return;
    }

    bool has_changes = false;
    size_t buffer_size = SSD1306_WIDTH * SSD1306_HEIGHT / 8;

    // Check if there are any changes using memcmp
    has_changes = (memcmp(frame_buffer, prev_frame_buffer, buffer_size) != 0);

    // Only update display if there are changes
    if (has_changes) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle,
                                                0, 0,  // Start from display origin
                                                SSD1306_WIDTH, SSD1306_HEIGHT, // Full display dimensions
                                                frame_buffer)); // Our frame buffer

        // Copy current frame to previous frame for next comparison
        memcpy(prev_frame_buffer, frame_buffer, buffer_size);
        ESP_LOGI(TAG, "Display updated with changes");
    } else {
        ESP_LOGI(TAG, "No display changes detected, skipping update");
    }
}

/**
 * @brief Clear a specific line on the display
 *
 * @param y The y coordinate (should be a multiple of 8)
 */
static void clear_line(int y)
{
    if (!frame_buffer) {
        ESP_LOGE(TAG, "Frame buffer not initialized");
        return;
    }

    int page = y / 8;  // Each page is 8 pixels high
    if (page < 0 || page >= (SSD1306_HEIGHT / 8)) {
        ESP_LOGE(TAG, "Invalid page number: %d", page);
        return;
    }

    // Clear the entire line (page)
    memset(frame_buffer + (page * SSD1306_WIDTH), 0, SSD1306_WIDTH);
}

/**
 * @brief WiFi event handler
 *
 * Handles WiFi connection events and updates display status
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting to WiFi...");
        strcpy(wifi_status, "WiFi Connecting");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Connection failed! Retrying...");
        strcpy(wifi_status, "WiFi Failed");
        strcpy(ip_address, "");  // Clear IP address
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        strcpy(wifi_status, "WiFi Connected");
        snprintf(ip_address, sizeof(ip_address), "IP:" IPSTR, IP2STR(&event->ip_info.ip));

        // Start HTTP server
        start_webserver();
    }
}

/**
 * @brief Initialize WiFi in station mode
 *
 * Configures and starts WiFi in station mode to connect to the specified access point.
 */
static void wifi_init_sta(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing WiFi");

    // Initialize TCP/IP adapter
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Configure WiFi station with SSID and password
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi initialization completed");
}

/**
 * @brief Sensor reading task
 */
static void sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor task started");

    // Initialize SCD41 with configuration
    scd41_config_t config = SCD41_CONFIG_DEFAULT();
    config.i2c_port = I2C_MASTER_NUM;
    config.timeout_ms = I2C_MASTER_TIMEOUT_MS;

    if (scd41_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SCD41 sensor");
        vTaskDelete(NULL);
        return;
    }

    // Start measurement
    if (scd41_start_measurement() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start SCD41 measurement");
        vTaskDelete(NULL);
        return;
    }

    // Wait for first measurement to be ready (SCD41 needs 5 seconds for first reading)
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        scd41_data_t new_data;
        if (scd41_read_measurement(&new_data) == ESP_OK) {
            // Update global sensor data
            sensor_data = new_data;
        } else {
            ESP_LOGW(TAG, "Failed to read sensor data");
            sensor_data.data_ready = false;
        }

        // SCD41 provides new data every 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * @brief HTTP handler for /metrics endpoint
 *
 * Formats current sensor data in Prometheus text format with scd41_ prefix
 */
static esp_err_t metrics_get_handler(httpd_req_t *req)
{
    char resp_str[1024];

    // Format metrics in Prometheus text format with scd41_ prefix to differentiate from ESP32S3
    int len = snprintf(resp_str, sizeof(resp_str),
        "# HELP scd41_temperature_celsius Temperature in Celsius from SCD41 sensor\n"
        "# TYPE scd41_temperature_celsius gauge\n"
        "scd41_temperature_celsius %.2f\n"
        "# HELP scd41_humidity_percent Relative humidity percentage from SCD41 sensor\n"
        "# TYPE scd41_humidity_percent gauge\n"
        "scd41_humidity_percent %.2f\n"
        "# HELP scd41_co2_ppm CO2 concentration in ppm from SCD41 sensor\n"
        "# TYPE scd41_co2_ppm gauge\n"
        "scd41_co2_ppm %" PRIu16 "\n"
        "# HELP esp32_uptime_seconds ESP32 uptime in seconds\n"
        "# TYPE esp32_uptime_seconds counter\n"
        "esp32_uptime_seconds %llu\n"
        "# HELP esp32_free_heap_bytes Free heap memory in bytes\n"
        "# TYPE esp32_free_heap_bytes gauge\n"
        "esp32_free_heap_bytes %" PRIu32 "\n",
        sensor_data.temperature,
        sensor_data.humidity,
        sensor_data.co2_ppm,
        esp_timer_get_time() / 1000000ULL,
        esp_get_free_heap_size()
    );

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp_str, len);
    return ESP_OK;
}

/**
 * @brief Start HTTP server and register handlers
 */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        httpd_uri_t metrics_uri = {
            .uri       = "/metrics",
            .method    = HTTP_GET,
            .handler   = metrics_get_handler,
            .user_ctx  = NULL
        };
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &metrics_uri);
        return server;
    }

    ESP_LOGI(TAG, "Error starting HTTP server!");
    return NULL;
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32 Sample Application");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());

    // Configure GPIO pins
    configure_gpio();

    // Initialize OLED display
    configure_oled();

    // Show initial test message
    display_text("SSD1306 Ready!", 0, 0);
    display_text("Starting WiFi...", 0, 16);
    display_text("Init SCD41...", 0, 32);
    update_display_changes();
    vTaskDelay(pdMS_TO_TICKS(2000)); // Show for 2 seconds

    // Clear display before starting tasks
    memset(frame_buffer, 0, SSD1306_WIDTH * SSD1306_HEIGHT / 8);
    update_display_changes();

    // Initialize WiFi
    wifi_init_sta();

    // Create tasks
    xTaskCreate(led_task, "led_task", 2048, NULL, LED_TASK_PRIORITY, NULL);
    xTaskCreate(display_task, "display_task", 4096, NULL, DISPLAY_TASK_PRIORITY, NULL);
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, SENSOR_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "Application initialization complete");
}
