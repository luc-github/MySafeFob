/**
 * @file touch.c
 * @brief MySafeFob Factory — X4 Pro GT911 touch driver.
 *
 * Assembles the sequences validated on the x4pro-probe/main/main.c probe:
 *  - gt911_begin()      : POR dance under reset (self-load guaranteed)
 *  - cmd_touchcfg       : uploads 480x800 host config if 0x8047 == 0x00
 *  - cmd_touchdump/info : status + points reading, 4-corner mapping
 *
 * Config table: based on Staars/GT911_ESP32 GoodixFW.h (g911xOrig 1024x600)
 * adapted to 480x800 portrait; checksum recomputed at runtime (the original
 * table was corrupted — total 0x2C instead of 0x00).
 */
#include "touch.h"
#include "hw_config.h"
#include "i2c_bus.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

static const char *TAG = "touch";

/* GT911 registers */
#define GT_REG_CFG_VER   0x8047
#define GT_REG_PRODUCT   0x8140
#define GT_REG_STATUS    0x814E
#define GT_REG_POINTS    0x8150
#define GT_REG_CFG_FRESH 0x8100

static i2c_master_bus_handle_t s_bus = NULL;
static uint8_t s_addr = 0;

/* Host 480x800 config (185 bytes @0x8047, [184] = recomputed checksum). */
static const uint8_t s_cfg_480x800[185] = {
    0x81, 0xE0, 0x01, 0x20, 0x03, 0x0A, 0x0C, 0x20, 0x01, 0x08, 0x28, 0x05, 0x50, 0x3C, 0x03, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x89, 0x2A, 0x0B, 0x2D, 0x2B,
    0x0F, 0x0A, 0x00, 0x00, 0x01, 0xA9, 0x03, 0x2D, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x21, 0x59, 0x94, 0xC5, 0x02, 0x07, 0x00, 0x00, 0x04, 0x93, 0x24, 0x00, 0x7D,
    0x2C, 0x00, 0x6B, 0x36, 0x00, 0x5D, 0x42, 0x00, 0x53, 0x50, 0x00, 0x53, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x04, 0x06, 0x08, 0x0A, 0x0F, 0x10, 0x12, 0x16, 0x18, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22,
    0x24, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* [184] = checksum, recomputed */
};

static void rails_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RAIL_PERIPH_PIN) | (1ULL << RAIL_TOUCH_PIN) |
                        (1ULL << RAIL_SD_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(RAIL_PERIPH_PIN, 1);
    gpio_set_level(RAIL_TOUCH_PIN, 0);   /* touch on (active-low) */
    gpio_set_level(RAIL_SD_PIN, 1);      /* SD pulse start */
    vTaskDelay(pdMS_TO_TICKS(80));
    gpio_set_level(RAIL_SD_PIN, 0);      /* SD on (active-low) */
    vTaskDelay(pdMS_TO_TICKS(120));
}

/* SHARED I2C bus (i2c_bus.c) — RTC/gauge use it too. Never destroy it
 * here (i2c_del_master_bus) or it would break the other drivers; on a
 * touch failure, we simply give up locally (s_addr stays 0), the shared
 * bus survives. */
static esp_err_t i2c_bus_open(void)
{
    return i2c_bus_get(&s_bus);
}

static esp_err_t i2c_dev_open(uint16_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, out);
}

/* 16-bit register read (>= 2 bytes: 1-byte reads get NACKed,
 * an IDF 5.4 I2C driver quirk validated on this hardware). */
static esp_err_t reg_read(uint16_t reg, uint8_t *out, int len)
{
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_dev_open(s_addr, &dev) != ESP_OK) return ESP_FAIL;
    uint8_t addr16[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
    esp_err_t err = i2c_master_transmit_receive(dev, addr16, 2, out, len,
                                                pdMS_TO_TICKS(100));
    i2c_master_bus_rm_device(dev);
    return err;
}

static esp_err_t reg_write(uint16_t reg, const uint8_t *data, int len)
{
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_dev_open(s_addr, &dev) != ESP_OK) return ESP_FAIL;
    uint8_t buf[2 + 185];
    if (len > 185) len = 185;
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    memcpy(buf + 2, data, len);
    esp_err_t err = i2c_master_transmit(dev, buf, (size_t)(2 + len),
                                        pdMS_TO_TICKS(200));
    i2c_master_bus_rm_device(dev);
    return err;
}

/* Full POR dance (FreeInk xteink-x4pro-support.md): self-load /
 * config upload is only guaranteed if POR happens with RST asserted.
 * Validated pitfalls: exact delays in us (pdMS_TO_TICKS(2)/(8) = 0 tick),
 * INT low BEFORE rail-off for address 0x5D and config mode. */
