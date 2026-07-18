#pragma once

#include <QByteArray>
#include <QHash>
#include <QStringList>

#include "projectmodel.h"

struct PackCompileResult {
  QByteArray data;
  QHash<QString, quint16> channels;
  QStringList warnings;
  QString error;
  bool ok() const { return error.isEmpty() && !data.isEmpty(); }
};

class PackCompiler {
 public:
  static PackCompileResult compile(const ProjectModel &project);
  static QByteArray monochromeBits(const QImage &source, QSize size,
                                   int threshold, const QString &dither,
                                   bool inverted);
  static QByteArray rle(const QByteArray &bytes);
};
