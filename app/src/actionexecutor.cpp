#include "actionexecutor.h"

#include <QDesktopServices>
#include <QHash>
#include <QProcess>
#include <QStringList>
#include <QUrl>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

ActionExecutor::ActionExecutor(ProjectModel *project, QObject *parent)
    : QObject(parent), project_(project) {}

bool ActionExecutor::workstationUnlocked() const {
#ifdef Q_OS_WIN
  HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
  if (!desktop) return false;
  BOOL accessible = SwitchDesktop(desktop);
  CloseDesktop(desktop);
  return accessible;
#else
  return true;
#endif
}

bool ActionExecutor::sendShortcut(const QString &shortcut) {
#ifdef Q_OS_WIN
  QStringList parts = shortcut.toUpper().split('+', Qt::SkipEmptyParts);
  QVector<WORD> modifiers;
  WORD key = 0;
  for (QString part : parts) {
    part = part.trimmed();
    if (part == "CTRL" || part == "CONTROL") modifiers << VK_CONTROL;
    else if (part == "ALT") modifiers << VK_MENU;
    else if (part == "SHIFT") modifiers << VK_SHIFT;
    else if (part == "WIN" || part == "META") modifiers << VK_LWIN;
    else if (part.startsWith('F') && part.mid(1).toInt() >= 1 &&
             part.mid(1).toInt() <= 24)
      key = WORD(VK_F1 + part.mid(1).toInt() - 1);
    else if (part.size() == 1)
      key = WORD(VkKeyScanW(part[0].unicode()) & 0xff);
    else {
      static const QHash<QString, WORD> names = {
          {"SPACE", VK_SPACE}, {"ENTER", VK_RETURN}, {"ESC", VK_ESCAPE},
          {"TAB", VK_TAB},     {"LEFT", VK_LEFT},    {"RIGHT", VK_RIGHT},
          {"UP", VK_UP},       {"DOWN", VK_DOWN},    {"DELETE", VK_DELETE},
          {"HOME", VK_HOME},   {"END", VK_END},      {"PGUP", VK_PRIOR},
          {"PGDN", VK_NEXT}};
      key = names.value(part, 0);
    }
  }
  if (!key) return false;
  QVector<INPUT> input;
  auto add = [&](WORD code, DWORD flags) {
    INPUT item{};
    item.type = INPUT_KEYBOARD;
    item.ki.wVk = code;
    item.ki.dwFlags = flags;
    input << item;
  };
  for (WORD modifier : modifiers) add(modifier, 0);
  add(key, 0);
  add(key, KEYEVENTF_KEYUP);
  for (auto it = modifiers.crbegin(); it != modifiers.crend(); ++it)
    add(*it, KEYEVENTF_KEYUP);
  return SendInput(input.size(), input.data(), sizeof(INPUT)) == UINT(input.size());
#else
  Q_UNUSED(shortcut)
  return false;
#endif
}

void ActionExecutor::execute(quint16 actionId, quint32 eventId) {
  if (eventId <= lastEventId_) return;
  lastEventId_ = eventId;
  auto found = std::find_if(project_->actions().cbegin(), project_->actions().cend(),
                            [actionId](const HostAction &action) {
                              return action.id == actionId;
                            });
  if (found == project_->actions().cend()) return;
  const HostAction &action = *found;
  if (!action.allowWhenLocked && !workstationUnlocked()) return;
  bool success = true;
  switch (action.type) {
    case HostActionType::Shortcut:
      success = sendShortcut(action.value);
      break;
    case HostActionType::MediaKey: {
#ifdef Q_OS_WIN
      static const QHash<QString, WORD> keys = {
          {"playpause", VK_MEDIA_PLAY_PAUSE}, {"next", VK_MEDIA_NEXT_TRACK},
          {"previous", VK_MEDIA_PREV_TRACK}, {"stop", VK_MEDIA_STOP},
          {"volumeup", VK_VOLUME_UP}, {"volumedown", VK_VOLUME_DOWN},
          {"mute", VK_VOLUME_MUTE}};
      WORD key = keys.value(action.value.toLower(), 0);
      INPUT input[2]{};
      input[0].type = input[1].type = INPUT_KEYBOARD;
      input[0].ki.wVk = input[1].ki.wVk = key;
      input[1].ki.dwFlags = KEYEVENTF_KEYUP;
      success = key && SendInput(2, input, sizeof(INPUT)) == 2;
#else
      success = false;
#endif
      break;
    }
    case HostActionType::Launch:
      success = QProcess::startDetached(action.value, {});
      break;
    case HostActionType::Url:
      success = QDesktopServices::openUrl(QUrl::fromUserInput(action.value));
      break;
    case HostActionType::Command:
      success = QProcess::startDetached("cmd.exe", {"/d", "/s", "/c", action.value});
      break;
    case HostActionType::PowerShell:
      success = QProcess::startDetached(
          "powershell.exe", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                              action.value});
      break;
    case HostActionType::None:
      break;
  }
  if (!success)
    emit actionFailed(QStringLiteral("Не удалось выполнить действие «%1»").arg(action.name));
}
