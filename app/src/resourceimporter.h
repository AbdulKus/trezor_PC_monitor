#pragma once

#include <QString>

#include "projectmodel.h"

class ResourceImporter {
 public:
  static bool importFile(const QString &path, ResourceModel *resource,
                         QString *error = nullptr);
  static bool suggestedInversion(const QImage &image);
};
