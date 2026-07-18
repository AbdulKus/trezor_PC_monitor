#include "presentmonprovider.h"

#include <QDebug>
#include <QStringList>

#include <cstring>
#include <utility>

template <typename T>
static T resolve(QLibrary &library, const char *name) {
  return reinterpret_cast<T>(library.resolve(name));
}

PresentMonProvider::PresentMonProvider() {
  const QStringList candidates = {
      QStringLiteral("PresentMonAPI2Loader"),
      QStringLiteral("PresentMonAPI2"),
      QStringLiteral("PresentMonAPI")};
  for (const QString &candidate : candidates) {
    library_.setFileName(candidate);
    if (library_.load()) break;
  }
  if (!library_.isLoaded()) {
    status_ = QStringLiteral("PresentMon API не установлен");
    return;
  }
  openSession_ = resolve<decltype(openSession_)>(library_, "pmOpenSession");
  closeSession_ = resolve<decltype(closeSession_)>(library_, "pmCloseSession");
  startTracking_ =
      resolve<decltype(startTracking_)>(library_, "pmStartTrackingProcess");
  stopTracking_ =
      resolve<decltype(stopTracking_)>(library_, "pmStopTrackingProcess");
  getRoot_ =
      resolve<decltype(getRoot_)>(library_, "pmGetIntrospectionRoot");
  freeRoot_ =
      resolve<decltype(freeRoot_)>(library_, "pmFreeIntrospectionRoot");
  setTelemetryPeriod_ = resolve<decltype(setTelemetryPeriod_)>(
      library_, "pmSetTelemetryPollingPeriod");
  registerQuery_ = resolve<decltype(registerQuery_)>(
      library_, "pmRegisterDynamicQuery");
  freeQuery_ =
      resolve<decltype(freeQuery_)>(library_, "pmFreeDynamicQuery");
  pollQuery_ =
      resolve<decltype(pollQuery_)>(library_, "pmPollDynamicQuery");
  if (!openSession_ || !closeSession_ || !startTracking_ || !stopTracking_ ||
      !getRoot_ || !freeRoot_ || !registerQuery_ || !freeQuery_ ||
      !pollQuery_ || openSession_(&session_) != PM_STATUS_SUCCESS) {
    status_ = QStringLiteral("PresentMon service недоступен");
    session_ = nullptr;
    library_.unload();
    return;
  }
  if (setTelemetryPeriod_) setTelemetryPeriod_(session_, 0, 250);
  if (getRoot_(session_, &root_) != PM_STATUS_SUCCESS) root_ = nullptr;
  if (root_ && root_->pDevices) {
    for (size_t i = 0; i < root_->pDevices->size; ++i) {
      const auto *device = static_cast<const PM_INTROSPECTION_DEVICE *>(
          root_->pDevices->pData[i]);
      if (!device) continue;
      const QByteArray luid = device->pLuid
          ? QByteArray(reinterpret_cast<const char *>(device->pLuid->pData),
                       int(device->pLuid->size))
          : QByteArray();
      qInfo() << "PresentMon device" << device->id << int(device->type)
              << (device->pName ? device->pName->pData : "") << luid.toHex();
    }
  }
  status_ = QStringLiteral("PresentMon подключён");
}

PresentMonProvider::~PresentMonProvider() {
  closeQuery();
  if (trackedPid_ && stopTracking_) stopTracking_(session_, trackedPid_);
  if (root_ && freeRoot_) freeRoot_(root_);
  if (session_ && closeSession_) closeSession_(session_);
}

void PresentMonProvider::setTarget(quint32 processId,
                                   const QByteArray &activeGpuLuid,
                                   const QString &activeGpuName) {
  if (targetPid_ == processId && activeGpuLuid_ == activeGpuLuid &&
      activeGpuName_ == activeGpuName)
    return;
  targetPid_ = processId;
  activeGpuLuid_ = activeGpuLuid;
  activeGpuName_ = activeGpuName;
  rebuildQuery();
}