static void gt911_por_dance(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << TOUCH_RST_PIN) | (1ULL << TOUCH_INT_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(TOUCH_RST_PIN, 0);   /* RST low, held throughout the POR */
    gpio_set_level(TOUCH_INT_PIN, 0);   /* INT low -> address 0x5D */
    gpio_set_level(RAIL_TOUCH_PIN, 1);  /* rail off */
    esp_rom_delay_us(50000);
    gpio_set_level(RAIL_TOUCH_PIN, 0);  /* rail on: POR with RST asserted */
    esp_rom_delay_us(50000);

    esp_rom_delay_us(2000);             /* RST low 2 ms (doc) */
    gpio_set_level(TOUCH_RST_PIN, 1);   /* release */
    esp_rom_delay_us(8000);             /* INT low 8 ms after release (doc) */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(TOUCH_INT_PIN);
    esp_rom_delay_us(60000);            /* wait for config self-load */
}

/* Host config upload in CONFIG UPDATE mode (INT held LOW during the
 * write). Necessary on this batch: OTP config blank (0x8047 == 0x00). */
static bool gt911_upload_config(void)
{
    /* Enter update mode via POR: INT low + RST low BEFORE the rail
       powers on (whichever comes first, measured 23:37). */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(TOUCH_INT_PIN, 0);
    gpio_set_level(TOUCH_RST_PIN, 0);
    gpio_set_level(RAIL_TOUCH_PIN, 1);
    esp_rom_delay_us(50000);
    gpio_set_level(RAIL_TOUCH_PIN, 0);  /* rail on, POR under reset+INT low */
    esp_rom_delay_us(50000);
    gpio_set_level(TOUCH_RST_PIN, 1);   /* release RST, INT low on the rising edge */
    esp_rom_delay_us(60000);

    uint8_t buf[185];
    memcpy(buf, s_cfg_480x800, 185);
    uint32_t sum = 0;
    for (int i = 0; i < 184; i++) sum += buf[i];
    buf[184] = (uint8_t)((0x100 - (sum & 0xFF)) & 0xFF);  /* total == 0 mod 256 */

    if (reg_write(GT_REG_CFG_VER, buf, 185) != ESP_OK) return false;
    uint8_t fresh[1] = {0x01};
    if (reg_write(GT_REG_CFG_FRESH, fresh, 1) != ESP_OK) return false;
    esp_rom_delay_us(200000);

    /* Release INT: the RAM config stays active without a reset (decisive C2 test). */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(TOUCH_INT_PIN);
    esp_rom_delay_us(100000);

    /* Check: the config must read back 0x81. */
    uint8_t ver[2] = {0};
    if (reg_read(GT_REG_CFG_VER, ver, 2) != ESP_OK) return false;
    return ver[0] == 0x81;
}

/* RST/INT-ONLY dance, without power-cycling the touch rail (the rail is
 * already powered once by rails_init()). Same timings (2/8/60 ms) as
 * gt911_por_dance(), but WITHOUT the extra GPIO2 off/on power-cycle it
 * does. Reference: freeink-sdk (docs/xteink-x4pro-support.md, Ghidra RE
 * of the OEM firmware + confirmed on hardware) — on this controller the
 * internal config self-load works with THIS minimal dance; a
 * "0x8047 == 0x00" there is diagnosed as too-short a reset, not a blank
 * OTP. To be tried first before falling back to our dance + host upload
 * (already validated). */
static void gt911_self_load_dance(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << TOUCH_RST_PIN) | (1ULL << TOUCH_INT_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(TOUCH_RST_PIN, 0);   /* RST low */
    gpio_set_level(TOUCH_INT_PIN, 0);   /* INT low -> address 0x5D */
    esp_rom_delay_us(2000);             /* RST low 2 ms (doc) */
    gpio_set_level(TOUCH_RST_PIN, 1);   /* release */
    esp_rom_delay_us(8000);             /* INT low 8 ms after release (doc) */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(TOUCH_INT_PIN);
    esp_rom_delay_us(60000);            /* wait for config self-load */
}

/* Probe 0x5D / 0x14 (GT911 invisible to a classic scan: 16-bit
 * registers). Identified by reading the product ID (>= 2 bytes). */
static bool gt911_probe_address(void)
{
    uint8_t addrs[2] = {0x14, 0x5D};
    for (int a = 0; a < 2; a++) {
        i2c_master_dev_handle_t dev = NULL;
        if (i2c_dev_open(addrs[a], &dev) != ESP_OK) continue;
        uint8_t reg16[2] = {0x81, 0x40};
        uint8_t id[2] = {0};
        esp_err_t err = i2c_master_transmit_receive(dev, reg16, 2, id, 2,
                                                    pdMS_TO_TICKS(100));
        i2c_master_bus_rm_device(dev);
        if (err == ESP_OK) {
            s_addr = addrs[a];
            return true;
        }
    }
    return false;
}

