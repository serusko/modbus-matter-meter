#include "le03mw.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "le03mw";

#define REG_LIVE   0x000E  // V1..V3, Hz, I1..I3, Ptot, P1..P3  -> 22 registers
#define N_LIVE     22
#define REG_ENERGY 0x0108  // Eimp, Eimp1..Eimp3, Eexp            -> 10 registers
#define N_ENERGY   10
#define TIMEOUT_MS 300

static float to_float(const uint16_t *r, bool swap)
{
    uint32_t u = swap ? ((uint32_t)r[1] << 16) | r[0] : ((uint32_t)r[0] << 16) | r[1];
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

esp_err_t le03mw_probe(le03mw_t *m, mb_cfg_t *cfg)
{
    // Manual p.9: parity EVEN, 1200-9600 bps. 8E1 is what the meter actually uses.
    static const int bauds[] = {9600, 4800, 2400, 1200};
    static const struct { uart_parity_t parity; uart_stop_bits_t stop; const char *name; } frames[] = {
        {UART_PARITY_EVEN, UART_STOP_BITS_1, "8E1"},  // verified on the meter
        {UART_PARITY_EVEN, UART_STOP_BITS_2, "8E2"},
        {UART_PARITY_DISABLE, UART_STOP_BITS_1, "8N1"},
    };
    static const uint8_t fcs[] = {0x03, 0x04};

    for (int b = 0; b < 4; b++)
    for (int p = 0; p < 3; p++) {
        const char *pname = frames[p].name;
        cfg->baud = bauds[b];
        cfg->parity = frames[p].parity;
        cfg->stop_bits = frames[p].stop;
        if (mb_init(cfg) != ESP_OK) {
            ESP_LOGE(TAG, "UART init failed for %d %s", cfg->baud, pname);
            continue;
        }
        for (int f = 0; f < 2; f++) {
            uint16_t r[8];
            uint8_t exc = 0;
            ESP_LOGI(TAG, "try %d %s FC%02X", cfg->baud, pname, fcs[f]);
            esp_err_t err = mb_read_regs(cfg->port, m->addr, fcs[f], REG_LIVE, 8, r, &exc, 500);
            if (err == ESP_ERR_INVALID_RESPONSE) {
                ESP_LOGW(TAG, "%s FC%02X: exception %d (link OK, wrong FC/register)", pname, fcs[f], exc);
                continue;
            }
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "%s FC%02X: no answer", pname, fcs[f]);
                continue;
            }
            for (int swap = 0; swap < 2; swap++) {
                float hz = to_float(&r[6], swap);
                ESP_LOGI(TAG, "%s FC%02X %s: V1=%.1f Hz=%.2f", pname, fcs[f],
                         swap ? "CDAB" : "ABCD", to_float(&r[0], swap), hz);
                if (hz > 45.0f && hz < 65.0f) {
                    m->fc = fcs[f];
                    m->swap_words = swap;
                    ESP_LOGI(TAG, "=> using %d baud %s, FC%02X, word order %s",
                             cfg->baud, pname, m->fc, swap ? "CDAB" : "ABCD");
                    return ESP_OK;
                }
            }
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t le03mw_read(const le03mw_t *m, le03mw_data_t *d)
{
    uint16_t r[N_LIVE];
    bool s = m->swap_words;
    esp_err_t err;

    if ((err = mb_read_regs(m->port, m->addr, m->fc, REG_LIVE, N_LIVE, r, NULL, TIMEOUT_MS)) != ESP_OK) return err;
    for (int k = 0; k < 3; k++) {
        d->v[k] = to_float(&r[0 + 2 * k], s);
        d->i[k] = to_float(&r[8 + 2 * k], s);
        d->p[k] = to_float(&r[16 + 2 * k], s);
    }
    d->freq = to_float(&r[6], s);
    d->p_total = to_float(&r[14], s);

    if ((err = mb_read_regs(m->port, m->addr, m->fc, REG_ENERGY, N_ENERGY, r, NULL, TIMEOUT_MS)) != ESP_OK) return err;
    d->e_import = to_float(&r[0], s);
    for (int k = 0; k < 3; k++) d->e_import_ph[k] = to_float(&r[2 + 2 * k], s);
    d->e_export = to_float(&r[8], s);

    // CRC-valid but non-finite floats would otherwise flow straight into Matter.
    // le03mw_data_t holds only floats, so it can be scanned as an array.
    const float *f = (const float *)d;
    for (size_t k = 0; k < sizeof(*d) / sizeof(float); k++) {
        if (!isfinite(f[k])) return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
