/**
 * @file eink.c
 * @brief MySafeFob Factory — driver E-Ink UC8279 (X4 Pro).
 *
 * Port direct des sequences validees du probe x4pro-probe (eink_test.c,
 * commandes einkucinit/einkuc2 mode 0). Ne pas "simplifier" les delais ni
 * l'ordre des registres — chaque ecart a deja ete mesure comme cassant.
 */
#include "eink.h"
#include "hw_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "eink";

/* Commandes UC8279 (batch UltraChip) */
#define UC_CMD_PANEL_SETTING 0x00   /* PSR  */
#define UC_CMD_POWER_OFF     0x02   /* POF  */
#define UC_CMD_PFS           0x03   /* PFS  */
#define UC_CMD_POWER_ON      0x04   /* PON  */
#define UC_CMD_DTM1          0x10   /* OLD plane */
#define UC_CMD_DRF           0x12   /* display refresh */
#define UC_CMD_DTM2          0x13   /* NEW plane */
#define UC_CMD_PLL           0x30
#define UC_CMD_CDI           0x50   /* 1 octet SEUL */
#define UC_CMD_TRES          0x61
#define UC_CMD_GSST          0x65
#define UC_CMD_CCSET         0xE0
#define UC_CMD_GATE_SCAN     0xE1
#define UC_CMD_TSSET         0xE5

#define UC_TRES_H            600     /* gates adressees (480 visibles) */
#define UC_GATE_OFFSET       120     /* gates 0..119 = zone non visible */

/* ---- Refresh rapide DU (waveform OTP partielle du stock FW, RE de
 * Uc8279X4Driver::startBwRefresh fast=true — reference extraite du
 * freeink-sdk, docs/xteink-x3-uc8279-support.md). POINTS CLES mesures
 * par le firmware stock :
 *   - DU = waveform OTP (REG=0), PAS de LUT hote : la selection se fait
 *     par TSSET 0x5A + CDI 0xD7. Le probe a fige le UC8279 avec REG=1.
 *   - PTIN exige une fenetre PTL prealable, sinon le DU scanne sans
 *     developper (unite terrain : DRF 443 ms, aucune image).
 *   - OLD plane = frame precedente (diff differentielle) ; apres chaque
 *     refresh, DTM1 est resynchronise avec la frame affichee.
 *   - Un full GC periodique reste obligatoire (budget de ghosts). */
#define UC_CMD_PTL           0x90    /* PTL  : fenetre partielle */
#define UC_CMD_PTL_IN        0x91    /* PTIN */
#define UC_CMD_PTL_OUT       0x92    /* PTOUT */
#define UC_CDI_FAST          0xD7
#define UC_TSSET_FAST        0x5A
#define EINK_FAST_BUDGET     30      /* fast DU entre deux full GC
                                      * (session 2026-09-14 : 10 laissait des
                                      * fantomes noir->gris->blanc sur la barre
                                      * de selection — grosse zone noire mobile
                                      * = cas pire pour le DU). Remonte a 30
                                      * (session 2026-09-15) : le vrai coupable
                                      * du ghosting etait le bug s_prev jamais
                                      * mis a jour apres un fast DU (corrige) —
                                      * le diff etait calcule contre une frame
                                      * perimee, pas la limite intrinseque du
                                      * DU. A revalider sur hardware ; remonter
                                      * encore ou repasser plus bas selon le
                                      * ghosting observe en session longue. */

static spi_device_handle_t s_spi = NULL;
static bool s_initialized = false;

static uint8_t *s_prev = NULL;        /* PSRAM : derniere frame affichee */
static bool s_prev_valid = false;
static uint8_t s_fast_count = 0;
static bool s_screen_on = false;

/* Definis plus bas dans le fichier (ordre historique du driver). */
static void write_cmd(uint8_t cmd);
static bool wait_idle(const char *what, uint32_t timeout_ms);

static void cs_low(void)  { gpio_set_level(EINK_CS, 0); }
static void cs_high(void) { gpio_set_level(EINK_CS, 1); }