void PresentMonProvider::closeQuery() {
  if (query_ && freeQuery_) freeQuery_(query_);
  query_ = nullptr;
  values_.clear();
  blobSize_ = 0;
}

uint32_t PresentMonProvider::findSystemDevice() const {
  if (!root_ || !root_->pDevices) return UINT32_MAX;
  for (size_t i = 0; i < root_->pDevices->size; i++) {
    auto *device = static_cast<const PM_INTROSPECTION_DEVICE *>(
        root_->pDevices->pData[i]);
    if (device && device->type == PM_DEVICE_TYPE_SYSTEM) return device->id;
  }
  return UINT32_MAX;
}

uint32_t PresentMonProvider::findGpuDevice(const QByteArray &luid,
                                           const QString &name) const {
  if (!root_ || !root_->pDevices) return UINT32_MAX;
  for (size_t i = 0; i < root_->pDevices->size; i++) {
    auto *device = static_cast<const PM_INTROSPECTION_DEVICE *>(
        root_->pDevices->pData[i]);
    if (!luid.isEmpty() && device &&
        device->type == PM_DEVICE_TYPE_GRAPHICS_ADAPTER &&
        device->pLuid && int(device->pLuid->size) == luid.size() &&
        memcmp(device->pLuid->pData, luid.constData(), luid.size()) == 0)
      return device->id;
  }
  // PresentMon 2.5 may omit LUIDs from introspection. DXGI still supplies the
  // stable LUID on our side; matching the exact adapter name is a safe fallback.
  for (size_t i = 0; i < root_->pDevices->size; i++) {
    auto *device = static_cast<const PM_INTROSPECTION_DEVICE *>(
        root_->pDevices->pData[i]);
    if (device && device->type == PM_DEVICE_TYPE_GRAPHICS_ADAPTER &&
        device->pName &&
        QString::fromUtf8(device->pName->pData).compare(name,
                                                        Qt::CaseInsensitive) == 0)
      return device->id;
  }
  return UINT32_MAX;
}

bool PresentMonProvider::metricAvailable(PM_METRIC metric,
                                         uint32_t device) const {
  if (!root_ || !root_->pMetrics) return false;
  for (size_t i = 0; i < root_->pMetrics->size; ++i) {
    const auto *info = static_cast<const PM_INTROSPECTION_METRIC *>(
        root_->pMetrics->pData[i]);
    if (!info || info->id != metric || !info->pDeviceMetricInfo) continue;
    for (size_t d = 0; d < info->pDeviceMetricInfo->size; ++d) {
      const auto *deviceInfo =
          static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO *>(
              info->pDeviceMetricInfo->pData[d]);
      if (deviceInfo && deviceInfo->deviceId == device)
        return deviceInfo->availability == PM_METRIC_AVAILABILITY_AVAILABLE;
    }
  }
  return false;
}

