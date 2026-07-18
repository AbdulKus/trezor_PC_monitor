#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <QtMath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "designcanvas.h"
#include "iconeditor.h"
#include "pixelfont.h"
#include "resourceimporter.h"
#include "resourcepreview.h"
#include "xptheme.h"

namespace {
quint32 bindingCode(tm_action_type_t type, quint8 target = 0, quint16 id = 0) {
  return (quint32(type) << 24) | (quint32(target) << 16) | id;
}
ButtonBinding bindingFromCode(quint32 code) {
  return ButtonBinding{tm_action_type_t(code >> 24), quint8(code >> 16),
                       quint16(code)};
}
const QStringList kMetrics = {
    "cpu.total.load", "cpu.package.temperature", "cpu.package.power",
    "cpu.temperature.acpi", "cpu.frequency", "memory.ram.load", "memory.ram.used", "memory.ram.total",
    "memory.commit.used", "memory.commit.limit", "foreground.fps.displayed",
    "foreground.fps.presented", "foreground.fps.1low", "gpu.active.load",
    "gpu.active.temperature", "gpu.active.power", "gpu.active.frequency",
    "gpu.active.fan", "gpu.active.vram.used", "gpu.active.vram.total",
    "system.power.estimated"};

QString portableSettingsPath() {
  const QString directory = QCoreApplication::applicationDirPath() +
                            QStringLiteral("/portable-data");
  QDir().mkpath(directory);
  return directory + QStringLiteral("/settings.ini");
}

void rememberProjectPath(const QString &path) {
  if (path.isEmpty()) return;
  QSettings settings(portableSettingsPath(), QSettings::IniFormat);
  settings.setValue(QStringLiteral("project/lastPath"),
                    QFileInfo(path).absoluteFilePath());
  settings.sync();
}
}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), actions_(&project_, this) {
  setWindowTitle(QStringLiteral("Trezor PC Monitor Studio[*]"));
  setWindowIcon(QApplication::windowIcon());
  resize(1180, 760);
  createMenus();
  tabs_ = new QTabWidget;
  tabs_->addTab(createDeviceTab(), QStringLiteral("Устройство"));
  tabs_->addTab(createScreensTab(), QStringLiteral("Экраны"));
  tabs_->addTab(createSensorsTab(), QStringLiteral("Датчики"));
  tabs_->addTab(createResourcesTab(), QStringLiteral("Медиа"));
  tabs_->addTab(createButtonsTab(), QStringLiteral("Кнопки"));
  setCentralWidget(tabs_);
  createTray();

  connect(&telemetry_, &TelemetryManager::updated, this,
          [this](const QHash<QString, MetricSample> &samples) {
            canvas_->setSamples(samples);
            device_.updateMetrics(samples);
            sensorTable_->setRowCount(samples.size());
            int row = 0;
            QStringList names = samples.keys();
            names.sort();
            for (const QString &name : names) {
              const MetricSample sample = samples.value(name);
              sensorTable_->setItem(row, 0, new QTableWidgetItem(name));
              sensorTable_->setItem(
                  row, 1,
                  new QTableWidgetItem(sample.valid
                                           ? QString::number(sample.value, 'f', 2)
                                           : QStringLiteral("N/A")));
              sensorTable_->setItem(row, 2, new QTableWidgetItem(sample.unit));
              sensorTable_->setItem(
                  row, 3,
                  new QTableWidgetItem(!sample.valid ? "N/A"
                                                        : sample.estimated
                                                              ? "estimated"
                                                              : "live"));
              row++;
            }
          });
  connect(&device_, &DeviceConnection::statusChanged, this,
          [this](const QString &status) {
            deviceStatus_->setText(status);
            qInfo() << status;
          });
  connect(&device_, &DeviceConnection::connectedChanged, this, [this](bool connected) {
    deviceConnected_ = connected;
    if (connected) {
      actions_.resetEventSequence();
      uploadProject();
    }
  });
  connect(&device_, &DeviceConnection::uploadProgress, progress_, &QProgressBar::setValue);
  connect(&device_, &DeviceConnection::packUploaded, this,
          [this](bool success, const QString &message) {
            qInfo() << message;
            statusBar()->showMessage(message, 5000);
            if (!success) QMessageBox::warning(this, windowTitle(), message);
          });
  connect(&device_, &DeviceConnection::buttonEvent, &actions_, &ActionExecutor::execute);
  connect(&actions_, &ActionExecutor::actionFailed, this,
          [this](const QString &message) { tray_->showMessage(windowTitle(), message); });
  connect(&actions_, &ActionExecutor::actionCompleted, this,
          [this](const QString &message) { statusBar()->showMessage(message, 3000); });
  connect(&updater_, &FirmwareUpdater::statusChanged, deviceStatus_, &QLabel::setText);
  connect(&updater_, &FirmwareUpdater::progressChanged, progress_, &QProgressBar::setValue);
  connect(&updater_, &FirmwareUpdater::finished, this,
          [this](bool success, const QString &message) {
            QMessageBox::information(this, windowTitle(), message);
            if (success) QTimer::singleShot(1500, this, &MainWindow::uploadProject);
          });
  connect(&project_, &ProjectModel::modifiedChanged, this, [this](bool modified) {
    setWindowModified(modified);
  });
  presentMonStatus_->setText(telemetry_.presentMonStatus());
  QString startupProject;
  const QStringList arguments = QCoreApplication::arguments();
  for (const QString &argument : arguments) {
    if (argument.endsWith(QStringLiteral(".tmon"), Qt::CaseInsensitive) &&
        QFileInfo::exists(argument)) {
      startupProject = argument;
      break;
    }
  }
  if (startupProject.isEmpty() &&
      !arguments.contains(QStringLiteral("--no-restore"))) {
    QSettings settings(portableSettingsPath(), QSettings::IniFormat);
    startupProject = settings.value(QStringLiteral("project/lastPath")).toString();
  }
  if (!startupProject.isEmpty() && QFileInfo::exists(startupProject)) {
    QString error;
    if (project_.load(startupProject, &error)) {
      rememberProjectPath(project_.filePath());
      statusBar()->showMessage(
          QStringLiteral("Открыт последний проект: %1")
              .arg(QFileInfo(project_.filePath()).fileName()),
          5000);
    } else {
      qWarning() << "Unable to restore project" << startupProject << error;
    }
  }
  refreshScreens();
  refreshResources();
  refreshActions();
  telemetry_.start(250);
  device_.start();
}

MainWindow::~MainWindow() {
  device_.stop();
  device_.wait(2000);
}

void MainWindow::createMenus() {
  QMenu *file = menuBar()->addMenu(QStringLiteral("&Файл"));
  file->addAction(QStringLiteral("Новый"), this, [this] {
    if (confirmDiscard()) {
      project_.resetToDefault();
      refreshScreens();
    }
  }, QKeySequence::New);
  file->addAction(QStringLiteral("Открыть…"), this, &MainWindow::openProject,
                  QKeySequence::Open);
  file->addAction(QStringLiteral("Сохранить"), this,
                  [this] { saveProject(false); }, QKeySequence::Save);
  file->addAction(QStringLiteral("Сохранить как…"), this,
                  [this] { saveProject(true); }, QKeySequence::SaveAs);
  file->addSeparator();
  file->addAction(QStringLiteral("Выход"), this, [this] {
    quitting_ = true;
    if (close())
      QCoreApplication::quit();
    else
      quitting_ = false;
  });
  QMenu *view = menuBar()->addMenu(QStringLiteral("&Вид"));
  QMenu *themes = view->addMenu(QStringLiteral("Тема интерфейса"));
  for (const QString &theme : {"Dark", "Light", "Forest"})
    themes->addAction(theme, this, [theme] {
      XpTheme::apply(*qobject_cast<QApplication *>(qApp), theme);
    });
  QAction *autostart = view->addAction(QStringLiteral("Запускать вместе с Windows"));
  autostart->setCheckable(true);
  QSettings startup("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                    QSettings::NativeFormat);
  autostart->setChecked(startup.contains("TrezorPcMonitor"));
  connect(autostart, &QAction::toggled, this, [autostart](bool enabled) {
    QSettings settings(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat);
    if (enabled)
      settings.setValue("TrezorPcMonitor",
                        QStringLiteral("\"%1\" --tray")
                            .arg(QDir::toNativeSeparators(
                                QCoreApplication::applicationFilePath())));
    else
      settings.remove("TrezorPcMonitor");
  });
}

void MainWindow::createTray() {
  tray_ = new QSystemTrayIcon(QApplication::windowIcon(), this);
  QMenu *menu = new QMenu(this);
  menu->addAction(QStringLiteral("Открыть"), this, [this] {
    showNormal(); activateWindow();
  });
  menu->addAction(QStringLiteral("Загрузить макет"), this, &MainWindow::uploadProject);
  menu->addSeparator();
  menu->addAction(QStringLiteral("Выход"), this, [this] {
    quitting_ = true;
    if (close())
      QCoreApplication::quit();
    else
      quitting_ = false;
  });
  tray_->setContextMenu(menu);
  connect(tray_, &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger ||
                reason == QSystemTrayIcon::DoubleClick) {
              showNormal(); activateWindow();
            }
          });
  tray_->show();
}

