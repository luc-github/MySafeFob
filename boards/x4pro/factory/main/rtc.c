/**
 * @file rtc.c
 * @brief MySafeFob Factory — RTC BM8563, lecture seule (pas d'ecriture :
 *        la sync heure est une feature app, F-02, hors scope factory).
 */
#include "rtc.h"
#include "hw_config.h"
#include "i2c_bus.h"

#include "freertos/FreeRTOS.h"

static inline int bcd(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

bool rtc_read(rtc_time_t *out)
{
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_bus_get(&bus) != ESP_OK) return false;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RTC_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) return false;

    uint8_t reg = 0x02;   /* VL_seconds..year, cf. docs/hardware-specs.md */
    uint8_t buf[7] = {0};
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, buf, sizeof(buf),
                                                pdMS_TO_TICKS(200));
    i2c_master_bus_rm_device(dev);

    if (err != ESP_OK) return false;
    if (out) {
        out->second = bcd(buf[0] & 0x7F);
        out->minute = bcd(buf[1] & 0x7F);
        out->hour   = bcd(buf[2] & 0x3F);
        out->day    = bcd(buf[3] & 0x3F);
        out->month  = bcd(buf[5] & 0x1F);
        out->year   = 2000 + bcd(buf[6]);
    }
    return true;
}
