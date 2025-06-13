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
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"

/* Constants and Definitions */
static const char *TAG = "ESP32_SAMPLE";

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

// Task priorities
#define LED_TASK_PRIORITY       2
#define BUTTON_TASK_PRIORITY    3
#define DISPLAY_TASK_PRIORITY   2

// Global variables
static bool led_state = false;
static int button_press_count = 0;

/* Display handles */
static esp_lcd_panel_io_handle_t panel_io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static uint8_t *frame_buffer = NULL;

// Forward declarations
static void configure_oled(void);
static void display_text(const char* text, int x, int y);

/**
 * @brief Configure GPIO pins for LED and button
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

    // Configure button pin as input with pull-up
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&button_config);

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
 * @brief Button monitoring task
 *
 * This task monitors button presses and prints count to console
 */
static void button_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Button task started");

    bool last_button_state = true;  // Pull-up means button not pressed = 1
    bool current_button_state;

    while (1) {
        current_button_state = gpio_get_level(BUTTON_PIN);

        // Detect button press (falling edge: 1 -> 0)
        if (last_button_state && !current_button_state) {
            button_press_count++;
            ESP_LOGI(TAG, "Button pressed! Count: %d", button_press_count);

            // Simple debounce delay
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        last_button_state = current_button_state;

        // Check button state every 10ms
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief System status task
 *
 * This task prints system information periodically
 */
static void status_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Status task started");

    while (1) {
        // Print system information every 10 seconds
        ESP_LOGI(TAG, "=== System Status ===");
        ESP_LOGI(TAG, "Uptime: %lld seconds", esp_timer_get_time() / 1000000);
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Button presses: %d", button_press_count);
        ESP_LOGI(TAG, "LED state: %s", led_state ? "ON" : "OFF");
        ESP_LOGI(TAG, "==================");

        // Wait for 10 seconds
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/**
 * @brief Display task
 *
 * This task updates the SSD1306 display with system information using frame buffer approach
 */
static void display_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Display task started");

    char line_buffer[32];

    while (1) {
        // Clear the frame buffer
        if (frame_buffer) {
            memset(frame_buffer, 0, SSD1306_WIDTH * SSD1306_HEIGHT / 8);
        }

        // Line 1: Title
        display_text("ESP32 Monitor", 0, 0);

        // Line 2: Uptime
        uint64_t uptime_seconds = esp_timer_get_time() / 1000000;
        snprintf(line_buffer, sizeof(line_buffer), "Time: %llu s", uptime_seconds);
        display_text(line_buffer, 0, 8);

        // Line 3: Button count
        snprintf(line_buffer, sizeof(line_buffer), "Button: %d", button_press_count);
        display_text(line_buffer, 0, 16);

        // Line 4: LED status
        snprintf(line_buffer, sizeof(line_buffer), "LED: %s", led_state ? "ON" : "OFF");
        display_text(line_buffer, 0, 24);

        // Line 5: Free heap
        snprintf(line_buffer, sizeof(line_buffer), "Heap: %lu B", esp_get_free_heap_size());
        display_text(line_buffer, 0, 32);

        ESP_LOGI(TAG, "Display updated - LED: %s, Button: %d, Uptime: %llu",
                 led_state ? "ON" : "OFF", button_press_count, uptime_seconds);

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

    // Clear the frame buffer (calloc already zeroed it, but being explicit)
    memset(frame_buffer, 0, SSD1306_WIDTH * SSD1306_HEIGHT / 8);

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
 * Uses an 8x8 font to render text to the frame buffer and updates the display.
 * Supports ASCII characters from 32 (space) to 122 (z).
 */
static void display_text(const char* text, int x, int y)
{
    if (!frame_buffer || !panel_handle) {
        ESP_LOGE(TAG, "Display not initialized");
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

    // Update display using standard panel API
    // The SSD1306 panel driver will handle the low-level I2C commands
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle,
                                            0, 0,  // Start from display origin
                                            SSD1306_WIDTH, SSD1306_HEIGHT, // Full display dimensions
                                            frame_buffer)); // Our frame buffer
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
    ESP_LOGI(TAG, "Initial test message displayed");
    vTaskDelay(pdMS_TO_TICKS(2000)); // Show for 2 seconds

    // Create tasks
    xTaskCreate(led_task, "led_task", 2048, NULL, LED_TASK_PRIORITY, NULL);
    xTaskCreate(button_task, "button_task", 2048, NULL, BUTTON_TASK_PRIORITY, NULL);
    xTaskCreate(status_task, "status_task", 4096, NULL, 1, NULL);
    xTaskCreate(display_task, "display_task", 4096, NULL, DISPLAY_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "Application initialization complete");
}
