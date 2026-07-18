#include "telemetry.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi1_6.h>
#include <pdh.h>
#include <psapi.h>
#endif

#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QStringList>

namespace {
#ifdef Q_OS_WIN
quint64 fileTimeValue(const FILETIME &value) {
  return (quint64(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

QString luidKey(quint32 high, quint32 low) {
  return QStringLiteral("%1:%2")
      .arg(high, 8, 16, QLatin1Char('0'))
      .arg(low, 8, 16, QLatin1Char('0'))
      .toLower();
}

QString metricUnit(const QString &name) {
  if (name.contains("fps")) return QStringLiteral("fps");
  if (name.contains("temperature")) return QStringLiteral("C");
  if (name.contains("power")) return QStringLiteral("W");
  if (name.contains("frequency")) return QStringLiteral("MHz");
  if (name.contains("fan")) return QStringLiteral("RPM");
  return {};
}

QHash<QString, double> readPdhArray(PDH_HCOUNTER counter, DWORD format) {
  QHash<QString, double> output;
  DWORD size = 0;
  DWORD count = 0;
  PdhGetFormattedCounterArrayW(counter, format, &size, &count, nullptr);
  if (!size) return output;
  QByteArray buffer(size, '\0');
  auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
  if (PdhGetFormattedCounterArrayW(counter, format, &size, &count, items) !=
      ERROR_SUCCESS)
    return output;
  QRegularExpression expression(
      QStringLiteral("luid_0x([0-9a-fA-F]+)_0x([0-9a-fA-F]+)"));
  for (DWORD i = 0; i < count; i++) {
    QRegularExpressionMatch match = expression.match(QString::fromWCharArray(items[i].szName));
    if (!match.hasMatch() || items[i].FmtValue.CStatus != ERROR_SUCCESS) continue;
    bool highOk = false;
    bool lowOk = false;
    quint32 high = match.captured(1).toUInt(&highOk, 16);
    quint32 low = match.captured(2).toUInt(&lowOk, 16);
    if (!highOk || !lowOk) continue;
    double value = (format & PDH_FMT_DOUBLE)
                       ? items[i].FmtValue.doubleValue
                       : double(items[i].FmtValue.largeValue);
    QString key = luidKey(high, low);
    if (format & PDH_FMT_DOUBLE)
      output[key] = qMax(output.value(key), value);
    else
      output[key] += value;
  }
  return output;
}

double readPdhMaximum(PDH_HCOUNTER counter, bool *valid) {
  *valid = false;
  if (!counter) return 0;
  DWORD size = 0;
  DWORD count = 0;
  PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr);
  if (!size) return 0;
  QByteArray buffer(size, '\0');
  auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
  if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count,
                                   items) != ERROR_SUCCESS)
    return 0;
  double maximum = 0;
  for (DWORD i = 0; i < count; ++i) {
    if (items[i].FmtValue.CStatus != ERROR_SUCCESS) continue;
    maximum = *valid ? qMax(maximum, items[i].FmtValue.doubleValue)
                     : items[i].FmtValue.doubleValue;
    *valid = true;
  }
  return maximum;
}
#endif
}  // namespace

TelemetryManager::TelemetryManager(QObject *parent) : QObject(parent) {
  connect(&timer_, &QTimer::timeout, this, &TelemetryManager::sample);
  enumerateGpus();
#ifdef Q_OS_WIN
  PDH_HQUERY query = nullptr;
  PDH_HCOUNTER usage = nullptr;
  PDH_HCOUNTER memory = nullptr;
  PDH_HCOUNTER thermal = nullptr;
  if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
    PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0,
                          &usage);
    PdhAddEnglishCounterW(query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0,
                          &memory);
    PdhAddEnglishCounterW(query, L"\\Thermal Zone Information(*)\\Temperature",
                          0, &thermal);
    PdhCollectQueryData(query);
    pdhQuery_ = query;
    gpuUsageCounter_ = usage;
    gpuMemoryCounter_ = memory;
    thermalCounter_ = thermal;
  }
#endif
}

TelemetryManager::~TelemetryManager() {
#ifdef Q_OS_WIN
  if (pdhQuery_) PdhCloseQuery(reinterpret_cast<PDH_HQUERY>(pdhQuery_));
#endif
}

void TelemetryManager::start(int intervalMs) {
  timer_.start(qBound(100, intervalMs, 1000));
  sample();
}
void TelemetryManager::stop() { timer_.stop(); }

