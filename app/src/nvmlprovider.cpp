#include "nvmlprovider.h"

#include <QByteArray>

namespace {
template <typename T>
T symbol(QLibrary &library, const char *name) {
  return reinterpret_cast<T>(library.resolve(name));
}
}  // namespace

NvmlProvider::~NvmlProvider() { shutdown(); }

bool NvmlProvider::initialize() {
  if (initialized_) return true;
  library_.setFileName(QStringLiteral("nvml"));
  if (!library_.load()) return false;
  init_ = symbol<decltype(init_)>(library_, "nvmlInit_v2");
  shutdown_ = symbol<decltype(shutdown_)>(library_, "nvmlShutdown");
  getCount_ = symbol<decltype(getCount_)>(library_, "nvmlDeviceGetCount_v2");
  getHandle_ = symbol<decltype(getHandle_)>(
      library_, "nvmlDeviceGetHandleByIndex_v2");
  getName_ = symbol<decltype(getName_)>(library_, "nvmlDeviceGetName");
  getTemperature_ = symbol<decltype(getTemperature_)>(
      library_, "nvmlDeviceGetTemperature");
  getPower_ = symbol<decltype(getPower_)>(library_, "nvmlDeviceGetPowerUsage");
  getClock_ = symbol<decltype(getClock_)>(library_, "nvmlDeviceGetClockInfo");
  getFan_ = symbol<decltype(getFan_)>(library_, "nvmlDeviceGetFanSpeed");
  if (!init_ || !shutdown_ || !getCount_ || !getHandle_ || !getName_ ||
      !getTemperature_ || init_() != 0) {
    library_.unload();
    return false;
  }
  initialized_ = true;
  return true;
}

QHash<QString, double> NvmlProvider::poll(const QString &adapterName) {
  QHash<QString, double> output;
  if (!adapterName.contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive) ||
      !initialize())
    return output;
  unsigned int count = 0;
  if (getCount_(&count) != 0) return output;
  for (unsigned int i = 0; i < count; ++i) {
    Device device = nullptr;
    char name[128]{};
    if (getHandle_(i, &device) != 0 || !device ||
        getName_(device, name, sizeof(name)) != 0)
      continue;
    const QString nvmlName = QString::fromUtf8(name);
    if (nvmlName.compare(adapterName, Qt::CaseInsensitive) != 0 &&
        !adapterName.contains(nvmlName, Qt::CaseInsensitive) &&
        !nvmlName.contains(adapterName, Qt::CaseInsensitive))
      continue;
    unsigned int value = 0;
    if (getTemperature_(device, 0, &value) == 0)
      output[QStringLiteral("temperature")] = value;
    if (getPower_ && getPower_(device, &value) == 0)
      output[QStringLiteral("power")] = value / 1000.0;
    if (getClock_ && getClock_(device, 0, &value) == 0)
      output[QStringLiteral("frequency")] = value;
    if (getFan_ && getFan_(device, &value) == 0)
      output[QStringLiteral("fan")] = value;
    break;
  }
  return output;
}

void NvmlProvider::shutdown() {
  if (initialized_ && shutdown_) shutdown_();
  initialized_ = false;
  init_ = nullptr;
  shutdown_ = nullptr;
  if (library_.isLoaded()) library_.unload();
}
