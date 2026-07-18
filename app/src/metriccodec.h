#pragma once

#include <QString>

#include "protocol.h"
#include "telemetry.h"

namespace MetricCodec {

tm_metric_entry_t encode(quint16 channel, const MetricSample &sample);
QString format(const MetricSample &sample, quint8 precision);

}  // namespace MetricCodec
