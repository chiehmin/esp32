#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"
#include "sgp30/sgp30.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_sh1106.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "main";

#define BLINK_GPIO CONFIG_BLINK_GPIO

#define I2C_HOST  0
#define I2C_SDA_GPIO  8   // Default SDA for ESP32-S3
#define I2C_SCL_GPIO  9   // Default SCL for ESP32-S3
#define I2C_HZ        400000   // 400kHz
#define OLED_I2C_ADDR          0x3C    // SH1106 default address
#define OLED_WIDTH             128
#define OLED_HEIGHT            64

// DHT11 definitions
#define DHT_GPIO 5  // You can change this to any available GPIO

static uint8_t s_led_state = 0;
typedef struct {
    float temperature;
    float humidity;
} dht11_reading;

// Display buffer size constants

#ifdef CONFIG_BLINK_LED_STRIP

static led_strip_handle_t led_strip;

static void blink_led(void)
{
    /* If the addressable LED is enabled */
    if (s_led_state) {
        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
        led_strip_set_pixel(led_strip, 0, 16, 16, 16);
        /* Refresh the strip to send data */
        led_strip_refresh(led_strip);
    } else {
        /* Set all LED off to clear all pixels */
        led_strip_clear(led_strip);
    }
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink addressable LED!");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
#else
#error "unsupported LED strip backend"
#endif
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
}

#elif CONFIG_BLINK_LED_GPIO

static void blink_led(void)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(BLINK_GPIO, s_led_state);
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

#else
#error "unsupported LED type"
#endif

static void configure_i2c(void)
{
    ESP_LOGI(TAG, "Initialize I2C");
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_HOST, i2c_conf.mode, 0, 0, 0));
}

static esp_lcd_panel_io_handle_t panel_io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static uint8_t *frame_buffer = NULL;

static void configure_oled(void)
{
    ESP_LOGI(TAG, "Initialize OLED panel");

    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = OLED_I2C_ADDR,
        .control_phase_bytes = 1,    // Number of bytes for command phase
        .lcd_cmd_bits = 8,          // Number of bits for command
        .lcd_param_bits = 8,        // Number of bits for parameter
        .dc_bit_offset = 6,         // Offset of data/command bit (D/C bit)
    };

    // Create panel IO handle
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_HOST, &io_config, &panel_io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .color_space = ESP_LCD_COLOR_SPACE_MONOCHROME,
        .bits_per_pixel = 1,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_handle, &panel_config, &panel_handle));

    // Initialize the LCD
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Allocate frame buffer
    frame_buffer = heap_caps_calloc(1, OLED_WIDTH * OLED_HEIGHT / 8, MALLOC_CAP_DMA);
    if (!frame_buffer) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer");
        return;
    }

    // Clear the frame buffer (calloc already zeroed it, but being explicit)
    memset(frame_buffer, 0, OLED_WIDTH * OLED_HEIGHT / 8);

    // Update the display with the cleared frame buffer
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, OLED_WIDTH, OLED_HEIGHT, frame_buffer));
    ESP_LOGI(TAG, "OLED display initialized and cleared");
}

// Basic 8x8 font bitmap array - first 32 ASCII characters are non-printable
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
};

static void display_text(const char* text, int x, int y)
{
    if (!frame_buffer || !panel_handle) {
        ESP_LOGE(TAG, "Display not initialized");
        return;
    }

    int page = y / 8;  // Each page is 8 pixels high
    int len = strlen(text);

    // Clear the area we're going to write to
    memset(frame_buffer + (page * OLED_WIDTH) + x, 0, len * 8);

    // Write text to frame buffer
    for (int i = 0; i < len && (x + i * 8) < OLED_WIDTH; i++) {
        char c = text[i];
        // Only handle printable ASCII characters (32-90 for now)
        if (c >= 32 && c <= 90) {
            uint8_t* char_bitmap = (uint8_t*)&FONT_8X8[(c - 32) * 8];
            for (int col = 0; col < 8; col++) {
                // Calculate position in frame buffer
                int buf_pos = (page * OLED_WIDTH) + x + (i * 8) + col;
                frame_buffer[buf_pos] = char_bitmap[col];
            }
        }
    }

    // Update display using standard panel API
    // The SH1106 panel driver will handle the low-level I2C commands
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle,
                                            0, 0,  // Start from display origin
                                            OLED_WIDTH, OLED_HEIGHT, // Full display dimensions
                                            frame_buffer)); // Our frame buffer
}

static void i2c_scan(void) {
    ESP_LOGI(TAG, "Scanning I2C bus...");
    for (uint8_t i = 0; i < 128; i++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (i << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_HOST, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found I2C device at address 0x%02x", i);
        }
    }
    ESP_LOGI(TAG, "I2C scan completed");
}

static void dht11_init(void)
{
    gpio_reset_pin(DHT_GPIO);
    gpio_set_direction(DHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_GPIO, 1);
}