void TelemetryManager::enumerateGpus() {
  gpus_.clear();
#ifdef Q_OS_WIN
  IDXGIFactory1 *factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                reinterpret_cast<void **>(&factory))))
    return;
  for (UINT index = 0;; index++) {
    IDXGIAdapter1 *adapter = nullptr;
    if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 description{};
    if (SUCCEEDED(adapter->GetDesc1(&description)) &&
        (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
      GpuInfo gpu;
      gpu.name = QString::fromWCharArray(description.Description);
      gpu.key = luidKey(quint32(description.AdapterLuid.HighPart),
                        description.AdapterLuid.LowPart);
      gpu.luid = QByteArray(reinterpret_cast<const char *>(&description.AdapterLuid),
                            sizeof(description.AdapterLuid));
      gpu.dedicatedBytes = description.DedicatedVideoMemory;
      qInfo() << "DXGI device" << gpu.name << gpu.key << gpu.luid.toHex();
      gpus_ << gpu;
    }
    adapter->Release();
  }
  factory->Release();
#endif
  emit gpuListChanged();
}

quint32 TelemetryManager::foregroundProcess() {
#ifdef Q_OS_WIN
  DWORD process = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &process);
  if (process != 0 && process != GetCurrentProcessId())
    lastExternalForegroundPid_ = process;
  return lastExternalForegroundPid_;
#else
  return 0;
#endif
}

void TelemetryManager::sampleWindowsCounters() {
#ifdef Q_OS_WIN
  FILETIME idle{}, kernel{}, user{};
  if (GetSystemTimes(&idle, &kernel, &user)) {
    quint64 idleValue = fileTimeValue(idle);
    quint64 kernelValue = fileTimeValue(kernel);
    quint64 userValue = fileTimeValue(user);
    quint64 total = kernelValue - previousKernel_ + userValue - previousUser_;
    quint64 idleDelta = idleValue - previousIdle_;
    if (previousKernel_ != 0 && total != 0) {
      samples_["cpu.total.load"] =
          MetricSample{100.0 * double(total - idleDelta) / double(total), "%", true};
    }
    previousIdle_ = idleValue;
    previousKernel_ = kernelValue;
    previousUser_ = userValue;
  }

  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory)) {
    double used = double(memory.ullTotalPhys - memory.ullAvailPhys);
    samples_["memory.ram.load"] =
        MetricSample{double(memory.dwMemoryLoad), "%", true};
    samples_["memory.ram.used"] = MetricSample{used / 1048576.0, "MiB", true};
    samples_["memory.ram.total"] =
        MetricSample{double(memory.ullTotalPhys) / 1048576.0, "MiB", true};
  }
  PERFORMANCE_INFORMATION performance{};
  performance.cb = sizeof(performance);
  if (GetPerformanceInfo(&performance, sizeof(performance))) {
    double page = double(performance.PageSize);
    samples_["memory.commit.used"] =
        MetricSample{performance.CommitTotal * page / 1048576.0, "MiB", true};
    samples_["memory.commit.limit"] =
        MetricSample{performance.CommitLimit * page / 1048576.0, "MiB", true};
  }

  if (pdhQuery_) {
    PdhCollectQueryData(reinterpret_cast<PDH_HQUERY>(pdhQuery_));
    QHash<QString, double> loads =
        readPdhArray(reinterpret_cast<PDH_HCOUNTER>(gpuUsageCounter_), PDH_FMT_DOUBLE);
    QHash<QString, double> memories =
        readPdhArray(reinterpret_cast<PDH_HCOUNTER>(gpuMemoryCounter_), PDH_FMT_LARGE);
    bool thermalValid = false;
    double thermal = readPdhMaximum(
        reinterpret_cast<PDH_HCOUNTER>(thermalCounter_), &thermalValid);
    if (thermalValid) {
      // This Windows counter is Kelvin on current systems. It identifies a
      // firmware thermal zone rather than a calibrated CPU package sensor.
      if (thermal > 200.0) thermal -= 273.15;
      if (thermal > -40.0 && thermal < 150.0)
        samples_["cpu.temperature.acpi"] =
            MetricSample{thermal, "C", true, true};
    }
    for (int i = 0; i < gpus_.size(); i++) {
      gpus_[i].load = qBound(0.0, loads.value(gpus_[i].key), 100.0);
      gpus_[i].usedBytes = quint64(qMax(0.0, memories.value(gpus_[i].key)));
      QString prefix = QStringLiteral("gpu.%1.").arg(i);
      samples_[prefix + "load"] = MetricSample{gpus_[i].load, "%", true};
      samples_[prefix + "vram.used"] =
          MetricSample{double(gpus_[i].usedBytes) / 1048576.0, "MiB", true};
      samples_[prefix + "vram.total"] =
          MetricSample{double(gpus_[i].dedicatedBytes) / 1048576.0, "MiB", true};
    }
  }
