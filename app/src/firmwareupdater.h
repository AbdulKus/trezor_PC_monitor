#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class FirmwareUpdater : public QObject {
  Q_OBJECT
 public:
  explicit FirmwareUpdater(QObject *parent = nullptr);
  bool validate(const QByteArray &firmware, QString *error,
                QString *sha256 = nullptr) const;
  void flash(const QString &path);
 signals:
  void statusChanged(const QString &status);
  void progressChanged(int percent);
  void finished(bool success, const QString &message);
};
