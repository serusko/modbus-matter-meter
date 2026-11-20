#include "modbus_rtu.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "modbus";

static uint16_t crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

static bool crc_ok(const uint8_t *frame, size_t len)
{
    uint16_t c = crc16(frame, len - 2);
    return frame[len - 2] == (c & 0xFF) && frame[len - 1] == (c >> 8);
}

esp_err_t mb_init(const mb_cfg_t *c)
{
    if (uart_is_driver_installed(c->port)) {
        uart_driver_delete(c->port);
    }
    const uart_config_t uc = {
        .baud_rate = c->baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = c->parity,
        .stop_bits = c->stop_bits ? c->stop_bits : UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(c->port, 512, 0, 0, NULL, 0);
    if (err == ESP_OK) err = uart_param_config(c->port, &uc);
    if (err == ESP_OK) err = uart_set_pin(c->port, c->tx_pin, c->rx_pin, c->de_pin, UART_PIN_NO_CHANGE);
    if (err == ESP_OK) err = uart_set_mode(c->port, UART_MODE_RS485_HALF_DUPLEX);
    return err;
}

esp_err_t mb_read_regs(uart_port_t port, uint8_t slave, uint8_t fc, uint16_t addr,
                       uint16_t count, uint16_t *out, uint8_t *exc, int timeout_ms)
{
    uint8_t req[8] = {slave, fc, addr >> 8, addr & 0xFF, count >> 8, count & 0xFF};
    uint16_t c = crc16(req, 6);
    req[6] = c & 0xFF;
    req[7] = c >> 8;

    uart_flush_input(port);
    uart_write_bytes(port, req, sizeof(req));
    uart_wait_tx_done(port, pdMS_TO_TICKS(100));

    // Some auto-direction RS-485 modules echo our request back, so we scan the
    // buffer for a valid reply frame instead of assuming it starts at byte 0.
    const size_t expected = 5 + 2 * count;
    uint8_t buf[sizeof(req) + 5 + 2 * 125];
    size_t n = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline && n < sizeof(buf)) {
        int r = uart_read_bytes(port, buf + n, sizeof(buf) - n, pdMS_TO_TICKS(20));
        if (r > 0) n += r;
        for (size_t i = 0; i + 5 <= n; i++) {
            if (buf[i] != slave) continue;
            if (buf[i + 1] == (fc | 0x80) && crc_ok(buf + i, 5)) {
                if (exc) *exc = buf[i + 2];
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (buf[i + 1] == fc && buf[i + 2] == 2 * count && i + expected <= n
                && crc_ok(buf + i, expected)) {
                for (uint16_t k = 0; k < count; k++) {
                    out[k] = (buf[i + 3 + 2 * k] << 8) | buf[i + 4 + 2 * k];
                }
                return ESP_OK;
            }
        }
    }
    // Diagnostics: 0 bytes = silence, 8 bytes equal to the request = module echo only,
    // anything else = garbled reply (baud/parity) or a different slave address.
    ESP_LOGW(TAG, "slave %d FC%02X @0x%04X: timeout, %u byte(s) received", slave, fc, addr, (unsigned)n);
    if (n) ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, n, ESP_LOG_WARN);
    return ESP_ERR_TIMEOUT;
}