/* PON idempotent (le stock FW evite un second PON ecran allume). */
static esp_err_t power_on(void)
{
    if (s_screen_on) return ESP_OK;
    write_cmd(UC_CMD_POWER_ON);
    if (wait_idle("PON", 2000)) return ESP_ERR_TIMEOUT;
    s_screen_on = true;
    return ESP_OK;
}

/* Buffer "frame precedente" en PSRAM (48 Ko) — base du diff DU. */
static bool prev_alloc(void)
{
    if (s_prev) return true;
    s_prev = heap_caps_malloc(SCREEN_FB_SIZE,
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_prev) {
        ESP_LOGW(TAG, "PSRAM indisponible pour le diff DU — full GC uniquement");
    }
    return s_prev != NULL;
}

static void record_prev(const uint8_t *fb)
{
    if (!prev_alloc()) {
        s_prev_valid = false;
        return;
    }
    memcpy(s_prev, fb, SCREEN_FB_SIZE);
    s_prev_valid = true;
    s_fast_count = 0;   /* nouveau cycle : budget de fasts reparti a 0 */
}

/* Attend que BUSY_N repasse HIGH (idle). true = timeout (etat anormal). */
static bool wait_idle(const char *what, uint32_t timeout_ms)
{
    vTaskDelay(pdMS_TO_TICKS(10));   /* laisse le controleur prendre BUSY */
    uint64_t start = esp_timer_get_time();
    while (gpio_get_level(EINK_BUSY) == 0) {
        if ((esp_timer_get_time() - start) / 1000 > timeout_ms) {
            ESP_LOGW(TAG, "UC BUSY timeout (%s) apres %lu ms", what,
                     (unsigned long)timeout_ms);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));  /* >= 1 tick : nourrit le watchdog */
    }
    return false;
}

static void spi_write(const uint8_t *data, size_t len)
{
    /* DMA S3 limite a 32768 o/transaction -> chunker a 16 Ko. */
    while (len > 0) {
        size_t chunk = len > 16384 ? 16384 : len;
        spi_transaction_t x = {0};
        x.length = chunk * 8;
        x.tx_buffer = data;
        spi_device_transmit(s_spi, &x);
        data += chunk;
        len -= chunk;
    }
}

static void write_cmd(uint8_t cmd)
{
    gpio_set_level(EINK_DC, 0);
    cs_low();
    spi_write(&cmd, 1);
    cs_high();
}

static void write_data(const uint8_t *data, size_t len)
{
    gpio_set_level(EINK_DC, 1);
    cs_low();
    spi_write(data, len);
    cs_high();
}

