#pragma once

#include <QObject>

#include "projectmodel.h"

class ActionExecutor : public QObject {
  Q_OBJECT
 public:
  explicit ActionExecutor(ProjectModel *project, QObject *parent = nullptr);
 public slots:
  void execute(quint16 actionId, quint32 eventId);
 signals:
  void actionFailed(const QString &message);
 private:
  bool workstationUnlocked() const;
  bool sendShortcut(const QString &shortcut);
  ProjectModel *project_;
  quint32 lastEventId_ = 0;
};
