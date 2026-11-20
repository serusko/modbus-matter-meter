#pragma once
#include <stdbool.h>
#include "modbus_rtu.h"

#ifdef __cplusplus
extern "C" {
#endif

// F&F LE-03MW, register map from the user manual (section 9, pages 13-14).
typedef struct {
    float v[3];          // V, phase-neutral
    float i[3];          // A
    float p[3];          // kW (verified on the meter)
    float p_total;       // kW
    float freq;          // Hz
    float e_import;      // kWh, 0x108 "consumed"
    float e_import_ph[3];
    float e_export;      // kWh, 0x110 "exported"
} le03mw_data_t;

typedef struct {
    uart_port_t port;
    uint8_t addr;
    uint8_t fc;          // 0x03 or 0x04, found by le03mw_probe
    bool swap_words;     // FLOAT word order CDAB instead of ABCD
} le03mw_t;

// Try 9600..1200 baud x 8E2/8E1/8N1 x FC 03/04 x word order until the frequency register
// reads 45-65 Hz. Updates cfg (baud/parity/stop) and m->fc/swap_words. Logs every attempt.
esp_err_t le03mw_probe(le03mw_t *m, mb_cfg_t *cfg);

esp_err_t le03mw_read(const le03mw_t *m, le03mw_data_t *d);


#ifdef __cplusplus
}
#endif
