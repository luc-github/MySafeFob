/**
 * @file touch.c
 * @brief MySafeFob Factory — driver touch GT911 X4 Pro.
 *
 * Assemble les sequences validees du probe x4pro-probe/main/main.c :
 *  - gt911_begin()      : dance POR sous reset (self-load garanti)
 *  - cmd_touchcfg       : upload config 480x800 hote si 0x8047 == 0x00
 *  - cmd_touchdump/info : lecture status + points, mapping 4 coins
 *
 * Table config : base Staars/GT911_ESP32 GoodixFW.h (g911xOrig 1024x600)
 * adaptee 480x800 portrait ; checksum recalcule a l'execution (la table
 * d'origine etait corrompue — total 0x2C au lieu de 0x00).
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

/* Registres GT911 */
#define GT_REG_CFG_VER   0x8047
#define GT_REG_PRODUCT   0x8140
#define GT_REG_STATUS    0x814E
#define GT_REG_POINTS    0x8150
#define GT_REG_CFG_FRESH 0x8100

static i2c_master_bus_handle_t s_bus = NULL;
static uint8_t s_addr = 0;

/* Config 480x800 hote (185 octets @0x8047, [184] = checksum recalcule). */
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
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* [184] = checksum, recalcule */
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

/* Bus I2C PARTAGE (i2c_bus.c) — RTC/gauge l'utilisent aussi. Ne jamais le
 * detruire ici (i2c_del_master_bus) sous peine de casser les autres
 * drivers ; en cas d'echec touch, on abandonne juste localement (s_addr
 * reste 0), le bus partage survit. */
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

/* Lecture registre 16-bit (>= 2 octets : les lectures d'1 octet NACKent,
 * anomalie driver I2C IDF 5.4 validee sur ce hardware). */
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

/* Dance POR complete (FreeInk xteink-x4pro-support.md) : le self-load /
 * l'upload config n'est garanti que si le POR se fait RST asserte.
 * Pièges valides : delais exacts en us (pdMS_TO_TICKS(2)/(8) = 0 tick),
 * INT low AVANT rail-off pour adresse 0x5D et mode config. */
static void gt911_por_dance(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << TOUCH_RST_PIN) | (1ULL << TOUCH_INT_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(TOUCH_RST_PIN, 0);   /* RST low, maintenu pendant tout le POR */
    gpio_set_level(TOUCH_INT_PIN, 0);   /* INT low -> adresse 0x5D */
    gpio_set_level(RAIL_TOUCH_PIN, 1);  /* rail off */
    esp_rom_delay_us(50000);
    gpio_set_level(RAIL_TOUCH_PIN, 0);  /* rail on : POR avec RST asserte */
    esp_rom_delay_us(50000);

    esp_rom_delay_us(2000);             /* RST low 2 ms (doc) */
    gpio_set_level(TOUCH_RST_PIN, 1);   /* release */
    esp_rom_delay_us(8000);             /* INT low 8 ms apres release (doc) */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(TOUCH_INT_PIN);
    esp_rom_delay_us(60000);            /* attente self-load config */
}

/* Upload config hote en mode CONFIG UPDATE (INT maintenu LOW pendant
 * l'ecriture). Necessaire sur ce batch : OTP config vide (0x8047 == 0x00). */
static bool gt911_upload_config(void)
{
    /* Entree update par POR : INT low + RST low AVANT le power-on de la rail
       (le plus fort des deux, mesure 23:37). */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(TOUCH_INT_PIN, 0);
    gpio_set_level(TOUCH_RST_PIN, 0);
    gpio_set_level(RAIL_TOUCH_PIN, 1);
    esp_rom_delay_us(50000);
    gpio_set_level(RAIL_TOUCH_PIN, 0);  /* rail on, POR sous reset+INT low */
    esp_rom_delay_us(50000);
    gpio_set_level(TOUCH_RST_PIN, 1);   /* release RST, INT low a la montee */
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

    /* Relache INT : la config RAM reste active sans reset (test decisif C2). */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(TOUCH_INT_PIN);
    esp_rom_delay_us(100000);

    /* Verif : la config doit relire 0x81. */
    uint8_t ver[2] = {0};
    if (reg_read(GT_REG_CFG_VER, ver, 2) != ESP_OK) return false;
    return ver[0] == 0x81;
}

/* Dance RST/INT SEULE, sans cycle d'alimentation du rail touch (la rail est
 * deja alimentee une fois par rails_init()). Timings identiques (2/8/60 ms)
 * a gt911_por_dance(), mais SANS le power-cycle GPIO2 off/on qu'elle fait en
 * plus. Reference : freeink-sdk (docs/xteink-x4pro-support.md, RE Ghidra du
 * firmware OEM + confirme sur hardware) — sur ce controleur le self-load de
 * la config interne marche avec CETTE dance minimale ; un "0x8047 == 0x00"
 * y est diagnostique comme un reset trop court, pas un OTP vide. A tester en
 * premier avant de retomber sur notre dance + upload hote (deja valides). */
