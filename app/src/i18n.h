#pragma once

#include <QString>

class QApplication;
class QObject;

namespace I18n {

void initialize(QApplication &application);
QString settingsPath();
QString language();
bool isEnglish();
void setLanguage(const QString &language);
QString text(const QString &source);
void translateUi(QObject *root);

}  // namespace I18n
