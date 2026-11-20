#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uart_port_t port;
    int tx_pin;
    int rx_pin;
    int de_pin;              // RS-485 DE/RE (driven as RTS), or UART_PIN_NO_CHANGE
    int baud;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
} mb_cfg_t;

// (Re)initialise the UART in RS-485 half-duplex mode. Safe to call repeatedly.
esp_err_t mb_init(const mb_cfg_t *cfg);

// Read `count` 16-bit registers with function code 0x03 or 0x04.
// Returns ESP_ERR_TIMEOUT (no/garbled answer) or ESP_ERR_INVALID_RESPONSE
// (Modbus exception, code in *exc if non-NULL).
esp_err_t mb_read_regs(uart_port_t port, uint8_t slave, uint8_t fc, uint16_t addr,
                       uint16_t count, uint16_t *out, uint8_t *exc, int timeout_ms);

#ifdef __cplusplus
}
#endif