static void hw_reset(void)
{
    gpio_set_level(EINK_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EINK_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EINK_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static esp_err_t spi_init(void)
{
    if (s_spi) return ESP_OK;

    spi_bus_config_t bus = {
        .sclk_io_num = EINK_SCLK,
        .mosi_io_num = EINK_MOSI,
        .miso_io_num = GPIO_NUM_NC,     /* write-only */
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = SCREEN_FB_SIZE + 64,
    };
    esp_err_t err = spi_bus_initialize(EINK_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    spi_device_interface_config_t dev = {
        .clock_speed_hz = EINK_SPI_HZ,
        .mode = 0,
        .spics_io_num = GPIO_NUM_NC,    /* CS manuel */
        .queue_size = 1,
    };
    return spi_bus_add_device(EINK_HOST, &dev, &s_spi);
}

/* Registres d'init, ordre exact Uc8279X4Driver (FreeInk) + RE firmware stock.
 * pas de BTST/PWS : PWR/VDCS/BTST restent panel-programmes (OTP/MTP). */
static void write_init_registers(uint8_t psr0)
{
    write_cmd(UC_CMD_PANEL_SETTING);
    { const uint8_t v[2] = {psr0, 0x4D}; write_data(v, 2); }

    write_cmd(UC_CMD_TRES);
    { const uint8_t v[4] = {0x03, 0x20, 0x02, 0x58}; write_data(v, 4); }  /* 800 x 600 */

    write_cmd(UC_CMD_GSST);
    { const uint8_t v[4] = {0x00, 0x00, 0x00, 0x00}; write_data(v, 4); }

    write_cmd(UC_CMD_PFS);
    { uint8_t v = 0x20; write_data(&v, 1); }

    write_cmd(UC_CMD_PLL);   /* X4 Pro uniquement (stock X4C : no-op) */
    { uint8_t v = 0x0E; write_data(&v, 1); }

    write_cmd(UC_CMD_GATE_SCAN);
    { uint8_t v = 0x02; write_data(&v, 1); }
}

/* Stream d'un plan : pad blanc gates 0..119, 480 lignes fb ordre direct,
 * pad blanc jusqu'a 600 gates. Orientation definitive (mode 0, mesure 12:18). */
static void stream_plane(uint8_t ram_cmd, const uint8_t *fb)
{
    static uint8_t row[EINK_WB];
    write_cmd(ram_cmd);
    memset(row, 0xFF, sizeof(row));
    for (int y = 0; y < UC_GATE_OFFSET; y++) {
        write_data(row, EINK_WB);
    }
    for (int y = 0; y < EINK_H; y++) {
        write_data(fb + (uint32_t)y * EINK_WB, EINK_WB);
    }
    for (int y = UC_GATE_OFFSET + EINK_H; y < UC_TRES_H; y++) {
        write_data(row, EINK_WB);
    }
}

esp_err_t eink_init(void)
{
    if (s_initialized) return ESP_OK;

    esp_err_t err = spi_init();
    if (err != ESP_OK) return err;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << EINK_CS) | (1ULL << EINK_DC) | (1ULL << EINK_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_config_t busy_io = {
        .pin_bit_mask = 1ULL << EINK_BUSY,
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&busy_io);
    cs_high();

    hw_reset();
    write_init_registers(0x37);   /* REG=1 (LUT hote) a l'init */

    s_initialized = true;
    ESP_LOGI(TAG, "UC8279 init OK (800x480 visible, gates 120-599)");
    return ESP_OK;
}

esp_err_t eink_display_fb(const uint8_t *fb)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* PAS d'ecriture PSR ici (l'etat hote est garanti au moment du write) :
       un write PSR avant le DTM a deja fige le controleur (mesure 11:21). */

    stream_plane(UC_CMD_DTM2, fb);            /* NEW = frame */
    write_cmd(UC_CMD_DTM1);                   /* OLD = blanc */
    {
        static uint8_t row[EINK_WB];
        memset(row, 0xFF, sizeof(row));
        for (int y = 0; y < UC_TRES_H; y++) write_data(row, EINK_WB);
    }

    /* Refresh setup — ordre du stock FW : CDI (1 o), CCSET, TSSET, PON,
       PSR ENTRE PON et DRF (PON recharge le MTP), DRF. */
    write_cmd(UC_CMD_CDI);
    { uint8_t v = 0x97; write_data(&v, 1); }
    write_cmd(UC_CMD_CCSET);
    { uint8_t v = 0x02; write_data(&v, 1); }
    write_cmd(UC_CMD_TSSET);
    { uint8_t v = 0x1E; write_data(&v, 1); }

    if (power_on() != ESP_OK) return ESP_ERR_TIMEOUT;

    /* Re-ecriture COMPLETE des registres entre PON et DRF avec PSR 0x17
       (REG=0, scan MTP) — exigence UC8279 rev v0.2 (mesure 11:58). */
    write_init_registers(0x17);

    write_cmd(UC_CMD_DRF);
    {   /* confirme le depart (BUSY drop) puis attends la fin (BUSY HIGH) */
        uint64_t t0 = esp_timer_get_time();
        while (gpio_get_level(EINK_BUSY) == 1 &&
               (esp_timer_get_time() - t0) / 1000 < 50) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (wait_idle("DRF", 20000)) {
        ESP_LOGE(TAG, "DRF timeout — refresh non termine");
        write_cmd(UC_CMD_POWER_OFF);
        wait_idle("POF-recovery", 3000);
        s_screen_on = false;
        s_prev_valid = false;
        return ESP_ERR_TIMEOUT;
    }
    record_prev(fb);
    return ESP_OK;
}

esp_err_t eink_display_fb_fast(const uint8_t *fb)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* Un fast DU n'est possible que sur un diff differentiel sain :
       frame precedente connue ET budget de ghosts non epuise. Sinon full. */
    if (!s_prev_valid || !s_prev || s_fast_count >= EINK_FAST_BUDGET) {
        return eink_display_fb(fb);
    }

    /* NEW = frame, OLD = frame precedente (diff, pas de clear flash). */
    stream_plane(UC_CMD_DTM2, fb);
    stream_plane(UC_CMD_DTM1, s_prev);

    /* Declenchement DU du stock FW (RE Uc8279X4Driver::startBwRefresh
       fast=true) : CDI 0xD7, CCSET, TSSET 0x5A, PFS, gate scan, PON,
       PTIN + PTL fenetre PLEINE (obligatoire — sans PTL le DU scanne
       sans developper), PSR 0x17 (OTP), DRF. */
    write_cmd(UC_CMD_CDI);
    { uint8_t v = UC_CDI_FAST; write_data(&v, 1); }
    write_cmd(UC_CMD_CCSET);
    { uint8_t v = 0x02; write_data(&v, 1); }
    write_cmd(UC_CMD_TSSET);
    { uint8_t v = UC_TSSET_FAST; write_data(&v, 1); }
    write_cmd(UC_CMD_PFS);
    { uint8_t v = 0x20; write_data(&v, 1); }
    write_cmd(UC_CMD_GATE_SCAN);
    { uint8_t v = 0x02; write_data(&v, 1); }

    if (power_on() != ESP_OK) {
        s_prev_valid = false;
        return ESP_ERR_TIMEOUT;
    }

    /* PTL : fenetre pleine, coords gates avec l'offset +120 visible,
       PT_SCAN=1. Byte-exact Uc8279X4Driver. */
    write_cmd(UC_CMD_PTL_IN);
    write_cmd(UC_CMD_PTL);
    {
        const uint8_t v[9] = {
            0x00, 0x00,             /* x start */
            0x03, 0x1F,             /* x end 799|0x07 */
            (uint8_t)(UC_GATE_OFFSET >> 8), (uint8_t)(UC_GATE_OFFSET & 0xFF),
            (uint8_t)((UC_GATE_OFFSET + EINK_H - 1) >> 8),
            (uint8_t)((UC_GATE_OFFSET + EINK_H - 1) & 0xFF),
            0x01,                   /* PT_SCAN */
        };
        write_data(v, sizeof(v));
    }

    write_cmd(UC_CMD_PANEL_SETTING);
    { const uint8_t v[2] = {0x17, 0x4D}; write_data(v, 2); }

    write_cmd(UC_CMD_DRF);
    {
        uint64_t t0 = esp_timer_get_time();
        while (gpio_get_level(EINK_BUSY) == 1 &&
               (esp_timer_get_time() - t0) / 1000 < 50) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (wait_idle("DRF-fast", 20000)) {
        ESP_LOGE(TAG, "DRF fast timeout");
        write_cmd(UC_CMD_PTL_OUT);
        write_cmd(UC_CMD_POWER_OFF);
        wait_idle("POF-recovery", 3000);
        s_screen_on = false;
        s_prev_valid = false;
        return ESP_ERR_TIMEOUT;
    }
    write_cmd(UC_CMD_PTL_OUT);

    /* Resynchronise DTM1 avec la frame affichee (le prochain fast diffe
       contre elle) — a faire ecran encore alimente, comme le stock. */
    stream_plane(UC_CMD_DTM1, fb);

    /* BUGFIX : s_prev (cache logiciel de la derniere frame reellement
     * affichee) DOIT suivre chaque fast DU reussi, sinon le prochain DU
     * diffe contre une frame perimee (celle du dernier full refresh) au
     * lieu de l'etat ecran courant -> toggles incorrects qui s'accumulent
     * sur le budget (ghosting / inversions locales), corriges seulement
     * au prochain full refresh force. */
    memcpy(s_prev, fb, SCREEN_FB_SIZE);

    s_fast_count++;
    return ESP_OK;
}

esp_err_t eink_power_off(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    write_cmd(UC_CMD_POWER_OFF);
    wait_idle("POF", 2000);
    s_screen_on = false;
    /* L'image bistable persiste : s_prev reste physiquement valide. */
    return ESP_OK;
}