QWidget *MainWindow::createDeviceTab() {
  QWidget *page = new QWidget;
  QVBoxLayout *layout = new QVBoxLayout(page);
  QGroupBox *device = new QGroupBox(QStringLiteral("Trezor One"));
  QFormLayout *form = new QFormLayout(device);
  deviceStatus_ = new QLabel(QStringLiteral("Поиск устройства…"));
  packStatus_ = new QLabel(QStringLiteral("Макет ещё не скомпилирован"));
  presentMonStatus_ = new QLabel;
  form->addRow(QStringLiteral("Состояние:"), deviceStatus_);
  form->addRow(QStringLiteral("Пакет:"), packStatus_);
  form->addRow(QStringLiteral("PresentMon:"), presentMonStatus_);
  layout->addWidget(device);
  progress_ = new QProgressBar;
  progress_->setRange(0, 100);
  layout->addWidget(progress_);
  QHBoxLayout *buttons = new QHBoxLayout;
  QPushButton *compile = new QPushButton(QStringLiteral("Проверить проект"));
  QPushButton *upload = new QPushButton(QStringLiteral("Загрузить на Trezor"));
  QPushButton *reboot = new QPushButton(QStringLiteral("В bootloader"));
  QPushButton *flash = new QPushButton(QStringLiteral("Установить firmware…"));
  QPushButton *presentMonSetup =
      new QPushButton(QStringLiteral("Настроить FPS / PresentMon…"));
  buttons->addWidget(compile); buttons->addWidget(upload); buttons->addWidget(reboot);
  buttons->addWidget(flash); buttons->addStretch();
  buttons->addWidget(presentMonSetup);
  layout->addLayout(buttons);
  connect(compile, &QPushButton::clicked, this, [this] { compileProject(true); });
  connect(upload, &QPushButton::clicked, this, &MainWindow::uploadProject);
  connect(reboot, &QPushButton::clicked, &device_, &DeviceConnection::requestBootloader);
  connect(flash, &QPushButton::clicked, this, &MainWindow::flashFirmware);
  connect(presentMonSetup, &QPushButton::clicked, this, [this] {
    const QString installer = QDir::toNativeSeparators(
        QCoreApplication::applicationDirPath() +
        "/setup/PresentMon-v2.5.1.msi");
    if (!QFileInfo::exists(installer)) {
      QMessageBox::warning(this, windowTitle(),
                           QStringLiteral("Установщик PresentMon не найден в portable-сборке."));
      return;
    }
    if (QMessageBox::information(
            this, windowTitle(),
            QStringLiteral("Windows запросит права администратора для установки "
                           "официального PresentMon service. После установки "
                           "перезапустите PC Monitor."),
            QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
      return;
#ifdef Q_OS_WIN
    const QString parameters = QStringLiteral("/i \"%1\"").arg(installer);
    const auto result = reinterpret_cast<qintptr>(ShellExecuteW(
        reinterpret_cast<HWND>(winId()), L"runas", L"msiexec.exe",
        reinterpret_cast<const wchar_t *>(parameters.utf16()), nullptr,
        SW_SHOWNORMAL));
    if (result <= 32)
      QMessageBox::warning(this, windowTitle(),
                           QStringLiteral("Не удалось запустить установщик PresentMon (код %1).")
                               .arg(result));
#else
    if (!QProcess::startDetached(QStringLiteral("msiexec"),
                                 {QStringLiteral("/i"), installer}))
      QMessageBox::warning(this, windowTitle(),
                           QStringLiteral("Не удалось запустить установщик PresentMon."));
#endif
  });
  QLabel *warning = new QLabel(QStringLiteral(
      "Firmware не является официальной кошельковой прошивкой. Установка может "
      "стереть storage Trezor; подтверждение всегда выполняется на устройстве."));
  warning->setWordWrap(true);
  warning->setStyleSheet(
      "color:#fef3c7;background:#3a2a10;border:1px solid #854d0e;"
      "border-radius:7px;font-weight:600;padding:10px;");
  layout->addWidget(warning);
  layout->addStretch();
  return page;
}

QWidget *MainWindow::createScreensTab() {
  QWidget *page = new QWidget;
  QHBoxLayout *layout = new QHBoxLayout(page);
  QSplitter *splitter = new QSplitter;
  QWidget *left = new QWidget;
  left->setMinimumWidth(245);
  left->setMaximumWidth(265);
  QVBoxLayout *leftLayout = new QVBoxLayout(left);
  screenList_ = new QListWidget;
  leftLayout->addWidget(new QLabel(QStringLiteral("Экраны")));
  leftLayout->addWidget(screenList_);
  screenList_->setMaximumHeight(150);
  QPushButton *addScreen = new QPushButton(QStringLiteral("+ Экран"));
  QPushButton *removeScreen = new QPushButton(QStringLiteral("Удалить экран"));
  QPushButton *moveScreenUp = new QPushButton(QStringLiteral("Выше"));
  QPushButton *moveScreenDown = new QPushButton(QStringLiteral("Ниже"));
  QHBoxLayout *screenButtons = new QHBoxLayout;
  screenButtons->addWidget(addScreen);
  screenButtons->addWidget(removeScreen);
  leftLayout->addLayout(screenButtons);
  QHBoxLayout *screenOrderButtons = new QHBoxLayout;
  screenOrderButtons->addWidget(moveScreenUp);
  screenOrderButtons->addWidget(moveScreenDown);
  leftLayout->addLayout(screenOrderButtons);

  leftLayout->addWidget(new QLabel(QStringLiteral("Слои текущего экрана")));
  widgetList_ = new QListWidget;
  widgetList_->setToolTip(QStringLiteral(
      "Порядок снизу вверх. Выберите элемент для настройки или удаления."));
  leftLayout->addWidget(widgetList_, 1);
  QPushButton *removeWidget = new QPushButton(QStringLiteral("Удалить элемент"));
  QPushButton *moveDown = new QPushButton(QStringLiteral("Ниже"));
  QPushButton *moveUp = new QPushButton(QStringLiteral("Выше"));
  QHBoxLayout *layerButtons = new QHBoxLayout;
  layerButtons->addWidget(removeWidget);
  layerButtons->addWidget(moveDown);
  layerButtons->addWidget(moveUp);
  leftLayout->addLayout(layerButtons);

  addWidgetType_ = new QComboBox;
  for (int type = TM_WIDGET_STATIC_TEXT; type <= TM_WIDGET_WARNING; type++)
    addWidgetType_->addItem(widgetTypeName(tm_widget_type_t(type)), type);
  QPushButton *add = new QPushButton(QStringLiteral("Добавить элемент"));
  leftLayout->addSpacing(8);
  leftLayout->addWidget(new QLabel(QStringLiteral("Новый элемент")));
  leftLayout->addWidget(addWidgetType_);
  leftLayout->addWidget(add);
  QCheckBox *pixelPreview = new QCheckBox(QStringLiteral("Точный пиксельный preview"));
  pixelPreview->setChecked(true);
  leftLayout->addWidget(pixelPreview);
  canvas_ = new DesignCanvas;
  canvas_->setProject(&project_);

  QGroupBox *properties = new QGroupBox(QStringLiteral("Свойства элемента"));
  properties->setMinimumWidth(285);
  properties->setMaximumWidth(305);
  propertyLayout_ = new QFormLayout(properties);
  propertyX_ = new QSpinBox; propertyX_->setRange(0, 127);
  propertyY_ = new QSpinBox; propertyY_->setRange(0, 63);
  propertyW_ = new QSpinBox; propertyW_->setRange(1, 128);
  propertyH_ = new QSpinBox; propertyH_->setRange(1, 64);
  propertyW_->setSuffix(QStringLiteral(" px"));
  propertyH_->setSuffix(QStringLiteral(" px"));
  propertyW_->setToolTip(QStringLiteral(
      "Для изображений и GIF это реальный размер на OLED."));
  propertyH_->setToolTip(propertyW_->toolTip());
  propertyText_ = new QLineEdit;
  propertyMetric_ = new QComboBox; propertyMetric_->setEditable(true);
  propertyMetric_->addItems(kMetrics);
  propertyFont_ = new QComboBox;
  propertyFont_->setEditable(true);
  propertyFont_->addItems(PixelFonts::families());
  for (const QString &family : QFontDatabase::families())
    if (propertyFont_->findText(family) < 0) propertyFont_->addItem(family);
  propertyFontSize_ = new QSpinBox; propertyFontSize_->setRange(5, 32);
  propertyResource_ = new QComboBox;
  propertyMin_ = new QSpinBox; propertyMin_->setRange(-100000000, 100000000);
  propertyMax_ = new QSpinBox; propertyMax_->setRange(-100000000, 100000000);
  propertyArg0_ = new QSpinBox; propertyArg0_->setRange(1, 32);
  propertyAutoRange_ = new QCheckBox(
      QStringLiteral("Автомасштаб максимума"));
  propertyAutoRange_->setToolTip(QStringLiteral(
      "Ноль остаётся снизу, а верхняя граница плавно следует за значениями с запасом."));
  suggestRange_ = new QPushButton(QStringLiteral("Подобрать по датчику"));
  propertyRangeHelp_ = new QLabel;
  propertyRangeHelp_->setWordWrap(true);
  propertyRangeHelp_->setStyleSheet(QStringLiteral("color:#94a3b8;font-size:12px;"));
  propertyLayout_->addRow(QStringLiteral("X:"), propertyX_);
  propertyLayout_->addRow(QStringLiteral("Y:"), propertyY_);
  propertyLayout_->addRow(QStringLiteral("Ширина:"), propertyW_);
  propertyLayout_->addRow(QStringLiteral("Высота:"), propertyH_);
  propertyLayout_->addRow(QStringLiteral("Текст / {v}:"), propertyText_);
  propertyLayout_->addRow(QStringLiteral("Метрика:"), propertyMetric_);
  propertyLayout_->addRow(QStringLiteral("Шрифт:"), propertyFont_);
  propertyLayout_->addRow(QStringLiteral("Размер, px:"), propertyFontSize_);
  propertyLayout_->addRow(QStringLiteral("Изображение / GIF:"), propertyResource_);
  propertyLayout_->addRow(QStringLiteral("Минимум:"), propertyMin_);
  propertyLayout_->addRow(QStringLiteral("Максимум:"), propertyMax_);
  propertyLayout_->addRow(QStringLiteral("Сегментов:"), propertyArg0_);
  propertyLayout_->addRow(QString(), propertyAutoRange_);
  propertyLayout_->addRow(QString(), suggestRange_);
  propertyLayout_->addRow(propertyRangeHelp_);
  splitter->addWidget(left); splitter->addWidget(canvas_); splitter->addWidget(properties);
  splitter->setChildrenCollapsible(false);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({255, 590, 295});
  layout->addWidget(splitter);

  connect(screenList_, &QListWidget::currentRowChanged, this, [this](int row) {
    project_.setCurrentScreen(row);
    const int selection = row >= 0 && row < project_.screens().size() &&
                                  !project_.screens()[row].widgets.isEmpty()
                              ? 0 : -1;
    canvas_->setSelectedWidget(selection);
    refreshWidgetList();
    refreshProperties(selection);
  });
  connect(screenList_, &QListWidget::itemChanged, this,
          [this](QListWidgetItem *item) {
            const int row = screenList_->row(item);
            if (row < 0 || row >= project_.screens().size()) return;
            const bool enabled = item->checkState() == Qt::Checked;
            if (!enabled) {
              int enabledCount = 0;
              for (const ScreenModel &screen : project_.screens())
                if (screen.enabled) ++enabledCount;
              if (enabledCount <= 1 && project_.screens()[row].enabled) {
                QSignalBlocker blocker(screenList_);
                item->setCheckState(Qt::Checked);
                statusBar()->showMessage(
                    QStringLiteral("Хотя бы один экран должен быть включён"),
                    3000);
                return;
              }
            }
            project_.screens()[row].enabled = enabled;
            item->setForeground(enabled ? palette().text().color()
                                        : QColor(QStringLiteral("#64748b")));
            project_.setModified();
            updateBindingControls();
          });
  connect(widgetList_, &QListWidget::currentRowChanged, this, [this](int row) {
    canvas_->setSelectedWidget(row);
    refreshProperties(row);
    if (row >= 0) canvas_->setFocus(Qt::OtherFocusReason);
  });
  connect(addScreen, &QPushButton::clicked, this, [this] {
    if (project_.screens().size() >= int(TM_MAX_SCREENS)) return;
    ScreenModel screen; screen.name = QStringLiteral("Экран %1").arg(project_.screens().size() + 1);
    project_.screens() << screen; project_.setModified(); refreshScreens();
    screenList_->setCurrentRow(project_.screens().size() - 1);
  });
  connect(removeScreen, &QPushButton::clicked, this, [this] {
    if (project_.screens().size() <= 1) return;
    const int removed = project_.currentScreen();
    project_.screens().removeAt(removed);
    auto repairBinding = [removed](ButtonBinding &binding) {
      if (binding.type != TM_ACTION_GO_TO_PAGE) return;
      if (binding.target == removed)
        binding = {};
      else if (binding.target > removed)
        --binding.target;
    };
    for (ScreenModel &screen : project_.screens()) {
      repairBinding(screen.leftShort); repairBinding(screen.leftLong);
      repairBinding(screen.rightShort); repairBinding(screen.rightLong);
    }
    project_.setCurrentScreen(0); project_.setModified(); refreshScreens();
  });
  auto moveScreen = [this](int direction) {
    const int from = project_.currentScreen();
    const int to = from + direction;
    if (from < 0 || to < 0 || to >= project_.screens().size()) return;
    project_.screens().swapItemsAt(from, to);
    auto remapBinding = [from, to](ButtonBinding &binding) {
      if (binding.type != TM_ACTION_GO_TO_PAGE) return;
      if (binding.target == from)
        binding.target = quint8(to);
      else if (binding.target == to)
        binding.target = quint8(from);
    };
    for (ScreenModel &screen : project_.screens()) {
      remapBinding(screen.leftShort); remapBinding(screen.leftLong);
      remapBinding(screen.rightShort); remapBinding(screen.rightLong);
    }
    project_.setCurrentScreen(to);
    project_.setModified();
    refreshScreens();
  };
  connect(moveScreenUp, &QPushButton::clicked, this,
          [moveScreen] { moveScreen(-1); });
  connect(moveScreenDown, &QPushButton::clicked, this,
          [moveScreen] { moveScreen(1); });
  connect(add, &QPushButton::clicked, this, &MainWindow::addWidget);
  connect(removeWidget, &QPushButton::clicked, this,
          &MainWindow::removeSelectedWidget);
  auto moveLayer = [this](int direction) {
    if (project_.currentScreen() >= project_.screens().size()) return;
    auto &widgets = project_.screens()[project_.currentScreen()].widgets;
    const int from = canvas_->selectedWidget();
    const int to = from + direction;
    if (from < 0 || to < 0 || to >= widgets.size()) return;
    widgets.swapItemsAt(from, to);
    project_.setModified();
    refreshWidgetList();
    widgetList_->setCurrentRow(to);
    canvas_->setSelectedWidget(to);
    canvas_->update();
  };
  connect(moveDown, &QPushButton::clicked, this,
          [moveLayer] { moveLayer(-1); });
  connect(moveUp, &QPushButton::clicked, this,
          [moveLayer] { moveLayer(1); });
  QAction *deleteElement = new QAction(QStringLiteral("Удалить элемент"), page);
  deleteElement->setShortcut(QKeySequence::Delete);
  page->addAction(deleteElement);
  connect(deleteElement, &QAction::triggered, this,
          &MainWindow::removeSelectedWidget);
  connect(pixelPreview, &QCheckBox::toggled, canvas_,
          &DesignCanvas::setPixelPerfect);
  connect(canvas_, &DesignCanvas::widgetSelected, this, [this](int index) {
    {
      QSignalBlocker blocker(widgetList_);
      widgetList_->setCurrentRow(index);
    }
    refreshProperties(index);
  });
  connect(canvas_, &DesignCanvas::widgetGeometryChanged, this,
          [this](int index, const QRect &) {
            project_.setModified();
            refreshProperties(index);
            refreshWidgetList();
          });
  auto updateProperty = [this] {
    int index = canvas_->selectedWidget();
    if (index < 0 || project_.currentScreen() >= project_.screens().size()) return;
    WidgetModel &widget = project_.screens()[project_.currentScreen()].widgets[index];
    widget.geometry = QRect(propertyX_->value(), propertyY_->value(), propertyW_->value(),
                            propertyH_->value());
    widget.text = propertyText_->text(); widget.metric = propertyMetric_->currentText();
    widget.minimum = propertyMin_->value(); widget.maximum = propertyMax_->value();
    widget.autoRange = propertyAutoRange_->isChecked();
    widget.resourceIndex = propertyResource_->currentData().toInt();
    widget.arg0 = propertyArg0_->value();
    widget.font.setFamily(propertyFont_->currentText());
    widget.font.setPixelSize(propertyFontSize_->value());
    project_.setModified();
    refreshWidgetList();
    canvas_->update();
  };
  for (QSpinBox *spin : {propertyX_, propertyY_, propertyW_, propertyH_,
                         propertyMin_, propertyMax_, propertyArg0_})
    connect(spin, &QSpinBox::valueChanged, this, [updateProperty](int) { updateProperty(); });
  connect(propertyText_, &QLineEdit::textChanged, this, [updateProperty](const QString &) { updateProperty(); });
  connect(propertyMetric_, &QComboBox::currentTextChanged, this, [updateProperty](const QString &) { updateProperty(); });
  connect(propertyResource_, &QComboBox::currentIndexChanged, this,
          [this, updateProperty](int) {
            const int resourceIndex = propertyResource_->currentData().toInt();
            const int widgetIndex = canvas_->selectedWidget();
            if (resourceIndex >= 0 && resourceIndex < project_.resources().size() &&
                widgetIndex >= 0 && project_.currentScreen() < project_.screens().size() &&
                !project_.resources()[resourceIndex].frames.isEmpty()) {
              const WidgetModel &widget = project_.screens()[project_.currentScreen()]
                                              .widgets[widgetIndex];
              if (widget.type == TM_WIDGET_IMAGE ||
                  widget.type == TM_WIDGET_ANIMATION ||
                  widget.type == TM_WIDGET_CONDITIONAL_ICON) {
                const QSize size = project_.resources()[resourceIndex]
                                       .frames.first().size().boundedTo(QSize(128, 64));
                QSignalBlocker bw(propertyW_), bh(propertyH_);
                propertyW_->setValue(size.width());
                propertyH_->setValue(size.height());
              }
            }
            updateProperty();
          });
  connect(propertyFont_, &QComboBox::currentTextChanged, this,
          [this, updateProperty](const QString &family) {
            const bool pixel = PixelFonts::isPixelFont(family);
            propertyFontSize_->setEnabled(!pixel);
            if (pixel) propertyFontSize_->setValue(PixelFonts::pixelHeight(family));
            updateProperty();
          });
  connect(propertyFontSize_, &QSpinBox::valueChanged, this,
          [updateProperty](int) { updateProperty(); });
  connect(propertyAutoRange_, &QCheckBox::toggled, this,
          [this, updateProperty](bool) {
            updateProperty();
            refreshProperties(canvas_->selectedWidget());
          });
  connect(propertyMetric_, &QComboBox::currentTextChanged, this,
          [this](const QString &metric) {
            const MetricSample sample = telemetry_.samples().value(metric);
            QString explanation = QStringLiteral(
                "Диапазон задаётся обычными единицами датчика. Например: 0–100 для загрузки, "
                "0–90 для температуры, 0–240 для FPS.");
            if (sample.valid)
              explanation += QStringLiteral(" Сейчас: %1 %2.")
                                     .arg(sample.value, 0, 'f', 1)
                                     .arg(sample.unit);
            if (metric == QStringLiteral("memory.ram.load"))
              explanation += QStringLiteral(
                  " Это процент занятой RAM, поэтому максимум 100. Для объёма выберите memory.ram.used.");
            propertyRangeHelp_->setText(explanation);
          });
  connect(suggestRange_, &QPushButton::clicked, this, [this] {
    const QString metric = propertyMetric_->currentText();
    int maximum = 100;
    if (metric.contains("fps")) maximum = 240;
    else if (metric.contains("temperature")) maximum = 100;
    else if (metric == "cpu.package.power") maximum = 250;
    else if (metric == "gpu.active.power") maximum = 500;
    else if (metric == "system.power.estimated") maximum = 750;
    else if (metric == "memory.ram.used" || metric == "memory.commit.used") {
      const QString totalMetric = metric == "memory.ram.used"
                                      ? "memory.ram.total" : "memory.commit.limit";
      const MetricSample total = telemetry_.samples().value(totalMetric);
      if (total.valid) maximum = qMax(1, int(qCeil(total.value)));
    } else if (metric.endsWith("vram.used")) {
      QString totalMetric = metric;
      totalMetric.chop(QStringLiteral("used").size());
      totalMetric += QStringLiteral("total");
      const MetricSample total = telemetry_.samples().value(totalMetric);
      if (total.valid) maximum = qMax(1, int(qCeil(total.value)));
    } else if (metric.contains("frequency")) maximum = 5000;
    else if (metric.contains("fan")) maximum = 5000;
    propertyMin_->setValue(0);
    propertyMax_->setValue(maximum);
  });
  return page;
}

QWidget *MainWindow::createSensorsTab() {
  QWidget *page = new QWidget;
  QVBoxLayout *layout = new QVBoxLayout(page);
  QLabel *note = new QLabel(QStringLiteral(
      "Windows-счётчики опрашиваются всегда. Vendor telemetry для GPU включается "
      "только после обнаружения активности; N/A никогда не заменяется нулём."));
  note->setWordWrap(true); layout->addWidget(note);
  sensorTable_ = new QTableWidget(0, 4);
  sensorTable_->setHorizontalHeaderLabels({"Канал", "Значение", "Единица", "Статус"});
  sensorTable_->horizontalHeader()->setStretchLastSection(true);
  layout->addWidget(sensorTable_);
  return page;
}

QWidget *MainWindow::createResourcesTab() {
  QWidget *page = new QWidget;
  QHBoxLayout *layout = new QHBoxLayout(page);
  QWidget *left = new QWidget; QVBoxLayout *buttons = new QVBoxLayout(left);
  buttons->addWidget(new QLabel(QStringLiteral("Изображения и GIF")));
  resourceList_ = new QListWidget;
  QPushButton *import = new QPushButton(QStringLiteral("Импорт PNG/GIF…"));
  QPushButton *create = new QPushButton(QStringLiteral("Новая пиктограмма"));
  QPushButton *addToScreen = new QPushButton(QStringLiteral("Добавить на текущий экран"));
  QPushButton *rename = new QPushButton(QStringLiteral("Переименовать"));
  QPushButton *remove = new QPushButton(QStringLiteral("Удалить ресурс"));
  QPushButton *erase = new QPushButton(QStringLiteral("Карандаш / ластик"));
  erase->setCheckable(true);
  QPushButton *clear = new QPushButton(QStringLiteral("Очистить"));
  QGroupBox *conversion = new QGroupBox(QStringLiteral("1-битное преобразование"));
  QFormLayout *conversionForm = new QFormLayout(conversion);
  QSpinBox *threshold = new QSpinBox;
  threshold->setRange(0, 255);
  threshold->setValue(128);
  QComboBox *dither = new QComboBox;
  dither->addItem(QStringLiteral("Без дизеринга"), QStringLiteral("none"));
  dither->addItem(QStringLiteral("Упорядоченный 4×4"), QStringLiteral("ordered"));
  dither->addItem(QStringLiteral("Floyd–Steinberg"), QStringLiteral("floyd"));
  dither->setCurrentIndex(1);
  QCheckBox *invert = new QCheckBox(QStringLiteral("Инвертировать при выводе"));
  QPushButton *autoPolarity = new QPushButton(QStringLiteral("Авто по фону"));
  conversionForm->addRow(QStringLiteral("Порог:"), threshold);
  conversionForm->addRow(QStringLiteral("Дизеринг:"), dither);
  conversionForm->addRow(invert);
  conversionForm->addRow(QString(), autoPolarity);
  resourceInfo_ = new QLabel(QStringLiteral("Выберите ресурс"));
  resourceInfo_->setWordWrap(true);
  resourceInfo_->setStyleSheet(QStringLiteral("color:#94a3b8;"));
  buttons->addWidget(resourceList_, 1);
  buttons->addWidget(resourceInfo_);
  buttons->addWidget(import); buttons->addWidget(create);
  buttons->addWidget(addToScreen);
  buttons->addWidget(rename);
  buttons->addWidget(remove);
  buttons->addWidget(conversion);
  buttons->addSpacing(10);
  QLabel *pixelControlsLabel = new QLabel(
      QStringLiteral("Пиксельный редактор одиночного изображения"));
  buttons->addWidget(pixelControlsLabel);
  buttons->addWidget(erase); buttons->addWidget(clear);
  QWidget *right = new QWidget;
  QVBoxLayout *rightLayout = new QVBoxLayout(right);
  resourcePreview_ = new ResourcePreview;
  iconEditor_ = new IconEditor;
  iconEditor_->setEnabled(false);
  erase->setEnabled(false);
  clear->setEnabled(false);
  conversion->setEnabled(false);
  rightLayout->addWidget(resourcePreview_, 1);
  QLabel *sourceEditorLabel = new QLabel(QStringLiteral(
      "Редактор исходных пикселей (для PNG/иконок)"));
  rightLayout->addWidget(sourceEditorLabel);
  rightLayout->addWidget(iconEditor_, 1);
  layout->addWidget(left);
  layout->addWidget(right, 1);
  connect(import, &QPushButton::clicked, this, &MainWindow::importResource);
  connect(addToScreen, &QPushButton::clicked, this,
          &MainWindow::addSelectedResourceToScreen);
  connect(remove, &QPushButton::clicked, this,
          &MainWindow::removeSelectedResource);
  connect(rename, &QPushButton::clicked, this, [this] {
    const int row = resourceList_->currentRow();
    if (row < 0 || row >= project_.resources().size()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, windowTitle(), QStringLiteral("Название ресурса:"),
        QLineEdit::Normal, project_.resources()[row].name, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    project_.resources()[row].name = name.trimmed();
    project_.setModified();
    refreshResources();
    resourceList_->setCurrentRow(row);
    refreshWidgetList();
  });
  connect(create, &QPushButton::clicked, this, [this] {
    ResourceModel resource; resource.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    resource.name = QStringLiteral("Пиктограмма %1").arg(project_.resources().size() + 1);
    QImage image(32, 32, QImage::Format_Mono); image.fill(1); resource.frames << image;
    resource.durationsMs << 100; project_.resources() << resource;
    project_.setModified(); refreshResources(); resourceList_->setCurrentRow(project_.resources().size() - 1);
  });
  connect(erase, &QPushButton::toggled, iconEditor_, &IconEditor::setErase);
  connect(clear, &QPushButton::clicked, iconEditor_, &IconEditor::clear);
  connect(threshold, &QSpinBox::valueChanged, this, [this](int value) {
    const int row = resourceList_->currentRow();
    if (row < 0 || row >= project_.resources().size()) return;
    project_.resources()[row].threshold = value;
    project_.setModified();
    resourcePreview_->setResource(project_.resources()[row]);
    canvas_->update();
  });
  connect(dither, &QComboBox::currentIndexChanged, this,
          [this, dither](int) {
            const int row = resourceList_->currentRow();
            if (row < 0 || row >= project_.resources().size()) return;
            project_.resources()[row].dither = dither->currentData().toString();
            project_.setModified();
            resourcePreview_->setResource(project_.resources()[row]);
            canvas_->update();
          });
  connect(invert, &QCheckBox::toggled, this, [this](bool enabled) {
    const int row = resourceList_->currentRow();
    if (row < 0 || row >= project_.resources().size()) return;
    project_.resources()[row].inverted = enabled;
    project_.setModified();
    resourcePreview_->setResource(project_.resources()[row]);
    canvas_->update();
  });
  connect(autoPolarity, &QPushButton::clicked, this, [this, invert] {
    const int row = resourceList_->currentRow();
    if (row < 0 || row >= project_.resources().size() ||
        project_.resources()[row].frames.isEmpty())
      return;
    invert->setChecked(ResourceImporter::suggestedInversion(
        project_.resources()[row].frames.first()));
  });
  connect(resourceList_, &QListWidget::currentRowChanged, this,
          [this, threshold, dither, invert, erase, clear,
           conversion, pixelControlsLabel, sourceEditorLabel](int row) {
    if (row >= 0 && row < project_.resources().size() &&
        !project_.resources()[row].frames.isEmpty()) {
      const ResourceModel &resource = project_.resources()[row];
      const bool animation = resource.frames.size() > 1;
      iconEditor_->setImage(project_.resources()[row].frames.first());
      resourcePreview_->setResource(resource);
      conversion->setEnabled(true);
      iconEditor_->setEnabled(!animation);
      iconEditor_->setVisible(!animation);
      sourceEditorLabel->setVisible(!animation);
      pixelControlsLabel->setVisible(!animation);
      erase->setVisible(!animation);
      clear->setVisible(!animation);
      erase->setEnabled(!animation);
      clear->setEnabled(!animation);
      {
        QSignalBlocker bt(threshold), bd(dither), bi(invert);
        threshold->setValue(resource.threshold);
        dither->setCurrentIndex(
            qMax(0, dither->findData(resource.dither)));
        invert->setChecked(resource.inverted);
      }
      int duration = 0;
      for (int value : resource.durationsMs) duration += value;
      resourceInfo_->setText(
          QStringLiteral("%1 × %2 px · %3\n%4")
              .arg(resource.frames.first().width())
              .arg(resource.frames.first().height())
              .arg(resource.frames.size() > 1
                       ? QStringLiteral("GIF: %1 кадров").arg(resource.frames.size())
                       : QStringLiteral("1 кадр"))
              .arg(resource.frames.size() > 1
                       ? QStringLiteral("Цикл %1 мс · на устройстве до 12 FPS")
                             .arg(duration)
                       : QStringLiteral("Статическое изображение"))
          + QStringLiteral("\nПорог %1 · %2%3")
                .arg(resource.threshold)
                .arg(dither->currentText())
                .arg(resource.inverted ? QStringLiteral(" · инверсия")
                                       : QString())
          + QStringLiteral(
              "\nРазмер на OLED меняется у элемента во вкладке «Экраны». "
              "В пакет каждый вариант попадает сразу в итоговом W×H."));
    } else {
      resourceInfo_->setText(QStringLiteral("Выберите ресурс"));
      resourcePreview_->clearResource();
      conversion->setEnabled(false);
      iconEditor_->setEnabled(false);
      iconEditor_->setVisible(true);
      sourceEditorLabel->setVisible(true);
      pixelControlsLabel->setVisible(true);
      erase->setVisible(true);
      clear->setVisible(true);
      erase->setEnabled(false);
      clear->setEnabled(false);
    }
  });
  connect(iconEditor_, &IconEditor::imageChanged, this, [this](const QImage &image) {
    int row = resourceList_->currentRow();
    if (row >= 0 && row < project_.resources().size()) {
      project_.resources()[row].frames[0] = image;
      project_.setModified();
      resourcePreview_->setResource(project_.resources()[row]);
      canvas_->update();
    }
  });
  return page;
}

QWidget *MainWindow::createButtonsTab() {
  QWidget *page = new QWidget; QHBoxLayout *layout = new QHBoxLayout(page);
  QGroupBox *bindings = new QGroupBox(QStringLiteral("Действия текущего экрана"));
  QFormLayout *form = new QFormLayout(bindings);
  const QStringList labels = {"Левая — нажатие", "Левая — удержание",
                              "Правая — нажатие", "Правая — удержание"};
  for (int i = 0; i < 4; i++) {
    bindingCombos_[i] = new QComboBox; form->addRow(labels[i], bindingCombos_[i]);
    connect(bindingCombos_[i], &QComboBox::currentIndexChanged, this, [this, i](int) {
      if (project_.currentScreen() >= project_.screens().size()) return;
      ButtonBinding value = bindingFromCode(bindingCombos_[i]->currentData().toUInt());
      ScreenModel &screen = project_.screens()[project_.currentScreen()];
      ButtonBinding *bindings[] = {&screen.leftShort, &screen.leftLong,
                                   &screen.rightShort, &screen.rightLong};
      *bindings[i] = value; project_.setModified();
    });
  }
  QWidget *actions = new QWidget; QVBoxLayout *actionLayout = new QVBoxLayout(actions);
  actionLayout->addWidget(new QLabel(QStringLiteral("Действия ПК")));
  QLabel *shortcutHelp = new QLabel(QStringLiteral(
      "Сочетания записываются как CTRL+C, ALT+F4, SHIFT+F10. "
      "После создания назначьте действие одной из четырёх кнопок слева."));
  shortcutHelp->setWordWrap(true);
  actionLayout->addWidget(shortcutHelp);
  actionList_ = new QListWidget; actionLayout->addWidget(actionList_);
  QPushButton *add = new QPushButton(QStringLiteral("+ Действие"));
  QPushButton *test = new QPushButton(QStringLiteral("Проверить выбранное"));
  QPushButton *remove = new QPushButton(QStringLiteral("Удалить"));
  actionLayout->addWidget(add); actionLayout->addWidget(test);
  actionLayout->addWidget(remove);
  layout->addWidget(bindings); layout->addWidget(actions, 1);
  connect(add, &QPushButton::clicked, this, [this] {
    bool ok = false;
    QString name = QInputDialog::getText(this, windowTitle(), "Название:", QLineEdit::Normal, {}, &ok);
    if (!ok || name.isEmpty()) return;
    QStringList types = {"Сочетание клавиш", "Media key", "Запуск приложения", "URL", "Команда", "PowerShell"};
    QString type = QInputDialog::getItem(this, windowTitle(), "Тип:", types, 0, false, &ok);
    if (!ok) return;
    QString value = QInputDialog::getText(this, windowTitle(), "Значение:", QLineEdit::Normal, {}, &ok);
    if (!ok) return;
    HostActionType actionType = HostActionType(types.indexOf(type) + 1);
    if ((actionType == HostActionType::Command || actionType == HostActionType::PowerShell) &&
        QMessageBox::warning(this, windowTitle(),
                             "Команда будет выполняться от имени текущего пользователя по нажатию физической кнопки.",
                             QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
      return;
    quint16 id = 1; for (const HostAction &existing : project_.actions()) id = qMax<quint16>(id, existing.id + 1);
    project_.actions() << HostAction{id, name, actionType, value, false};
    project_.setModified(); refreshActions();
  });
  connect(remove, &QPushButton::clicked, this, [this] {
    int row = actionList_->currentRow(); if (row < 0) return;
    project_.actions().removeAt(row); project_.setModified(); refreshActions();
  });
  connect(test, &QPushButton::clicked, this, [this] {
    const int row = actionList_->currentRow();
    if (row < 0 || row >= project_.actions().size()) {
      statusBar()->showMessage(QStringLiteral("Сначала выберите действие"), 3000);
      return;
    }
    actions_.test(project_.actions()[row].id);
  });
  connect(&project_, &ProjectModel::currentScreenChanged, this,
          [this](int) { updateBindingControls(); });
  return page;
}

void MainWindow::refreshScreens() {
  QSignalBlocker blocker(screenList_); screenList_->clear();
  for (const ScreenModel &screen : project_.screens()) {
    QListWidgetItem *item = new QListWidgetItem(screen.name, screenList_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(screen.enabled ? Qt::Checked : Qt::Unchecked);
    item->setForeground(screen.enabled ? palette().text().color()
                                       : QColor(QStringLiteral("#64748b")));
    item->setToolTip(screen.enabled
                         ? QStringLiteral("Экран включён и попадёт на устройство")
                         : QStringLiteral("Экран сохранён в проекте, но не компилируется"));
  }
  screenList_->setCurrentRow(project_.currentScreen());
  const int selection = project_.currentScreen() < project_.screens().size() &&
                                !project_.screens()[project_.currentScreen()].widgets.isEmpty()
                            ? 0 : -1;
  canvas_->setSelectedWidget(selection);
  refreshWidgetList();
  refreshProperties(selection);
  canvas_->update();
  updateBindingControls();
}

void MainWindow::refreshWidgetList() {
  if (!widgetList_) return;
  const int selected = canvas_ ? canvas_->selectedWidget() : -1;
  QSignalBlocker blocker(widgetList_);
  widgetList_->clear();
  if (project_.currentScreen() < project_.screens().size()) {
    const auto &widgets = project_.screens()[project_.currentScreen()].widgets;
    for (int i = 0; i < widgets.size(); ++i) {
      const WidgetModel &widget = widgets[i];
      QString detail;
      if (widget.type == TM_WIDGET_STATIC_TEXT ||
          widget.type == TM_WIDGET_DYNAMIC_TEXT ||
          widget.type == TM_WIDGET_WARNING)
        detail = widget.text;
      else if (widget.type == TM_WIDGET_IMAGE ||
               widget.type == TM_WIDGET_ANIMATION ||
               widget.type == TM_WIDGET_CONDITIONAL_ICON) {
        if (widget.resourceIndex >= 0 &&
            widget.resourceIndex < project_.resources().size())
          detail = project_.resources()[widget.resourceIndex].name;
      } else if (!widget.metric.isEmpty()) {
        detail = widget.metric;
      }
      widgetList_->addItem(QStringLiteral("%1. %2%3")
                               .arg(i + 1)
                               .arg(widgetTypeName(widget.type))
                               .arg(detail.isEmpty()
                                        ? QString()
                                        : QStringLiteral(" — %1").arg(detail)));
    }
  }
  widgetList_->setCurrentRow(selected);
}

void MainWindow::refreshResourceChoices() {
  if (!propertyResource_) return;
  int current = -1;
  tm_widget_type_t type = TM_WIDGET_IMAGE;
  const int selected = canvas_ ? canvas_->selectedWidget() : -1;
  if (project_.currentScreen() < project_.screens().size() && selected >= 0 &&
      selected < project_.screens()[project_.currentScreen()].widgets.size()) {
    const WidgetModel &widget =
        project_.screens()[project_.currentScreen()].widgets[selected];
    current = widget.resourceIndex;
    type = widget.type;
  }
  QSignalBlocker blocker(propertyResource_);
  propertyResource_->clear();
  propertyResource_->addItem(QStringLiteral("Не выбрано"), -1);
  for (int i = 0; i < project_.resources().size(); ++i) {
    const ResourceModel &resource = project_.resources()[i];
    const bool animation = resource.frames.size() > 1;
    const bool compatible = type == TM_WIDGET_ANIMATION
                                ? animation
                                : type == TM_WIDGET_IMAGE ||
                                          type == TM_WIDGET_CONDITIONAL_ICON
                                      ? !animation
                                      : true;
    if (compatible)
      propertyResource_->addItem(
          QStringLiteral("%1 (%2)")
              .arg(resource.name,
                   animation ? QStringLiteral("GIF, %1 кадров")
                                   .arg(resource.frames.size())
                             : QStringLiteral("изображение")),
          i);
  }
  const int comboIndex = propertyResource_->findData(current);
  propertyResource_->setCurrentIndex(qMax(0, comboIndex));
}

void MainWindow::refreshProperties(int index) {
  const bool valid = index >= 0 &&
                     project_.currentScreen() < project_.screens().size() &&
                     index < project_.screens()[project_.currentScreen()].widgets.size();
  if (!valid) {
    if (propertyLayout_) {
      for (QWidget *field : QList<QWidget *>{propertyX_, propertyY_, propertyW_,
                                             propertyH_, propertyText_, propertyMetric_,
                                             propertyFont_, propertyFontSize_,
                                             propertyResource_, propertyMin_, propertyMax_,
                                             propertyArg0_, propertyAutoRange_,
                                             suggestRange_,
                                             propertyRangeHelp_}) {
        if (!field) continue;
        field->setVisible(false);
        if (QWidget *label = propertyLayout_->labelForField(field))
          label->setVisible(false);
      }
    }
    return;
  }
  const WidgetModel &widget = project_.screens()[project_.currentScreen()].widgets[index];
  QSignalBlocker bx(propertyX_), by(propertyY_), bw(propertyW_), bh(propertyH_),
      bt(propertyText_), bm(propertyMetric_), bf(propertyFont_),
      bfs(propertyFontSize_), br(propertyResource_), bmin(propertyMin_),
      bmax(propertyMax_), barg(propertyArg0_),
      bautorange(propertyAutoRange_);
  propertyX_->setValue(widget.geometry.x()); propertyY_->setValue(widget.geometry.y());
  propertyW_->setValue(widget.geometry.width()); propertyH_->setValue(widget.geometry.height());
  propertyText_->setText(widget.text); propertyMetric_->setCurrentText(widget.metric);
  propertyFont_->setCurrentText(widget.font.family());
  propertyFontSize_->setValue(PixelFonts::isPixelFont(widget.font.family())
                                  ? PixelFonts::pixelHeight(widget.font.family())
                                  : qMax(5, widget.font.pixelSize()));
  propertyFontSize_->setEnabled(!PixelFonts::isPixelFont(widget.font.family()));
  propertyMin_->setValue(widget.minimum); propertyMax_->setValue(widget.maximum);
  propertyAutoRange_->setChecked(widget.autoRange);
  propertyArg0_->setValue(widget.arg0 ? widget.arg0 : 5);
  refreshResourceChoices();

  const bool text = widget.type == TM_WIDGET_STATIC_TEXT ||
                    widget.type == TM_WIDGET_DYNAMIC_TEXT ||
                    widget.type == TM_WIDGET_WARNING;
  const bool metric = widget.type == TM_WIDGET_DYNAMIC_TEXT ||
                      widget.type == TM_WIDGET_BAR_HORIZONTAL ||
                      widget.type == TM_WIDGET_BAR_VERTICAL ||
                      widget.type == TM_WIDGET_SEGMENTS ||
                      widget.type == TM_WIDGET_NEEDLE ||
                      widget.type == TM_WIDGET_RING ||
                      widget.type == TM_WIDGET_SPARKLINE ||
                      widget.type == TM_WIDGET_CONDITIONAL_ICON ||
                      widget.type == TM_WIDGET_WARNING;
  const bool range = metric && widget.type != TM_WIDGET_DYNAMIC_TEXT;
  const bool resource = widget.type == TM_WIDGET_IMAGE ||
                        widget.type == TM_WIDGET_ANIMATION ||
                        widget.type == TM_WIDGET_CONDITIONAL_ICON;
  auto showRow = [this](QWidget *field, bool show) {
    field->setVisible(show);
    if (QWidget *label = propertyLayout_->labelForField(field))
      label->setVisible(show);
  };
  showRow(propertyX_, true);
  showRow(propertyY_, true);
  showRow(propertyW_, true);
  showRow(propertyH_, true);
  showRow(propertyText_, text);
  showRow(propertyMetric_, metric);
  showRow(propertyFont_, text);
  showRow(propertyFontSize_, text);
  showRow(propertyResource_, resource);
  showRow(propertyMin_, range && !widget.autoRange);
  showRow(propertyMax_, range && !widget.autoRange);
  showRow(propertyArg0_, widget.type == TM_WIDGET_SEGMENTS);
  showRow(propertyAutoRange_, widget.type == TM_WIDGET_SPARKLINE);
  showRow(suggestRange_, range && !widget.autoRange);
  showRow(propertyRangeHelp_, range && !widget.autoRange);
  const MetricSample sample = telemetry_.samples().value(widget.metric);
  propertyRangeHelp_->setText(QStringLiteral(
      "Диапазон — в обычных единицах датчика%1. Для memory.ram.load используйте 0–100; "
      "для memory.ram.used кнопка подставит полный объём RAM.")
      .arg(sample.valid ? QStringLiteral(" (%1)").arg(sample.unit) : QString()));
}

void MainWindow::addWidget() {
  if (project_.currentScreen() >= project_.screens().size()) return;
  auto &widgets = project_.screens()[project_.currentScreen()].widgets;
  if (widgets.size() >= int(TM_MAX_WIDGETS_PER_SCREEN)) return;
  WidgetModel widget; widget.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  widget.type = tm_widget_type_t(addWidgetType_->currentData().toInt());
  widget.geometry = QRect(4, 4, 48, widget.type == TM_WIDGET_STATIC_TEXT ||
                                      widget.type == TM_WIDGET_DYNAMIC_TEXT ? 10 : 18);
  if (widget.type == TM_WIDGET_STATIC_TEXT) widget.text = "TEXT";
  if (widget.type == TM_WIDGET_FRAME) widget.geometry = QRect(1, 1, 126, 62);
  widgets << widget; project_.setModified(); canvas_->setSelectedWidget(widgets.size() - 1);
  refreshWidgetList();
  widgetList_->setCurrentRow(widgets.size() - 1);
  refreshProperties(widgets.size() - 1); canvas_->update();
}

void MainWindow::removeSelectedWidget() {
  if (project_.currentScreen() >= project_.screens().size()) return;
  auto &widgets = project_.screens()[project_.currentScreen()].widgets;
  const int index = canvas_->selectedWidget();
  if (index < 0 || index >= widgets.size()) return;
  widgets.removeAt(index);
  const int next = widgets.isEmpty() ? -1 : qMin(index, widgets.size() - 1);
  project_.setModified();
  canvas_->setSelectedWidget(next);
  refreshWidgetList();
  refreshProperties(next);
  canvas_->update();
}

void MainWindow::refreshResources() {
  if (!resourceList_) return;
  const int oldRow = resourceList_->currentRow();
  resourceList_->clear();
  for (const ResourceModel &resource : project_.resources())
    resourceList_->addItem(
        resource.frames.size() > 1
            ? QStringLiteral("🎞  %1  ·  %2 кадров")
                  .arg(resource.name)
                  .arg(resource.frames.size())
            : QStringLiteral("▣  %1").arg(resource.name));
  if (!project_.resources().isEmpty())
    resourceList_->setCurrentRow(qBound(0, oldRow,
                                        project_.resources().size() - 1));
  refreshResourceChoices();
}

void MainWindow::importResource() {
  QString path = QFileDialog::getOpenFileName(this, "Импорт ресурса", {}, "Images (*.png *.bmp *.jpg *.jpeg *.gif)");
  if (path.isEmpty()) return;
  ResourceModel resource;
  QString error;
  if (!ResourceImporter::importFile(path, &resource, &error)) {
    QMessageBox::warning(this, windowTitle(), error);
    return;
  }
  project_.resources() << resource; project_.setModified(); refreshResources();
  resourceList_->setCurrentRow(project_.resources().size() - 1);
}

void MainWindow::addSelectedResourceToScreen() {
  const int resourceIndex = resourceList_ ? resourceList_->currentRow() : -1;
  if (resourceIndex < 0 || resourceIndex >= project_.resources().size() ||
      project_.currentScreen() >= project_.screens().size())
    return;
  auto &widgets = project_.screens()[project_.currentScreen()].widgets;
  if (widgets.size() >= int(TM_MAX_WIDGETS_PER_SCREEN)) {
    QMessageBox::warning(this, windowTitle(),
                         QStringLiteral("На экране уже 24 элемента."));
    return;
  }
  const ResourceModel &resource = project_.resources()[resourceIndex];
  if (resource.frames.isEmpty()) return;
  WidgetModel widget;
  widget.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  widget.type = resource.frames.size() > 1 ? TM_WIDGET_ANIMATION
                                           : TM_WIDGET_IMAGE;
  widget.resourceIndex = resourceIndex;
  const QSize size = resource.frames.first().size().boundedTo(QSize(128, 64));
  widget.geometry = QRect((128 - size.width()) / 2,
                          (64 - size.height()) / 2, size.width(), size.height());
  widgets << widget;
  project_.setModified();
  const int index = widgets.size() - 1;
  canvas_->setSelectedWidget(index);
  refreshWidgetList();
  refreshProperties(index);
  canvas_->update();
  if (tabs_) tabs_->setCurrentIndex(1);
  widgetList_->setCurrentRow(index);
  statusBar()->showMessage(QStringLiteral("Ресурс добавлен на текущий экран"),
                           3000);
}

void MainWindow::removeSelectedResource() {
  const int row = resourceList_ ? resourceList_->currentRow() : -1;
  if (row < 0 || row >= project_.resources().size()) return;
  int references = 0;
  for (const ScreenModel &screen : project_.screens())
    for (const WidgetModel &widget : screen.widgets)
      if (widget.resourceIndex == row) ++references;
  const QString question = references > 0
      ? QStringLiteral("Ресурс используется элементами: %1. Удалить ресурс и "
                       "очистить эти привязки?").arg(references)
      : QStringLiteral("Удалить ресурс «%1»?")
            .arg(project_.resources()[row].name);
  if (QMessageBox::question(this, windowTitle(), question) != QMessageBox::Yes)
    return;
  project_.resources().removeAt(row);
  for (ScreenModel &screen : project_.screens()) {
    for (WidgetModel &widget : screen.widgets) {
      if (widget.resourceIndex == row)
        widget.resourceIndex = -1;
      else if (widget.resourceIndex > row)
        --widget.resourceIndex;
    }
  }
  project_.setModified();
  refreshResources();
  refreshWidgetList();
  refreshProperties(canvas_->selectedWidget());
  canvas_->update();
}

void MainWindow::refreshActions() {
  if (!actionList_) return;
  actionList_->clear();
  for (const HostAction &action : project_.actions())
    actionList_->addItem(QStringLiteral("%1 — %2").arg(action.name, actionTypeName(action.type)));
  updateBindingControls();
}

void MainWindow::updateBindingControls() {
  if (!bindingCombos_[0] || project_.currentScreen() >= project_.screens().size()) return;
  const ScreenModel &screen = project_.screens()[project_.currentScreen()];
  const ButtonBinding values[] = {screen.leftShort, screen.leftLong, screen.rightShort, screen.rightLong};
  for (int i = 0; i < 4; i++) {
    QSignalBlocker blocker(bindingCombos_[i]); bindingCombos_[i]->clear();
    bindingCombos_[i]->addItem("Нет", bindingCode(TM_ACTION_NONE));
    bindingCombos_[i]->addItem("Предыдущий экран", bindingCode(TM_ACTION_PREVIOUS_PAGE));
    bindingCombos_[i]->addItem("Следующий экран", bindingCode(TM_ACTION_NEXT_PAGE));
    for (int page = 0; page < project_.screens().size(); page++) {
      if (!project_.screens()[page].enabled) continue;
      bindingCombos_[i]->addItem(QStringLiteral("Перейти: %1").arg(project_.screens()[page].name),
                                 bindingCode(TM_ACTION_GO_TO_PAGE, page));
    }
    for (const HostAction &action : project_.actions())
      bindingCombos_[i]->addItem(QStringLiteral("ПК: %1").arg(action.name),
                                 bindingCode(TM_ACTION_HOST, 0, action.id));
    quint32 current = bindingCode(values[i].type, values[i].target, values[i].hostActionId);
    int index = bindingCombos_[i]->findData(current); bindingCombos_[i]->setCurrentIndex(qMax(0, index));
  }
}

PackCompileResult MainWindow::compileProject(bool showErrors) {
  lastPack_ = PackCompiler::compile(project_);
  if (!lastPack_.ok()) {
    packStatus_->setText(lastPack_.error);
    if (showErrors) QMessageBox::critical(this, windowTitle(), lastPack_.error);
  } else {
    packStatus_->setText(QStringLiteral("%1 / %2 байт, %3 каналов")
                             .arg(lastPack_.data.size()).arg(TM_MAX_PACK_SIZE)
                             .arg(lastPack_.channels.size()));
  }
  return lastPack_;
}
void MainWindow::uploadProject() {
  PackCompileResult pack = compileProject(true);
  if (pack.ok()) device_.uploadPack(pack.data, pack.channels);
}

bool MainWindow::confirmDiscard() {
  return !project_.modified() ||
         QMessageBox::question(this, windowTitle(), "Отбросить несохранённые изменения?") == QMessageBox::Yes;
}
void MainWindow::openProject() {
  if (!confirmDiscard()) return;
  QString path = QFileDialog::getOpenFileName(this, "Открыть проект", {}, "Trezor Monitor (*.tmon)");
  if (path.isEmpty()) return; QString error;
  if (!project_.load(path, &error)) QMessageBox::critical(this, windowTitle(), error);
  else {
    rememberProjectPath(project_.filePath());
    refreshScreens(); refreshResources(); refreshActions();
  }
}
void MainWindow::saveProject(bool saveAs) {
  QString path = project_.filePath();
  if (saveAs || path.isEmpty()) path = QFileDialog::getSaveFileName(this, "Сохранить проект", path, "Trezor Monitor (*.tmon)");
  if (path.isEmpty()) return; if (!path.endsWith(".tmon", Qt::CaseInsensitive)) path += ".tmon";
  QString error;
  if (!project_.save(path, &error))
    QMessageBox::critical(this, windowTitle(), error);
  else
    rememberProjectPath(project_.filePath());
}

void MainWindow::flashFirmware() {
  QString candidate = QCoreApplication::applicationDirPath() + "/firmware/pcmonitor-inner.bin";
  if (!QFileInfo::exists(candidate))
    candidate = QFileDialog::getOpenFileName(this, "Выберите внутренний TRZF", {}, "Firmware (*.bin)");
  if (candidate.isEmpty()) return;
  QFile file(candidate); file.open(QIODevice::ReadOnly); QByteArray bytes = file.readAll();
  QString error, sha; if (!updater_.validate(bytes, &error, &sha)) {
    QMessageBox::critical(this, windowTitle(), error); return;
  }
  QString warning = QStringLiteral(
      "Будет установлена неофициальная firmware. Storage кошелька может быть стёрт.\n\n"
      "SHA-256: %1\n\nПродолжить?").arg(sha);
  if (QMessageBox::warning(this, windowTitle(), warning,
                           QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
  if (deviceConnected_) {
    device_.requestBootloader();
    QTimer::singleShot(1500, this, [this, candidate] { updater_.flash(candidate); });
  } else {
    QMessageBox::information(this, windowTitle(),
                             "Подключите Trezor с зажатыми кнопками в режиме bootloader и нажмите OK.");
    updater_.flash(candidate);
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (!quitting_) {
    hide(); tray_->showMessage(windowTitle(), "Программа продолжает работать в трее.");
    event->ignore(); return;
  }
  if (!confirmDiscard()) {
    quitting_ = false;
    event->ignore();
    return;
  }
  tray_->hide();
  event->accept();
}
