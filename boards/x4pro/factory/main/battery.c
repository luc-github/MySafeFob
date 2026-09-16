/**
 * @file battery.c
 * @brief MySafeFob Factory — jauge CW2017 (I2C) + charge (GPIO21).
 */
#include "battery.h"
#include "hw_config.h"
#include "i2c_bus.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

static bool s_gpio_ready = false;

bool battery_read(uint8_t *soc_percent, bool *charging)
{
    if (!s_gpio_ready) {
        gpio_config_t io = { .pin_bit_mask = 1ULL << CHARGE_PIN, .mode = GPIO_MODE_INPUT };
        gpio_config(&io);
        s_gpio_ready = true;
    }
    if (charging) {
        *charging = gpio_get_level(CHARGE_PIN) != 0;
    }

    i2c_master_bus_handle_t bus = NULL;
    if (i2c_bus_get(&bus) != ESP_OK) return false;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GAUGE_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) return false;

    uint8_t reg = 0x04;   /* SoC, % direct (docs/hardware-specs.md) */
    uint8_t soc = 0;
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, &soc, 1,
                                                pdMS_TO_TICKS(100));
    i2c_master_bus_rm_device(dev);

    if (err != ESP_OK) return false;
    if (soc_percent) *soc_percent = soc;
    return true;
}
