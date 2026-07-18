#pragma once

#include <QFont>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QRect>
#include <QString>
#include <QVector>

#include "protocol.h"

enum class HostActionType {
  None,
  Shortcut,
  MediaKey,
  Launch,
  Url,
  Command,
  PowerShell,
};

struct HostAction {
  quint16 id = 0;
  QString name;
  HostActionType type = HostActionType::None;
  QString value;
  bool allowWhenLocked = false;
};

struct ButtonBinding {
  tm_action_type_t type = TM_ACTION_NONE;
  quint8 target = 0;
  quint16 hostActionId = 0;
};

struct WidgetModel {
  QString id;
  tm_widget_type_t type = TM_WIDGET_DYNAMIC_TEXT;
  QRect geometry{0, 0, 40, 9};
  QString text = QStringLiteral("{v}");
  QString metric = QStringLiteral("cpu.total.load");
  qint32 minimum = 0;
  qint32 maximum = 100;
  quint8 precision = 0;
  QFont font{QStringLiteral("Spleen 5x8")};
  int resourceIndex = -1;
  quint16 arg0 = 0;
  quint16 arg1 = 0;
  bool inverted = false;
  bool autoRange = false;
};

struct ScreenModel {
  QString name = QStringLiteral("Экран");
  bool enabled = true;
  QVector<WidgetModel> widgets;
  ButtonBinding leftShort;
  ButtonBinding leftLong;
  ButtonBinding rightShort;
  ButtonBinding rightLong;
};

struct ResourceModel {
  QString id;
  QString name;
  QVector<QImage> frames;
  QVector<int> durationsMs;
  int threshold = 128;
  QString dither = QStringLiteral("ordered");
  bool inverted = false;
};

class ProjectModel : public QObject {
  Q_OBJECT
 public:
  explicit ProjectModel(QObject *parent = nullptr);

  QVector<ScreenModel> &screens() { return screens_; }
  const QVector<ScreenModel> &screens() const { return screens_; }
  QVector<ResourceModel> &resources() { return resources_; }
  const QVector<ResourceModel> &resources() const { return resources_; }
  QVector<HostAction> &actions() { return actions_; }
  const QVector<HostAction> &actions() const { return actions_; }

  int currentScreen() const { return currentScreen_; }
  void setCurrentScreen(int index);
  QString filePath() const { return filePath_; }
  bool modified() const { return modified_; }
  void setModified(bool modified = true);
  bool burnInProtection() const { return burnInProtection_; }
  int pixelShiftInset() const { return pixelShiftInset_; }
  void setBurnInProtection(bool enabled);
  void setPixelShiftInset(int pixels);

  void resetToDefault();
  bool save(const QString &path, QString *error = nullptr);
  bool load(const QString &path, QString *error = nullptr);
  QJsonObject toJson() const;
  bool fromJson(const QJsonObject &root, QString *error = nullptr);

 signals:
  void changed();
  void currentScreenChanged(int index);
  void modifiedChanged(bool modified);

 private:
  QVector<ScreenModel> screens_;
  QVector<ResourceModel> resources_;
  QVector<HostAction> actions_;
  int currentScreen_ = 0;
  QString filePath_;
  bool modified_ = false;
  bool burnInProtection_ = false;
  int pixelShiftInset_ = 1;
};

QString widgetTypeName(tm_widget_type_t type);
QString actionTypeName(HostActionType type);