static dht11_reading dht11_read(void)
{
    dht11_reading reading = {0};
    uint8_t data[5] = {0};
    int bit_index = 7;
    int byte_index = 0;

    // Disable interrupts during timing-critical sections
    portDISABLE_INTERRUPTS();

    // Start signal
    gpio_set_direction(DHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_GPIO, 0);
    esp_rom_delay_us(20000);  // At least 18ms low
    gpio_set_level(DHT_GPIO, 1);
    esp_rom_delay_us(40);     // 20-40us high
    gpio_set_direction(DHT_GPIO, GPIO_MODE_INPUT);

    // DHT response
    if (gpio_get_level(DHT_GPIO) == 1) {
        portENABLE_INTERRUPTS();
        ESP_LOGE(TAG, "DHT11 not responding");
        return reading;
    }

    // Wait for DHT11 to pull up
    esp_rom_delay_us(80);

    // Read 40 bits of data
    for (int i = 0; i < 40; i++) {
        // Wait for falling edge (start of bit)
        int timeout = 0;
        while (gpio_get_level(DHT_GPIO) == 1) {
            esp_rom_delay_us(1);
            timeout++;
            if (timeout > 100) {
                portENABLE_INTERRUPTS();
                ESP_LOGE(TAG, "DHT11 timeout waiting for bit start");
                return reading;
            }
        }

        // Wait for rising edge
        timeout = 0;
        while (gpio_get_level(DHT_GPIO) == 0) {
            esp_rom_delay_us(1);
            timeout++;
            if (timeout > 100) {
                portENABLE_INTERRUPTS();
                ESP_LOGE(TAG, "DHT11 timeout waiting for bit rise");
                return reading;
            }
        }

        // Wait ~30us and check pin state
        esp_rom_delay_us(30);

        // If pin is high after 30us, it's a '1' bit
        if (gpio_get_level(DHT_GPIO)) {
            data[byte_index] |= (1 << bit_index);
        }

        // Next bit
        bit_index--;
        if (bit_index < 0) {
            bit_index = 7;
            byte_index++;
        }
    }

    // Re-enable interrupts
    portENABLE_INTERRUPTS();

    // Verify checksum
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (data[4] == checksum) {
        reading.humidity = data[0] + (data[1] * 0.1);
        reading.temperature = data[2] + (data[3] * 0.1);

        // Validate readings are in reasonable range
        if (reading.temperature >= 0 && reading.temperature <= 50 &&
            reading.humidity >= 20 && reading.humidity <= 95) {
            ESP_LOGI(TAG, "DHT11 read success: %.1f°C, %.1f%%", reading.temperature, reading.humidity);
        } else {
            ESP_LOGE(TAG, "DHT11 readings out of range: %.1f°C, %.1f%%", reading.temperature, reading.humidity);
            reading.temperature = 0;
            reading.humidity = 0;
        }
    } else {
        ESP_LOGE(TAG, "DHT11 checksum error: got %02x, expected %02x", data[4], checksum);
    }

    return reading;
}

void app_main(void)
{
    // Configure LED
    configure_led();

    // Initialize I2C
    configure_i2c();
    i2c_scan();

    // Initialize OLED display
    configure_oled();

    // Initialize DHT11
    ESP_LOGI(TAG, "Initializing DHT11 sensor...");
    dht11_init();

    // Initialize SGP30
    ESP_LOGI(TAG, "Initializing SGP30 sensor...");
    sgp30_config_t sgp30_config = {
        .i2c_master_port = I2C_HOST,
        .i2c_address = 0x58, // Default SGP30 address
    };
    if (sgp30_init(&sgp30_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SGP30");
        display_text("SGP30 Error!", 0, 0);
        return;
    }

    char line[32];
    uint16_t co2, tvoc;
    dht11_reading dht;

    uint8_t serial_id[6];
    if (sgp30_get_serial_id(serial_id) == ESP_OK) {
        ESP_LOGI(TAG, "Serial ID: %02X%02X%02X%02X%02X%02X\n", serial_id[0],
                   serial_id[1], serial_id[2], serial_id[3], serial_id[4],
                   serial_id[5]);
    }

    while (1) {
        // Read SGP30 data
        if (sgp30_read_measurements(&co2, &tvoc) == ESP_OK) {
            // Display CO2 and TVOC values
            snprintf(line, sizeof(line), "CO2: %d ppm", co2);
            display_text(line, 0, 0);
            snprintf(line, sizeof(line), "TVOC: %d ppb", tvoc);
            display_text(line, 0, 16);
            ESP_LOGI(TAG, "CO2: %d ppm, TVOC: %d ppb", co2, tvoc);
        } else {
            ESP_LOGE(TAG, "Failed to read SGP30");
            display_text("SGP30 Error", 0, 0);
            display_text("Check Wiring", 0, 16);
        }

        // Read DHT11 data (with retry on failure)
        static float last_valid_temp = 0;
        static float last_valid_humidity = 0;

        dht = dht11_read();
        if (dht.temperature != 0 || dht.humidity != 0) {
            last_valid_temp = dht.temperature;
            last_valid_humidity = dht.humidity;
            snprintf(line, sizeof(line), "T:%.1fC H:%.1f%%", dht.temperature, dht.humidity);
            display_text(line, 0, 32);
            ESP_LOGI(TAG, "Temp: %.1f°C, Humidity: %.1f%%", dht.temperature, dht.humidity);
        } else if (last_valid_temp != 0 || last_valid_humidity != 0) {
            // Show last valid reading if available
            snprintf(line, sizeof(line), "T:%.1fC* H:%.1f%%*", last_valid_temp, last_valid_humidity);
            display_text(line, 0, 32);
            ESP_LOGW(TAG, "Using last valid DHT11 reading");
        } else {
            ESP_LOGE(TAG, "Failed to read DHT11");
            display_text("DHT11 Error", 0, 32);
        }

        /* Toggle LED state */
        s_led_state = !s_led_state;
        blink_led();

        // Wait 2 seconds before next reading (DHT11 needs at least 1s between reads)
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
