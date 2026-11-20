// F&F LE-03MW over RS-485 -> Matter-over-Thread: 4 "Electrical Sensor" endpoints (total, L1, L2, L3).
#include <cmath>

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_matter.h>
#include <app_openthread_config.h>
#include <common_macros.h>
#include <data_model_provider/clusters/electrical_energy_measurement/integration.h>
#include <system/SystemClock.h>

#include "epm_delegate.h"
#include "le03mw.h"

// Pins match the rs485-meter carrier PCB (see ../../README.md).
#define RS485_UART   UART_NUM_1
#define RS485_TX     19
#define RS485_RX     23
#define RS485_DE     4
#define METER_ADDR   1
#define POLL_MS      2000

// Report a value to Matter only when it moved at least this much (keeps Thread traffic low).
#define DB_VOLT      0.5f    // V
#define DB_AMP       0.05f   // A
#define DB_KW        0.010f  // kW (10 W)

// Null the Matter values only after this many failed reads in a row (single timeouts happen).
#define FAIL_LIMIT   3

// Cumulative energy sanity: never decreases, never grows faster than the meter's maximum
// (3 x 230 V x 80 A ~ 55 kW) allows since the last report.
#define MAX_KW       60.0f
#define STEP_SLACK   0.05f   // kWh, register resolution + float rounding

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace chip::app::Clusters;
using chip::app::DataModel::MakeNullable;
using chip::app::DataModel::Nullable;
namespace EEM = chip::app::Clusters::ElectricalEnergyMeasurement;

// Endpoint index 0 = whole meter (totals), 1..3 = phases L1..L3.
#define N_EP 4
static uint16_t s_ep[N_EP];
static MeterEpmDelegate s_epm[N_EP];
static MeterTopologyDelegate s_topology;

// Accuracy is a mandatory EEM attribute; LE-03MW is class 1 (1 %).
static bool eem_set_accuracy(uint16_t ep)
{
    static const EEM::Structs::MeasurementAccuracyRangeStruct::Type ranges[] = {{
        .rangeMin   = 0,
        .rangeMax   = 1'000'000'000'000'000,  // 1 TWh in mWh
        .percentMax = chip::MakeOptional(static_cast<chip::Percent100ths>(100)),
    }};
    EEM::Structs::MeasurementAccuracyStruct::Type acc = {
        .measurementType  = EEM::MeasurementTypeEnum::kElectricalEnergy,
        .measured         = true,
        .minMeasuredValue = 0,
        .maxMeasuredValue = 1'000'000'000'000'000,
        .accuracyRanges   = chip::app::DataModel::List<const EEM::Structs::MeasurementAccuracyRangeStruct::Type>(ranges),
    };
    return EEM::SetMeasurementAccuracy(ep, acc) == CHIP_NO_ERROR;
}

static Nullable<int64_t> milli(float v) { return MakeNullable(static_cast<int64_t>(llroundf(v * 1000.0f))); }

// True (and remembers v) when v differs from the last reported value by >= db, or nothing was reported yet.
static bool moved(float &last, float v, float db)
{
    if (!std::isnan(last) && fabsf(v - last) < db) return false;
    last = v;
    return true;
}

// A cumulative energy register as last reported to Matter. Until the first report, two
// consecutive reads must agree, so a bogus first frame (e.g. 0 during meter start-up) is
// never published: HA would treat a drop as a counter reset and double-count the total.
struct Counter {
    float reported = -1;   // kWh, -1 = nothing reported yet
    float candidate = -1;  // first value seen, waiting for confirmation
    int64_t t_us = 0;      // time of the last report
};

// Returns true when v should be reported now (plausible and different from the last report).
static bool counter_update(Counter &c, float v, const char *name)
{
    int64_t now = esp_timer_get_time();
    if (v < 0) {
        ESP_LOGW(TAG, "%s: negative energy %.3f kWh ignored", name, v);
        return false;
    }
    if (c.reported < 0) {
        bool confirmed = c.candidate >= 0 && fabsf(v - c.candidate) <= STEP_SLACK;
        c.candidate = v;
        if (!confirmed) return false;
    } else {
        float max_step = STEP_SLACK + MAX_KW * (now - c.t_us) / 3.6e9f;  // kW * h
        if (v < c.reported || v - c.reported > max_step) {
            ESP_LOGW(TAG, "%s: implausible energy %.3f kWh (last %.3f) ignored", name, v, c.reported);
            return false;
        }
        if (v == c.reported) return false;
    }
    c.reported = v;
    c.t_us = now;
    return true;
}

// Last reported value of a counter as a Matter energy measurement (null until the first report).
static Nullable<EEM::Structs::EnergyMeasurementStruct::Type> energy_of(const Counter &c)
{
    if (c.reported < 0) return {};
    EEM::Structs::EnergyMeasurementStruct::Type e;
    e.energy = static_cast<int64_t>(llround(static_cast<double>(c.reported) * 1e6));  // kWh -> mWh
    e.endSystime.SetValue(chip::System::SystemClock().GetMonotonicTimestamp().count());
    return MakeNullable(e);
}