bool touch_init(void)
{
    if (s_bus && s_addr) return true;

    rails_init();

    /* Attempt 1: "freeink-style" self-load (rail already powered, RST/INT
     * dance without power-cycle). If 0x8047 comes back non-zero, we keep
     * the auto-loaded config (and potentially the real GT911 capacitive
     * Home key, see touch_read()) without ever touching the host upload. */
    gt911_self_load_dance();
    if (i2c_bus_open() != ESP_OK) {
        ESP_LOGE(TAG, "i2c_bus_open FAILED (bus 39/38)");
        s_bus = NULL;
        return false;
    }
    if (gt911_probe_address()) {
        ESP_LOGI(TAG, "probe OK (self-load), addr 0x%02X", s_addr);
        uint8_t ver[2] = {0};
        if (reg_read(GT_REG_CFG_VER, ver, 2) == ESP_OK && ver[0] != 0x00) {
            ESP_LOGI(TAG, "self-load OK, cfg version 0x%02X (no upload)",
                     ver[0]);
            return true;
        }
        ESP_LOGW(TAG, "self-load FAILED (cfg version 0x%02X) -> fallback dance+upload",
                 ver[0]);
    } else {
        ESP_LOGW(TAG, "probe FAILED after self-load dance -> fallback dance+upload");
    }

    /* Attempt 2 (fallback already validated on this unit): full dance
     * with rail power-cycle + host config upload if needed. */
    s_addr = 0;
    gt911_por_dance();
    if (!gt911_probe_address()) {
        ESP_LOGE(TAG, "probe: no GT911 address responded (0x14/0x5D)");
        /* DO NOT destroy s_bus: shared bus (i2c_bus.c), RTC/gauge also
         * depend on it. We just give up locally. */
        return false;
    }
    ESP_LOGI(TAG, "probe OK (fallback), addr 0x%02X", s_addr);

    uint8_t ver[2] = {0};
    if (reg_read(GT_REG_CFG_VER, ver, 2) != ESP_OK || ver[0] == 0x00) {
        ESP_LOGI(TAG, "cfg version 0x%02X -> uploading host config",
                 ver[0]);
        if (!gt911_upload_config()) {
            ESP_LOGE(TAG, "config upload FAILED");
            return false;   /* the chip doesn't scan without config */
        }
        ESP_LOGI(TAG, "host config uploaded (480x800)");
    }
    return true;
}

touch_point_t touch_read(void)
{
    touch_point_t pt = { .pressed = false, .x = -1, .y = -1 };
    if (!s_bus || !s_addr) return pt;

    uint8_t st[2] = {0};
    if (reg_read(GT_REG_STATUS, st, 2) != ESP_OK) return pt;
    if (!(st[0] & 0x80)) return pt;   /* no data */

    uint8_t pts[8] = {0};
    if (reg_read(GT_REG_POINTS, pts, 8) == ESP_OK) {
        int16_t raw_x = (int16_t)(pts[0] | (pts[1] << 8));
        int16_t raw_y = (int16_t)(pts[2] | (pts[3] << 8));
        /* Home = software zone OR the real GT911 capacitive key (bit
         * 0x10 of 0x814E — freeink-sdk, Ghidra RE of the OEM firmware:
         * "the OEM keys off exactly 0x814E & 0x10"). The bit has never
         * come up with our uploaded config; if self-load
         * (gt911_self_load_dance) ever succeeds, it might work directly.
         * The two coexist, with no risk.
         * Zone RECALIBRATED 2026-09-15 (real log, factory build 12:25):
         * repeated presses on the physical Home pad -> raw (x=2..8, y=693..696),
         * very stable. The old measurement (36,479, doc 01:34) no longer
         * matches anything with the currently uploaded config -> replaced. */
        pt.home = (raw_x < 70 && raw_y >= 660 && raw_y <= 720) ||
                  (st[0] & 0x10) != 0;
        /* Raw values already PORTRAIT (4-corner test 00:55: TL=(48,58) TR=(475,80)
         * BR=(476,660) BL=(52,661) -> raw_x = user x, raw_y = user y).
         * The UI is in portrait 480x800 coords since the gfx fix (transpose). */
        pt.x = raw_x;
        pt.y = raw_y;
        pt.pressed = true;
    }

    /* Free the buffer for next time. */
    uint8_t clr[3] = {0x81, 0x4E, 0x00};
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_dev_open(s_addr, &dev) == ESP_OK) {
        i2c_master_transmit(dev, clr, sizeof(clr), pdMS_TO_TICKS(100));
        i2c_master_bus_rm_device(dev);
    }
    return pt;
}
