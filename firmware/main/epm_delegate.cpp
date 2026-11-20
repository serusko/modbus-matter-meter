#include "epm_delegate.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/reporting/reporting.h>
#include <lib/support/CodeUtils.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ElectricalPowerMeasurement;
using namespace chip::app::Clusters::ElectricalPowerMeasurement::Attributes;
using namespace chip::app::Clusters::ElectricalPowerMeasurement::Structs;

namespace {
// LE-03MW: 3x230/400 V, 5(80) A, accuracy class 1 (1 %).
constexpr Percent100ths kOnePercent = 100;

const MeasurementAccuracyRangeStruct::Type kPowerRange[] = {
    {.rangeMin = -55'000'000, .rangeMax = 55'000'000, .percentMax = MakeOptional(kOnePercent)},  // mW
};
const MeasurementAccuracyRangeStruct::Type kCurrentRange[] = {
    {.rangeMin = -80'000, .rangeMax = 80'000, .percentMax = MakeOptional(kOnePercent)},  // mA
};
const MeasurementAccuracyRangeStruct::Type kVoltageRange[] = {
    {.rangeMin = 0, .rangeMax = 300'000, .percentMax = MakeOptional(kOnePercent)},  // mV
};

const MeasurementAccuracyStruct::Type kAccuracies[] = {
    {.measurementType = MeasurementTypeEnum::kActivePower, .measured = true,
     .minMeasuredValue = -55'000'000, .maxMeasuredValue = 55'000'000,
     .accuracyRanges = DataModel::List<const MeasurementAccuracyRangeStruct::Type>(kPowerRange)},
    {.measurementType = MeasurementTypeEnum::kActiveCurrent, .measured = true,
     .minMeasuredValue = -80'000, .maxMeasuredValue = 80'000,
     .accuracyRanges = DataModel::List<const MeasurementAccuracyRangeStruct::Type>(kCurrentRange)},
    {.measurementType = MeasurementTypeEnum::kVoltage, .measured = true,
     .minMeasuredValue = 0, .maxMeasuredValue = 300'000,
     .accuracyRanges = DataModel::List<const MeasurementAccuracyRangeStruct::Type>(kVoltageRange)},
};
} // namespace

uint8_t MeterEpmDelegate::GetNumberOfMeasurementTypes()
{
    return MATTER_ARRAY_SIZE(kAccuracies);
}

CHIP_ERROR MeterEpmDelegate::GetAccuracyByIndex(uint8_t index, AccuracyType &accuracy)
{
    VerifyOrReturnError(index < MATTER_ARRAY_SIZE(kAccuracies), CHIP_ERROR_PROVIDER_LIST_EXHAUSTED);
    accuracy = kAccuracies[index];
    return CHIP_NO_ERROR;
}

void MeterEpmDelegate::Update(Nullable &field, Nullable value, AttributeId id)
{
    if (field == value) return;
    field = value;
    MatterReportingAttributeChangeCallback(mEndpointId, ElectricalPowerMeasurement::Id, id);
}

void MeterEpmDelegate::SetPowerMode(PowerModeEnum mode)
{
    if (mPowerMode == mode) return;
    mPowerMode = mode;
    MatterReportingAttributeChangeCallback(mEndpointId, ElectricalPowerMeasurement::Id, PowerMode::Id);
}

void MeterEpmDelegate::SetVoltage(Nullable mV) { Update(mVoltage, mV, Voltage::Id); }
void MeterEpmDelegate::SetActiveCurrent(Nullable mA) { Update(mActiveCurrent, mA, ActiveCurrent::Id); }
void MeterEpmDelegate::SetActivePower(Nullable mW) { Update(mActivePower, mW, ActivePower::Id); }