static void gt911_self_load_dance(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << TOUCH_RST_PIN) | (1ULL << TOUCH_INT_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(TOUCH_RST_PIN, 0);   /* RST low */
    gpio_set_level(TOUCH_INT_PIN, 0);   /* INT low -> adresse 0x5D */
    esp_rom_delay_us(2000);             /* RST low 2 ms (doc) */
    gpio_set_level(TOUCH_RST_PIN, 1);   /* release */
    esp_rom_delay_us(8000);             /* INT low 8 ms apres release (doc) */
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(TOUCH_INT_PIN);
    esp_rom_delay_us(60000);            /* attente self-load config */
}

/* Probe 0x5D / 0x14 (GT911 invisible a un scan classique : registres
 * 16-bit). Identification par lecture product ID (>= 2 octets). */
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

    /* Tentative 1 : self-load "a la freeink" (rail deja alimentee, dance
     * RST/INT sans power-cycle). Si 0x8047 revient non-nul, on garde la
     * config auto-chargee (et potentiellement la vraie touche Home GT911,
     * cf. touch_read()) sans jamais toucher a l'upload hote. */
    gt911_self_load_dance();
    if (i2c_bus_open() != ESP_OK) {
        ESP_LOGE(TAG, "i2c_bus_open ECHEC (bus 39/38)");
        s_bus = NULL;
        return false;
    }
    if (gt911_probe_address()) {
        ESP_LOGI(TAG, "probe OK (self-load), addr 0x%02X", s_addr);
        uint8_t ver[2] = {0};
        if (reg_read(GT_REG_CFG_VER, ver, 2) == ESP_OK && ver[0] != 0x00) {
            ESP_LOGI(TAG, "self-load OK, cfg version 0x%02X (pas d'upload)",
                     ver[0]);
            return true;
        }
        ESP_LOGW(TAG, "self-load ECHEC (cfg version 0x%02X) -> fallback dance+upload",
                 ver[0]);
    } else {
        ESP_LOGW(TAG, "probe KO apres self-load dance -> fallback dance+upload");
    }

    /* Tentative 2 (fallback deja valide sur cette unite) : dance complete
     * avec power-cycle du rail + upload de config hote si necessaire. */
    s_addr = 0;
    gt911_por_dance();
    if (!gt911_probe_address()) {
        ESP_LOGE(TAG, "probe: aucune adresse GT911 ne repond (0x14/0x5D)");
        /* NE PAS detruire s_bus : bus partage (i2c_bus.c), RTC/gauge en
         * dependent aussi. On abandonne juste localement. */
        return false;
    }
    ESP_LOGI(TAG, "probe OK (fallback), addr 0x%02X", s_addr);

    uint8_t ver[2] = {0};
    if (reg_read(GT_REG_CFG_VER, ver, 2) != ESP_OK || ver[0] == 0x00) {
        ESP_LOGI(TAG, "cfg version 0x%02X -> upload config hote",
                 ver[0]);
        if (!gt911_upload_config()) {
            ESP_LOGE(TAG, "upload config ECHEC");
            return false;   /* le chip ne scanne pas sans config */
        }
        ESP_LOGI(TAG, "config hote uploadee (480x800)");
    }
    return true;
}

touch_point_t touch_read(void)
{
    touch_point_t pt = { .pressed = false, .x = -1, .y = -1 };
    if (!s_bus || !s_addr) return pt;

    uint8_t st[2] = {0};
    if (reg_read(GT_REG_STATUS, st, 2) != ESP_OK) return pt;
    if (!(st[0] & 0x80)) return pt;   /* pas de donnees */

    uint8_t pts[8] = {0};
    if (reg_read(GT_REG_POINTS, pts, 8) == ESP_OK) {
        int16_t raw_x = (int16_t)(pts[0] | (pts[1] << 8));
        int16_t raw_y = (int16_t)(pts[2] | (pts[3] << 8));
        /* Home = zone logicielle OU la vraie touche capacitive GT911 (bit
         * 0x10 de 0x814E — freeink-sdk, RE Ghidra du firmware OEM : "the OEM
         * keys off exactly 0x814E & 0x10"). Le bit ne s'est jamais leve avec
         * notre config uploadee ; si le self-load (gt911_self_load_dance)
         * reussit un jour, il pourrait fonctionner directement. Les deux
         * coexistent, sans risque.
         * Zone RECALIBREE 2026-09-15 (log reel, factory build 12:25) :
         * appuis repetes sur le pad Home physique -> raw (x=2..8, y=693..696),
         * tres stable. L'ancienne mesure (36,479, doc 01:34) ne correspond
         * plus a rien avec la config actuellement uploadee -> remplacee. */
        pt.home = (raw_x < 70 && raw_y >= 660 && raw_y <= 720) ||
                  (st[0] & 0x10) != 0;
        /* Bruts deja PORTRAIT (test 4 coins 00:55 : HG=(48,58) HD=(475,80)
         * BD=(476,660) BG=(52,661) -> raw_x = user x, raw_y = user y).
         * L'UI est en coords portrait 480x800 depuis le fix gfx (transpose). */
        pt.x = raw_x;
        pt.y = raw_y;
        pt.pressed = true;
    }

    /* Libere le buffer pour la suite. */
    uint8_t clr[3] = {0x81, 0x4E, 0x00};
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_dev_open(s_addr, &dev) == ESP_OK) {
        i2c_master_transmit(dev, clr, sizeof(clr), pdMS_TO_TICKS(100));
        i2c_master_bus_rm_device(dev);
    }
    return pt;
}
