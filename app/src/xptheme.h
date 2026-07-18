#pragma once

#include <QApplication>
#include <QString>

class XpTheme {
 public:
  static void apply(QApplication &application, const QString &variant);
};
