#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

class ZipStore {
 public:
  static bool write(const QString &path, const QHash<QString, QByteArray> &files,
                    QString *error = nullptr);
  static bool read(const QString &path, QHash<QString, QByteArray> *files,
                   QString *error = nullptr);
};
