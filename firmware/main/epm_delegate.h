#pragma once
// Electrical Power Measurement + Power Topology delegates for the LE-03MW endpoints.
// Only active power, voltage and active current are measured; everything else reads as null.

#include <app/clusters/electrical-power-measurement-server/ElectricalPowerMeasurementDelegate.h>
#include <app/clusters/power-topology-server/power-topology-server.h>

class MeterEpmDelegate : public chip::app::Clusters::ElectricalPowerMeasurement::Delegate {
public:
    using Nullable = chip::app::DataModel::Nullable<int64_t>;
    using PowerModeEnum = chip::app::Clusters::ElectricalPowerMeasurement::PowerModeEnum;
    using AccuracyType = chip::app::Clusters::ElectricalPowerMeasurement::Structs::MeasurementAccuracyStruct::Type;
    using RangeType = chip::app::Clusters::ElectricalPowerMeasurement::Structs::MeasurementRangeStruct::Type;
    using HarmonicType = chip::app::Clusters::ElectricalPowerMeasurement::Structs::HarmonicMeasurementStruct::Type;

    // Setters report the attribute only when the value actually changed.
    void SetPowerMode(PowerModeEnum mode);
    void SetVoltage(Nullable mV);
    void SetActiveCurrent(Nullable mA);
    void SetActivePower(Nullable mW);

    PowerModeEnum GetPowerMode() override { return mPowerMode; }
    uint8_t GetNumberOfMeasurementTypes() override;
    CHIP_ERROR StartAccuracyRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetAccuracyByIndex(uint8_t index, AccuracyType &accuracy) override;
    CHIP_ERROR EndAccuracyRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR StartRangesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetRangeByIndex(uint8_t, RangeType &) override { return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED; }
    CHIP_ERROR EndRangesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR StartHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicCurrentsByIndex(uint8_t, HarmonicType &) override { return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED; }
    CHIP_ERROR EndHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR StartHarmonicPhasesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicPhasesByIndex(uint8_t, HarmonicType &) override { return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED; }
    CHIP_ERROR EndHarmonicPhasesRead() override { return CHIP_NO_ERROR; }

    Nullable GetVoltage() override { return mVoltage; }
    Nullable GetActiveCurrent() override { return mActiveCurrent; }
    Nullable GetActivePower() override { return mActivePower; }
    Nullable GetReactiveCurrent() override { return {}; }
    Nullable GetApparentCurrent() override { return {}; }
    Nullable GetReactivePower() override { return {}; }
    Nullable GetApparentPower() override { return {}; }
    Nullable GetRMSVoltage() override { return {}; }
    Nullable GetRMSCurrent() override { return {}; }
    Nullable GetRMSPower() override { return {}; }
    Nullable GetFrequency() override { return {}; }
    Nullable GetPowerFactor() override { return {}; }
    Nullable GetNeutralCurrent() override { return {}; }

private:
    void Update(Nullable &field, Nullable value, chip::AttributeId id);

    PowerModeEnum mPowerMode = PowerModeEnum::kUnknown;
    Nullable mVoltage;
    Nullable mActiveCurrent;
    Nullable mActivePower;
};

// Node topology: every endpoint measures the whole node (or one phase of it), no child endpoints.
class MeterTopologyDelegate : public chip::app::Clusters::PowerTopology::Delegate {
public:
    CHIP_ERROR GetAvailableEndpointAtIndex(size_t, chip::EndpointId &) override { return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED; }
    CHIP_ERROR GetActiveEndpointAtIndex(size_t, chip::EndpointId &) override { return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED; }
};
