#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTabWidget>
#include <QTimer>

#include "mainwindow.h"
#include "i18n.h"
#include "xptheme.h"

namespace {
void logMessage(QtMsgType type, const QMessageLogContext &, const QString &message) {
  QDir(QCoreApplication::applicationDirPath()).mkpath("portable-data/logs");
  QFile file(QCoreApplication::applicationDirPath() + "/portable-data/logs/monitor.log");
  if (file.size() > 2 * 1024 * 1024) file.rename(file.fileName() + ".1");
  if (file.open(QIODevice::Append | QIODevice::Text)) {
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODate) << " [" << int(type)
           << "] " << message << '\n';
  }
}
}  // namespace

int main(int argc, char *argv[]) {
  QApplication application(argc, argv);
  application.setApplicationName("Trezor PC Monitor");
  application.setOrganizationName("Trezor Community");
  application.setWindowIcon(QIcon(QStringLiteral(":/icons/pcmonitor.svg")));
  application.setQuitOnLastWindowClosed(false);
  qInstallMessageHandler(logMessage);
  I18n::initialize(application);
  QSettings settings(I18n::settingsPath(), QSettings::IniFormat);
  XpTheme::apply(application,
                 settings.value(QStringLiteral("ui/theme"),
                                QStringLiteral("Dark")).toString());

  const QStringList arguments = application.arguments();
  const int validateIndex = arguments.indexOf(QStringLiteral("--validate-firmware"));
  if (validateIndex >= 0) {
    if (validateIndex + 1 >= arguments.size()) return 2;
    QFile input(arguments.at(validateIndex + 1));
    if (!input.open(QIODevice::ReadOnly)) return 3;
    FirmwareUpdater updater;
    QString error;
    QString sha256;
    if (!updater.validate(input.readAll(), &error, &sha256)) {
      qCritical() << error;
      return 4;
    }
    qInfo() << "T1B1 TRZF SHA-256" << sha256;
    return 0;
  }

  const int flashIndex = arguments.indexOf(QStringLiteral("--flash-firmware"));
  if (flashIndex >= 0) {
    if (flashIndex + 1 >= arguments.size()) return 2;
    FirmwareUpdater updater;
    QObject::connect(&updater, &FirmwareUpdater::statusChanged,
                     [](const QString &status) { qInfo() << status; });
    QObject::connect(&updater, &FirmwareUpdater::progressChanged,
                     [](int progress) { qInfo() << "Firmware" << progress << '%'; });
    QObject::connect(&updater, &FirmwareUpdater::finished, &application,
                     [&application](bool success, const QString &message) {
                       if (success)
                         qInfo() << message;
                       else
                         qCritical() << message;
                       application.exit(success ? 0 : 5);
                     });
    updater.flash(arguments.at(flashIndex + 1));
    return application.exec();
  }

  const int compileIndex = arguments.indexOf(QStringLiteral("--compile-default"));
  if (compileIndex >= 0) {
    if (compileIndex + 1 >= arguments.size()) return 2;
    ProjectModel project;
    const PackCompileResult result = PackCompiler::compile(project);
    if (!result.ok()) {
      qCritical() << result.error;
      return 3;
    }
    QFile output(arguments.at(compileIndex + 1));
    if (!output.open(QIODevice::WriteOnly) || output.write(result.data) != result.data.size()) {
      qCritical() << output.errorString();
      return 4;
    }
    return 0;
  }

  if (arguments.contains(QStringLiteral("--reboot-device"))) {
    DeviceConnection device;
    bool requested = false;
    QObject::connect(&device, &DeviceConnection::statusChanged,
                     [](const QString &status) { qInfo() << status; });
    QObject::connect(&device, &DeviceConnection::connectedChanged, &application,
                     [&application, &device, &requested](bool connected) {
      if (connected && !requested) {
        requested = true;
        device.requestBootloader();
      } else if (!connected && requested) {
        application.exit(0);
      }
    });
    QTimer::singleShot(15000, &application, [&application] { application.exit(7); });
    device.start();
    const int result = application.exec();
    device.stop();
    device.wait(2000);
    return result;
  }

  QLockFile instanceLock(QStandardPaths::writableLocation(
                             QStandardPaths::TempLocation) +
                         QStringLiteral("/trezor-pc-monitor-ui.lock"));
  instanceLock.setStaleLockTime(0);
  if (!instanceLock.tryLock(100)) {
    QMessageBox::warning(
        nullptr, QStringLiteral("Trezor PC Monitor"),
        I18n::text(QStringLiteral(
            "Программа уже запущена. Закройте существующее окно или используйте меню в трее.")));
    return 1;
  }

  MainWindow window;
  QObject::connect(&window, &MainWindow::restartRequested, &application,
                   [&application, &instanceLock] {
    QStringList arguments = application.arguments();
    if (!arguments.isEmpty()) arguments.removeFirst();
    instanceLock.unlock();
    QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
    application.quit();
  });
  const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot-ui"));
  if (screenshotIndex >= 0) {
    if (screenshotIndex + 1 >= arguments.size()) return 2;
    window.show();
    if (QTabWidget *tabs = window.findChild<QTabWidget *>())
      tabs->setCurrentIndex(screenshotIndex + 2 < arguments.size()
                                ? arguments.at(screenshotIndex + 2).toInt()
                                : 0);
    const int delayMs = screenshotIndex + 3 < arguments.size()
                            ? qBound(250, arguments.at(screenshotIndex + 3).toInt(), 15000)
                            : 500;
    QTimer::singleShot(delayMs, &application, [&application, &window, arguments,
                                               screenshotIndex] {
      const bool ok = window.grab().save(arguments.at(screenshotIndex + 1), "PNG");
      application.exit(ok ? 0 : 6);
    });
    return application.exec();
  }
  if (!application.arguments().contains("--tray")) window.show();
  return application.exec();
}
