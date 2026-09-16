#include "i2c_bus.h"
#include "hw_config.h"

static i2c_master_bus_handle_t s_bus = NULL;

esp_err_t i2c_bus_get(i2c_master_bus_handle_t *out)
{
    if (s_bus) {
        *out = s_bus;
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err == ESP_OK) {
        *out = s_bus;
    }
    return err;
}
