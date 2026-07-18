#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QThread>

#include <atomic>

#include "protocol.h"
#include "telemetry.h"

class DeviceConnection : public QThread {
  Q_OBJECT
 public:
  explicit DeviceConnection(QObject *parent = nullptr);
  ~DeviceConnection() override;

  void uploadPack(const QByteArray &pack, const QHash<QString, quint16> &channels);
  void updateMetrics(const QHash<QString, MetricSample> &samples);
  void setPage(quint8 page);
  void requestBootloader();
  void stop();

 signals:
  void connectedChanged(bool connected);
  void statusChanged(const QString &status);
  void uploadProgress(int percent);
  void packUploaded(bool success, const QString &message);
  void buttonEvent(quint16 actionId, quint32 eventId);

 protected:
  void run() override;

 private:
  struct PendingCommand {
    enum Type { None, Upload, SetPage, Bootloader } type = None;
    QByteArray pack;
    QHash<QString, quint16> channels;
    quint8 page = 0;
  };

  QMutex mutex_;
  PendingCommand command_;
  QHash<QString, MetricSample> latestSamples_;
  bool metricsDirty_ = false;
  std::atomic_bool stopping_ = false;
};