static void meter_task(void *)
{
    mb_cfg_t cfg = {
        .port = RS485_UART, .tx_pin = RS485_TX, .rx_pin = RS485_RX,
        .de_pin = RS485_DE, .baud = 9600,
    };
    le03mw_t meter = {.port = RS485_UART, .addr = METER_ADDR};
    while (le03mw_probe(&meter, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Meter not found, retrying in 10 s");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    bool accuracy_set[N_EP] = {};
    float sent_p[N_EP], sent_v[3], sent_i[3];
    Counter import_c[N_EP], export_c;
    static const char *const kPhaseImport[] = {"import L1", "import L2", "import L3"};
    int fails = 0;
    le03mw_data_t d;

    auto reset_sent = [&]() {
        for (float &x : sent_p) x = NAN;
        for (int k = 0; k < 3; k++) sent_v[k] = sent_i[k] = NAN;
    };
    reset_sent();

    while (true) {
        esp_err_t err = le03mw_read(&meter, &d);
        fails = (err == ESP_OK) ? 0 : fails + 1;

        {
        lock::ScopedChipStackLock stack_lock(portMAX_DELAY);
        for (int i = 0; i < N_EP; i++) {
            if (!accuracy_set[i] && EEM::GetClusterInstance(s_ep[i])) {
                accuracy_set[i] = eem_set_accuracy(s_ep[i]);
                s_epm[i].SetPowerMode(ElectricalPowerMeasurement::PowerModeEnum::kAc);
            }
        }
        if (err == ESP_OK) {
            if (moved(sent_p[0], d.p_total, DB_KW)) s_epm[0].SetActivePower(milli(d.p_total * 1000.0f));  // kW -> mW
            for (int k = 0; k < 3; k++) {
                auto &epm = s_epm[k + 1];
                if (moved(sent_v[k], d.v[k], DB_VOLT)) epm.SetVoltage(milli(d.v[k]));                // V -> mV
                if (moved(sent_i[k], d.i[k], DB_AMP)) epm.SetActiveCurrent(milli(d.i[k]));           // A -> mA
                if (moved(sent_p[k + 1], d.p[k], DB_KW)) epm.SetActivePower(milli(d.p[k] * 1000.0f)); // kW -> mW
            }
            if (accuracy_set[0]) {
                bool imp = counter_update(import_c[0], d.e_import, "import");
                bool exp = counter_update(export_c, d.e_export, "export");
                if (imp || exp) {
                    EEM::NotifyCumulativeEnergyMeasured(s_ep[0], energy_of(import_c[0]), energy_of(export_c));
                }
            }
            for (int k = 0; k < 3; k++) {
                if (accuracy_set[k + 1] && counter_update(import_c[k + 1], d.e_import_ph[k], kPhaseImport[k])) {
                    EEM::NotifyCumulativeEnergyMeasured(s_ep[k + 1], energy_of(import_c[k + 1]),
                                                        Nullable<EEM::Structs::EnergyMeasurementStruct::Type>());
                }
            }
        } else if (fails == FAIL_LIMIT) {
            reset_sent();  // report fresh values once the meter is back
            for (int i = 0; i < N_EP; i++) {
                s_epm[i].SetActivePower(Nullable<int64_t>());
                if (i) {
                    s_epm[i].SetVoltage(Nullable<int64_t>());
                    s_epm[i].SetActiveCurrent(Nullable<int64_t>());
                }
            }
        }
        }

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "P %.3f kW (%.3f/%.3f/%.3f)  U %.1f/%.1f/%.1f V  import %.3f kWh  export %.3f kWh",
                     d.p_total, d.p[0], d.p[1], d.p[2], d.v[0], d.v[1], d.v[2], d.e_import, d.e_export);
        } else {
            ESP_LOGW(TAG, "meter read failed (%d in a row): %s", fails, esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

// Electrical Sensor endpoint. Totals: import+export. Phases: import only, plus voltage/current.
static uint16_t create_sensor(node_t *node, int i)
{
    bool phase = i > 0;
    endpoint::electrical_sensor::config_t es;
    es.power_topology.feature_flags = cluster::power_topology::feature::node_topology::get_id();
    es.power_topology.delegate = &s_topology;
    auto &epm = es.with_electrical_power_measurement();
    epm.feature_flags = cluster::electrical_power_measurement::feature::alternating_current::get_id();
    epm.delegate = &s_epm[i];
    auto &eem = es.with_electrical_energy_measurement();
    eem.feature_flags = cluster::electrical_energy_measurement::feature::imported_energy::get_id() |
                        cluster::electrical_energy_measurement::feature::cumulative_energy::get_id();
    if (!phase) {
        eem.feature_flags |= cluster::electrical_energy_measurement::feature::exported_energy::get_id();
    }

    endpoint_t *ep = endpoint::electrical_sensor::create(node, &es, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(ep != nullptr, ESP_LOGE(TAG, "Failed to create electrical sensor endpoint %d", i));
    if (phase) {
        cluster_t *c = cluster::get(ep, ElectricalPowerMeasurement::Id);
        cluster::electrical_power_measurement::attribute::create_voltage(c, nullable<int64_t>());
        cluster::electrical_power_measurement::attribute::create_active_current(c, nullable<int64_t>());
    }
    return endpoint::get_id(ep);
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t, uint16_t, uint8_t, uint8_t, void *)
{
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t, uint16_t, uint32_t, uint32_t,
                                         esp_matter_attr_val_t *, void *)
{
    return ESP_OK;  // read-only sensor
}

extern "C" void app_main()
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unusable (%s), erasing it - the Matter pairing is lost", esp_err_to_name(nvs_err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ABORT_APP_ON_FAILURE(nvs_err == ESP_OK, ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_err)));

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    for (int i = 0; i < N_EP; i++) {
        s_ep[i] = create_sensor(node, i);
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    esp_openthread_platform_config_t ot_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&ot_config);
#endif

    esp_err_t err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    xTaskCreate(meter_task, "meter", 8192, nullptr, 5, nullptr);  // Matter calls + float logging run here
}
