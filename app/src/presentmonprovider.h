#pragma once

#include <QByteArray>
#include <QHash>
#include <QLibrary>
#include <QString>
#include <QVector>

#include "presentmon_compat.h"

struct PresentMonValue {
  double value = 0;
  bool valid = false;
};

class PresentMonProvider {
 public:
  PresentMonProvider();
  ~PresentMonProvider();

  bool available() const { return session_ != nullptr; }
  QString status() const { return status_; }
  void setTarget(quint32 processId, const QByteArray &activeGpuLuid,
                 const QString &activeGpuName = {});
  QHash<QString, PresentMonValue> poll();

 private:
  struct QueryValue {
    QString name;
    PM_QUERY_ELEMENT element{};
  };
  void closeQuery();
  bool rebuildQuery();
  uint32_t findSystemDevice() const;
  uint32_t findGpuDevice(const QByteArray &luid, const QString &name) const;
  bool metricAvailable(PM_METRIC metric, uint32_t device) const;

  QLibrary library_;
  PM_SESSION_HANDLE session_ = nullptr;
  PM_DYNAMIC_QUERY_HANDLE query_ = nullptr;
  const PM_INTROSPECTION_ROOT *root_ = nullptr;
  quint32 targetPid_ = 0;
  quint32 trackedPid_ = 0;
  QByteArray activeGpuLuid_;
  QString activeGpuName_;
  QVector<QueryValue> values_;
  int blobSize_ = 0;
  QString status_;

  PM_STATUS (*openSession_)(PM_SESSION_HANDLE *) = nullptr;
  PM_STATUS (*closeSession_)(PM_SESSION_HANDLE) = nullptr;
  PM_STATUS (*startTracking_)(PM_SESSION_HANDLE, uint32_t) = nullptr;
  PM_STATUS (*stopTracking_)(PM_SESSION_HANDLE, uint32_t) = nullptr;
  PM_STATUS (*getRoot_)(PM_SESSION_HANDLE, const PM_INTROSPECTION_ROOT **) = nullptr;
  PM_STATUS (*freeRoot_)(const PM_INTROSPECTION_ROOT *) = nullptr;
  PM_STATUS (*setTelemetryPeriod_)(PM_SESSION_HANDLE, uint32_t, uint32_t) = nullptr;
  PM_STATUS (*registerQuery_)(PM_SESSION_HANDLE, PM_DYNAMIC_QUERY_HANDLE *,
                              PM_QUERY_ELEMENT *, uint64_t, double, double) = nullptr;
  PM_STATUS (*freeQuery_)(PM_DYNAMIC_QUERY_HANDLE) = nullptr;
  PM_STATUS (*pollQuery_)(PM_DYNAMIC_QUERY_HANDLE, uint32_t, uint8_t *,
                          uint32_t *) = nullptr;
};
