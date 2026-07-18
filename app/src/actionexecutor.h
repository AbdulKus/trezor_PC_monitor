#pragma once

#include <QObject>

#include "projectmodel.h"

class ActionExecutor : public QObject {
  Q_OBJECT
 public:
 explicit ActionExecutor(ProjectModel *project, QObject *parent = nullptr);
 public slots:
  void execute(quint16 actionId, quint32 eventId);
  void test(quint16 actionId);
  void resetEventSequence();
 signals:
  void actionFailed(const QString &message);
  void actionCompleted(const QString &message);
 private:
  bool workstationUnlocked() const;
  bool sendShortcut(const QString &shortcut);
  void executeAction(const HostAction &action);
  ProjectModel *project_;
  quint32 lastEventId_ = 0;
};
