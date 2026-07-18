#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include "presentmonprovider.h"
#include "nvmlprovider.h"

struct MetricSample {
  double value = 0;
  QString unit;
  bool valid = false;
  bool estimated = false;
};

struct GpuInfo {
  QString name;
  QString key;
  QByteArray luid;
  quint64 dedicatedBytes = 0;
  double load = 0;
  quint64 usedBytes = 0;
  enum Policy { Never, Auto, Always } policy = Auto;
  int activeSamples = 0;
  qint64 lastActiveMs = 0;
};

class TelemetryManager : public QObject {
  Q_OBJECT
 public:
  explicit TelemetryManager(QObject *parent = nullptr);
  ~TelemetryManager() override;
  void start(int intervalMs = 250);
  void stop();
  const QHash<QString, MetricSample> &samples() const { return samples_; }
  const QVector<GpuInfo> &gpus() const { return gpus_; }
  QString presentMonStatus() const { return presentMon_.status(); }

 signals:
  void updated(const QHash<QString, MetricSample> &samples);
  void gpuListChanged();

 private slots:
  void sample();

 private:
  void enumerateGpus();
  void sampleWindowsCounters();
  quint32 foregroundProcess();

  QTimer timer_;
  QHash<QString, MetricSample> samples_;
  QVector<GpuInfo> gpus_;
  PresentMonProvider presentMon_;
  NvmlProvider nvml_;
  quint64 previousIdle_ = 0;
  quint64 previousKernel_ = 0;
  quint64 previousUser_ = 0;
  void *pdhQuery_ = nullptr;
  void *gpuUsageCounter_ = nullptr;
  void *gpuMemoryCounter_ = nullptr;
  void *thermalCounter_ = nullptr;
  quint32 lastExternalForegroundPid_ = 0;
};