#else
  samples_["cpu.total.load"] = MetricSample{42, "%", true};
  samples_["memory.ram.load"] = MetricSample{55, "%", true};
#endif
}

void TelemetryManager::sample() {
  sampleWindowsCounters();
  static const QStringList presentMonMetrics = {
      "foreground.fps.displayed", "foreground.fps.presented",
      "foreground.fps.1low", "cpu.package.temperature", "cpu.package.power",
      "cpu.frequency", "gpu.active.temperature", "gpu.active.power",
      "gpu.active.frequency", "gpu.active.fan"};
  for (const QString &name : presentMonMetrics)
    samples_[name] = MetricSample{0, metricUnit(name), false};
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  int active = -1;
  double activeLoad = -1;
  for (int i = 0; i < gpus_.size(); i++) {
    GpuInfo &gpu = gpus_[i];
    if (gpu.load > 0.5) {
      gpu.activeSamples++;
      gpu.lastActiveMs = now;
    } else {
      gpu.activeSamples = 0;
    }
    bool vendorAllowed = gpu.policy == GpuInfo::Always ||
                         (gpu.policy == GpuInfo::Auto &&
                          (gpu.activeSamples >= 2 || now - gpu.lastActiveMs < 30000));
    if (gpu.policy != GpuInfo::Never && vendorAllowed && gpu.load > activeLoad) {
      active = i;
      activeLoad = gpu.load;
    }
  }
  if (active < 0 && !gpus_.isEmpty()) {
    active = 0;
    for (int i = 1; i < gpus_.size(); i++)
      if (gpus_[i].load > gpus_[active].load) active = i;
  }
  QByteArray vendorLuid;
  QString vendorName;
  if (active >= 0 &&
      (gpus_[active].policy == GpuInfo::Always ||
       (gpus_[active].policy == GpuInfo::Auto &&
        (gpus_[active].activeSamples >= 2 ||
         (gpus_[active].lastActiveMs > 0 &&
          now - gpus_[active].lastActiveMs < 30000))))) {
    vendorLuid = gpus_[active].luid;
    vendorName = gpus_[active].name;
  }
  presentMon_.setTarget(foregroundProcess(), vendorLuid, vendorName);
  const auto presentValues = presentMon_.poll();
  for (auto it = presentValues.constBegin(); it != presentValues.constEnd(); ++it) {
    if (!it.value().valid) continue;
    samples_[it.key()] = MetricSample{it.value().value,
                                     metricUnit(it.key()), true};
  }
  if (!samples_.value("cpu.package.temperature").valid) {
    const MetricSample acpi = samples_.value("cpu.temperature.acpi");
    if (acpi.valid) samples_["cpu.package.temperature"] = acpi;
  }
  if (!vendorName.isEmpty()) {
    const QHash<QString, double> nvml = nvml_.poll(vendorName);
    const struct {
      const char *suffix;
      const char *unit;
    } fallbacks[] = {{"temperature", "C"}, {"power", "W"},
                     {"frequency", "MHz"}, {"fan", "RPM"}};
    for (const auto &fallback : fallbacks) {
      const QString key = QStringLiteral("gpu.active.") + fallback.suffix;
      const QString nvmlKey = QString::fromLatin1(fallback.suffix);
      if (!samples_.value(key).valid && nvml.contains(nvmlKey))
        samples_[key] = MetricSample{nvml.value(nvmlKey),
                                     QString::fromLatin1(fallback.unit), true};
    }
  } else {
    nvml_.shutdown();
  }

  if (active >= 0) {
    samples_["gpu.active.load"] = MetricSample{gpus_[active].load, "%", true};
    samples_["gpu.active.vram.used"] =
        MetricSample{double(gpus_[active].usedBytes) / 1048576.0, "MiB", true};
    samples_["gpu.active.vram.total"] = MetricSample{
        double(gpus_[active].dedicatedBytes) / 1048576.0, "MiB", true};
  }
  auto cpuPower = samples_.value("cpu.package.power");
  auto gpuPower = samples_.value("gpu.active.power");
  if (cpuPower.valid && gpuPower.valid)
    samples_["system.power.estimated"] =
        MetricSample{cpuPower.value + gpuPower.value, "W", true, true};
  else
    samples_["system.power.estimated"] = MetricSample{0, "W", false, true};
  emit updated(samples_);
}
