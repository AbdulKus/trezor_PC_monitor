#pragma once

#include <QMainWindow>

#include "actionexecutor.h"
#include "deviceconnection.h"
#include "firmwareupdater.h"
#include "packcompiler.h"
#include "projectmodel.h"
#include "telemetry.h"

class DesignCanvas;
class IconEditor;
class ResourcePreview;
class QLabel;
class QListWidget;
class QProgressBar;
class QSpinBox;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QTableWidget;
class QSystemTrayIcon;
class QFormLayout;
class QPushButton;
class QTabWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;
 protected:
  void closeEvent(QCloseEvent *event) override;
 private:
  QWidget *createDeviceTab();
  QWidget *createScreensTab();
  QWidget *createSensorsTab();
  QWidget *createResourcesTab();
  QWidget *createButtonsTab();
  void createMenus();
  void createTray();
  void refreshScreens();
  void refreshWidgetList();
  void refreshProperties(int index);
  void refreshResources();
  void refreshResourceChoices();
  void refreshActions();
  void updateBindingControls();
  PackCompileResult compileProject(bool showErrors = true);
  void uploadProject();
  void openProject();
  void saveProject(bool saveAs = false);
  void addWidget();
  void removeSelectedWidget();
  void addSelectedResourceToScreen();
  void removeSelectedResource();
  void importResource();
  void flashFirmware();
  bool confirmDiscard();

  ProjectModel project_;
  TelemetryManager telemetry_;
  DeviceConnection device_;
  FirmwareUpdater updater_;
  ActionExecutor actions_;
  PackCompileResult lastPack_;

  DesignCanvas *canvas_ = nullptr;
  QTabWidget *tabs_ = nullptr;
  QListWidget *screenList_ = nullptr;
  QListWidget *widgetList_ = nullptr;
  QComboBox *addWidgetType_ = nullptr;
  QFormLayout *propertyLayout_ = nullptr;
  QSpinBox *propertyX_ = nullptr;
  QSpinBox *propertyY_ = nullptr;
  QSpinBox *propertyW_ = nullptr;
  QSpinBox *propertyH_ = nullptr;
  QLineEdit *propertyText_ = nullptr;
  QComboBox *propertyMetric_ = nullptr;
  QComboBox *propertyFont_ = nullptr;
  QComboBox *propertyResource_ = nullptr;
  QSpinBox *propertyFontSize_ = nullptr;
  QSpinBox *propertyMin_ = nullptr;
  QSpinBox *propertyMax_ = nullptr;
  QSpinBox *propertyArg0_ = nullptr;
  QCheckBox *propertyAutoRange_ = nullptr;
  QPushButton *suggestRange_ = nullptr;
  QLabel *propertyRangeHelp_ = nullptr;
  QLabel *deviceStatus_ = nullptr;
  QLabel *packStatus_ = nullptr;
  QLabel *presentMonStatus_ = nullptr;
  QProgressBar *progress_ = nullptr;
  QTableWidget *sensorTable_ = nullptr;
  QListWidget *resourceList_ = nullptr;
  QLabel *resourceInfo_ = nullptr;
  IconEditor *iconEditor_ = nullptr;
  ResourcePreview *resourcePreview_ = nullptr;
  QListWidget *actionList_ = nullptr;
  QComboBox *bindingCombos_[4]{};
  QSystemTrayIcon *tray_ = nullptr;
  bool deviceConnected_ = false;
  bool quitting_ = false;
};