bool PresentMonProvider::rebuildQuery() {
  if (!session_) return false;
  closeQuery();
  if (trackedPid_ && trackedPid_ != targetPid_) {
    stopTracking_(session_, trackedPid_);
    trackedPid_ = 0;
  }
  if (targetPid_ && trackedPid_ != targetPid_) {
    PM_STATUS status = startTracking_(session_, targetPid_);
    if (status == PM_STATUS_SUCCESS || status == PM_STATUS_ALREADY_TRACKING_PROCESS)
      trackedPid_ = targetPid_;
  }
  uint32_t system = findSystemDevice();
  uint32_t gpu = findGpuDevice(activeGpuLuid_, activeGpuName_);
  auto add = [&](QString name, PM_METRIC metric, PM_STAT stat, uint32_t device) {
    if (!metricAvailable(metric, device)) {
      qInfo() << "PresentMon metric unavailable" << name << "device" << device;
      return;
    }
    QueryValue value;
    value.name = std::move(name);
    value.element = PM_QUERY_ELEMENT{metric, stat, device, 0, 0, 0};
    values_ << value;
  };
  qInfo() << "PresentMon query target" << targetPid_ << "system" << system
          << "gpu" << gpu;
  if (targetPid_) {
    add("foreground.fps.displayed", PM_METRIC_DISPLAYED_FPS, PM_STAT_AVG, 0);
    add("foreground.fps.presented", PM_METRIC_PRESENTED_FPS, PM_STAT_AVG, 0);
    add("foreground.fps.1low", PM_METRIC_DISPLAYED_FPS,
        PM_STAT_PERCENTILE_01, 0);
  }
  if (system != UINT32_MAX) {
    add("cpu.package.temperature", PM_METRIC_CPU_TEMPERATURE, PM_STAT_AVG,
        system);
    add("cpu.package.power", PM_METRIC_CPU_POWER, PM_STAT_AVG, system);
    add("cpu.frequency", PM_METRIC_CPU_FREQUENCY, PM_STAT_AVG, system);
  }
  if (gpu != UINT32_MAX) {
    add("gpu.active.temperature", PM_METRIC_GPU_TEMPERATURE, PM_STAT_AVG, gpu);
    add("gpu.active.power", PM_METRIC_GPU_CARD_POWER, PM_STAT_AVG, gpu);
    add("gpu.active.frequency", PM_METRIC_GPU_FREQUENCY, PM_STAT_AVG, gpu);
    add("gpu.active.fan", PM_METRIC_GPU_FAN_SPEED, PM_STAT_AVG, gpu);
  }
  if (values_.isEmpty()) return false;
  QVector<PM_QUERY_ELEMENT> elements;
  for (const QueryValue &value : values_) elements << value.element;
  const PM_STATUS registerStatus = registerQuery_(
      session_, &query_, elements.data(), elements.size(), 1000.0, 0.0);
  if (registerStatus != PM_STATUS_SUCCESS) {
    qWarning() << "PresentMon query registration failed" << int(registerStatus)
               << "elements" << elements.size();
    query_ = nullptr;
    return false;
  }
  for (int i = 0; i < values_.size(); i++) values_[i].element = elements[i];
  blobSize_ = int(elements.last().dataOffset + elements.last().dataSize);
  return blobSize_ > 0;
}

QHash<QString, PresentMonValue> PresentMonProvider::poll() {
  QHash<QString, PresentMonValue> output;
  if (!query_ && !rebuildQuery()) return output;
  QByteArray blobs(blobSize_ * 4, '\0');
  uint32_t swapChains = 4;
  const PM_STATUS pollStatus = pollQuery_(
      query_, targetPid_, reinterpret_cast<uint8_t *>(blobs.data()),
      &swapChains);
  if (pollStatus == PM_STATUS_SUCCESS && swapChains == 0) {
    // A foreground process without a presenting swap chain has no FPS row,
    // but system/GPU telemetry is still available through the global row.
    swapChains = 4;
    const PM_STATUS globalStatus = pollQuery_(
        query_, 0, reinterpret_cast<uint8_t *>(blobs.data()), &swapChains);
    if (globalStatus != PM_STATUS_SUCCESS) {
      qWarning() << "PresentMon global query poll failed" << int(globalStatus);
      return output;
    }
  }
  if (pollStatus != PM_STATUS_SUCCESS || swapChains == 0) {
    if (pollStatus != PM_STATUS_SUCCESS)
      qWarning() << "PresentMon query poll failed" << int(pollStatus)
                 << "pid" << targetPid_;
    return output;
  }
  for (const QueryValue &query : values_) {
    if (query.element.dataSize == sizeof(double) &&
        query.element.dataOffset + sizeof(double) <= uint64_t(blobs.size())) {
      double value;
      memcpy(&value, blobs.constData() + query.element.dataOffset, sizeof(value));
      output[query.name] = PresentMonValue{value, true};
    }
  }
  return output;
}
