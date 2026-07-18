#include "metriccodec.h"

#include <QtMath>

namespace MetricCodec {

tm_metric_entry_t encode(quint16 channel, const MetricSample &sample) {
  tm_metric_entry_t entry{};
  entry.channel_id = channel;
  if (!sample.valid) {
    entry.status = TM_STATUS_UNAVAILABLE;
    return entry;
  }
  entry.status = sample.estimated ? TM_STATUS_ESTIMATED : TM_STATUS_VALID;
  if (sample.unit == "%" || sample.unit == "C" || sample.unit == "W" ||
      sample.unit.isEmpty()) {
    entry.scale_exponent = -2;
    entry.value = qint32(qRound64(
        qBound(-21474836.0, sample.value, 21474836.0) * 100.0));
  } else {
    entry.scale_exponent = 0;
    entry.value = qint32(qRound64(
        qBound(-2147483647.0, sample.value, 2147483647.0)));
  }
  return entry;
}

QString format(const MetricSample &sample, quint8 precision) {
  const tm_metric_entry_t entry = encode(0, sample);
  char output[32];
  tm_format_metric(&entry, precision, output);
  return QString::fromLatin1(output);
}

}  // namespace MetricCodec
