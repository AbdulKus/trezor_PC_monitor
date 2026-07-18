#include "projectmodel.h"

#include <QBuffer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QUuid>

#include "zipstore.h"

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QJsonObject bindingToJson(const ButtonBinding &binding) {
  return {{"type", int(binding.type)},
          {"target", binding.target},
          {"hostActionId", binding.hostActionId}};
}
ButtonBinding bindingFromJson(const QJsonObject &object) {
  ButtonBinding value;
  value.type = tm_action_type_t(object.value("type").toInt());
  value.target = quint8(object.value("target").toInt());
  value.hostActionId = quint16(object.value("hostActionId").toInt());
  return value;
}
}  // namespace

ProjectModel::ProjectModel(QObject *parent) : QObject(parent) { resetToDefault(); }

void ProjectModel::setCurrentScreen(int index) {
  if (index < 0 || index >= screens_.size() || index == currentScreen_) return;
  currentScreen_ = index;
  emit currentScreenChanged(index);
}

void ProjectModel::setModified(bool modified) {
  if (modified_ == modified) return;
  modified_ = modified;
  emit modifiedChanged(modified);
  if (modified) emit changed();
}

void ProjectModel::resetToDefault() {
  screens_.clear();
  resources_.clear();
  actions_.clear();

  ScreenModel overview;
  overview.name = QStringLiteral("Обзор");
  WidgetModel title;
  title.id = newId();
  title.type = TM_WIDGET_STATIC_TEXT;
  title.geometry = QRect(2, 1, 78, 9);
  title.text = QStringLiteral("SYSTEM MONITOR");
  title.font.setPixelSize(8);
  overview.widgets << title;
  WidgetModel cpu;
  cpu.id = newId();
  cpu.geometry = QRect(2, 13, 58, 9);
  cpu.text = QStringLiteral("CPU {v}%");
  cpu.metric = QStringLiteral("cpu.total.load");
  cpu.precision = 0;
  overview.widgets << cpu;
  WidgetModel cpuBar = cpu;
  cpuBar.id = newId();
  cpuBar.type = TM_WIDGET_BAR_HORIZONTAL;
  cpuBar.geometry = QRect(64, 14, 61, 7);
  overview.widgets << cpuBar;
  WidgetModel ram = cpu;
  ram.id = newId();
  ram.geometry = QRect(2, 27, 58, 9);
  ram.text = QStringLiteral("RAM {v}%");
  ram.metric = QStringLiteral("memory.ram.load");
  overview.widgets << ram;
  WidgetModel ramBar = ram;
  ramBar.id = newId();
  ramBar.type = TM_WIDGET_BAR_HORIZONTAL;
  ramBar.geometry = QRect(64, 28, 61, 7);
  overview.widgets << ramBar;
  WidgetModel fps = cpu;
  fps.id = newId();
  fps.geometry = QRect(2, 42, 70, 9);
  fps.text = QStringLiteral("FPS {v}");
  fps.metric = QStringLiteral("foreground.fps.displayed");
  fps.maximum = 240;
  fps.precision = 0;
  overview.widgets << fps;
  WidgetModel graph = fps;
  graph.id = newId();
  graph.type = TM_WIDGET_SPARKLINE;
  graph.geometry = QRect(73, 40, 52, 20);
  overview.widgets << graph;
  overview.leftShort.type = TM_ACTION_PREVIOUS_PAGE;
  overview.rightShort.type = TM_ACTION_NEXT_PAGE;

  ScreenModel gpu;
  gpu.name = QStringLiteral("GPU");
  WidgetModel gpuTitle = title;
  gpuTitle.id = newId();
  gpuTitle.text = QStringLiteral("ACTIVE GPU");
  gpu.widgets << gpuTitle;
  WidgetModel gpuLoad = cpu;
  gpuLoad.id = newId();
  gpuLoad.text = QStringLiteral("LOAD {v}%");
  gpuLoad.metric = QStringLiteral("gpu.active.load");
  gpu.widgets << gpuLoad;
  WidgetModel gpuRing = gpuLoad;
  gpuRing.id = newId();
  gpuRing.type = TM_WIDGET_RING;
  gpuRing.geometry = QRect(91, 3, 32, 32);
  gpu.widgets << gpuRing;
  WidgetModel gpuTemp = cpu;
  gpuTemp.id = newId();
  gpuTemp.geometry = QRect(2, 28, 82, 9);
  gpuTemp.text = QStringLiteral("TEMP {v} C");
  gpuTemp.metric = QStringLiteral("gpu.active.temperature");
  gpuTemp.maximum = 100;
  gpu.widgets << gpuTemp;
  WidgetModel gpuPower = gpuTemp;
  gpuPower.id = newId();
  gpuPower.geometry = QRect(2, 42, 90, 9);
  gpuPower.text = QStringLiteral("POWER {v} W");
  gpuPower.metric = QStringLiteral("gpu.active.power");
  gpuPower.maximum = 400;
  gpuPower.precision = 1;
  gpu.widgets << gpuPower;
  gpu.leftShort.type = TM_ACTION_PREVIOUS_PAGE;
  gpu.rightShort.type = TM_ACTION_NEXT_PAGE;

  // A large, glanceable page for the most important values.
  ScreenModel essentials;
  essentials.name = QStringLiteral("Крупно: главное");
  auto largeValue = [&](const QString &text, const QString &metric, int y,
                        int maximum) {
    WidgetModel value;
    value.id = newId();
    value.type = TM_WIDGET_DYNAMIC_TEXT;
    value.geometry = QRect(2, y, 124, 13);
    value.text = text;
    value.metric = metric;
    value.maximum = maximum;
    value.font.setFamily(QStringLiteral("Spleen 6x12"));
    value.font.setPixelSize(12);
    essentials.widgets << value;
  };
  largeValue(QStringLiteral("CPU  {v}%"), QStringLiteral("cpu.total.load"), 1, 100);
  largeValue(QStringLiteral("GPU  {v}%"), QStringLiteral("gpu.active.load"), 17, 100);
  largeValue(QStringLiteral("RAM  {v}%"), QStringLiteral("memory.ram.load"), 33, 100);
  largeValue(QStringLiteral("FPS  {v}"), QStringLiteral("foreground.fps.displayed"), 49, 240);

  // Dense page: twelve live channels without decorative dead space.
  ScreenModel dense;
  dense.name = QStringLiteral("Плотно: все данные");
  auto denseValue = [&](int x, int y, const QString &text,
                        const QString &metric, int maximum,
                        int precision = 0) {
    WidgetModel value;
    value.id = newId();
    value.type = TM_WIDGET_DYNAMIC_TEXT;
    value.geometry = QRect(x, y, 61, 8);
    value.text = text;
    value.metric = metric;
    value.maximum = maximum;
    value.precision = precision;
    value.font.setFamily(QStringLiteral("Spleen 5x8"));
    value.font.setPixelSize(8);
    dense.widgets << value;
  };
  denseValue(1, 1,  QStringLiteral("CPU {v}%"), QStringLiteral("cpu.total.load"), 100);
  denseValue(1, 11, QStringLiteral("CT  {v}C"), QStringLiteral("cpu.package.temperature"), 110);
  denseValue(1, 21, QStringLiteral("CP  {v}W"), QStringLiteral("cpu.package.power"), 250, 1);
  denseValue(1, 31, QStringLiteral("RAM {v}%"), QStringLiteral("memory.ram.load"), 100);
  denseValue(1, 41, QStringLiteral("RU {v}M"), QStringLiteral("memory.ram.used"), 32768);
  denseValue(1, 51, QStringLiteral("PC {v}W"), QStringLiteral("system.power.estimated"), 750, 1);
  denseValue(66, 1,  QStringLiteral("FPS {v}"), QStringLiteral("foreground.fps.displayed"), 240);
  denseValue(66, 11, QStringLiteral("1L  {v}"), QStringLiteral("foreground.fps.1low"), 240);
  denseValue(66, 21, QStringLiteral("GPU {v}%"), QStringLiteral("gpu.active.load"), 100);
  denseValue(66, 31, QStringLiteral("GT  {v}C"), QStringLiteral("gpu.active.temperature"), 110);
  denseValue(66, 41, QStringLiteral("GP  {v}W"), QStringLiteral("gpu.active.power"), 500, 1);
  denseValue(66, 51, QStringLiteral("VR {v}M"), QStringLiteral("gpu.active.vram.used"), 16384);
  WidgetModel divider;
  divider.id = newId();
  divider.type = TM_WIDGET_LINE;
  divider.geometry = QRect(63, 0, 1, 62);
  dense.widgets << divider;

  ScreenModel graphs;
  graphs.name = QStringLiteral("Графики");
  auto graphRow = [&](int y, const QString &label, const QString &metric,
                      int maximum) {
    WidgetModel caption;
    caption.id = newId();
    caption.type = TM_WIDGET_STATIC_TEXT;
    caption.geometry = QRect(1, y + 4, 27, 8);
    caption.text = label;
    caption.font.setFamily(QStringLiteral("Spleen 5x8"));
    caption.font.setPixelSize(8);
    graphs.widgets << caption;
    WidgetModel spark;
    spark.id = newId();
    spark.type = TM_WIDGET_SPARKLINE;
    spark.geometry = QRect(30, y, 97, 18);
    spark.metric = metric;
    spark.maximum = maximum;
    graphs.widgets << spark;
  };
  graphRow(1, QStringLiteral("FPS"), QStringLiteral("foreground.fps.displayed"), 240);
  graphRow(23, QStringLiteral("CPU"), QStringLiteral("cpu.total.load"), 100);
  graphRow(45, QStringLiteral("GPU"), QStringLiteral("gpu.active.load"), 100);

  ScreenModel memory;
  memory.name = QStringLiteral("Память");
  WidgetModel memoryRam = cpu;
  memoryRam.id = newId();
  memoryRam.geometry = QRect(2, 1, 90, 16);
  memoryRam.text = QStringLiteral("RAM {v}%");
  memoryRam.metric = QStringLiteral("memory.ram.load");
  memoryRam.font.setFamily(QStringLiteral("Spleen 8x16"));
  memoryRam.font.setPixelSize(16);
  memory.widgets << memoryRam;
  WidgetModel memoryRamBar = memoryRam;
  memoryRamBar.id = newId();
  memoryRamBar.type = TM_WIDGET_BAR_HORIZONTAL;
  memoryRamBar.geometry = QRect(2, 19, 124, 8);
  memory.widgets << memoryRamBar;
  WidgetModel memoryVram = memoryRam;
  memoryVram.id = newId();
  memoryVram.geometry = QRect(2, 31, 110, 12);
  memoryVram.text = QStringLiteral("VRAM {v}M");
  memoryVram.metric = QStringLiteral("gpu.active.vram.used");
  memoryVram.maximum = 16384;
  memoryVram.font.setFamily(QStringLiteral("Spleen 6x12"));
  memoryVram.font.setPixelSize(12);
  memory.widgets << memoryVram;
  WidgetModel memoryVramBar = memoryVram;
  memoryVramBar.id = newId();
  memoryVramBar.type = TM_WIDGET_BAR_HORIZONTAL;
  memoryVramBar.geometry = QRect(2, 47, 124, 8);
  memory.widgets << memoryVramBar;
  WidgetModel memoryUsed = cpu;
  memoryUsed.id = newId();
  memoryUsed.geometry = QRect(2, 57, 124, 8);
  memoryUsed.text = QStringLiteral("USED {v} MB");
  memoryUsed.metric = QStringLiteral("memory.ram.used");
  memoryUsed.maximum = 32768;
  memoryUsed.font.setFamily(QStringLiteral("Spleen 5x8"));
  memoryUsed.font.setPixelSize(8);
  memory.widgets << memoryUsed;

  screens_ << essentials << overview << dense << gpu << graphs << memory;
  for (ScreenModel &screen : screens_) {
    screen.leftShort.type = TM_ACTION_PREVIOUS_PAGE;
    screen.rightShort.type = TM_ACTION_NEXT_PAGE;
  }
  currentScreen_ = 0;
  filePath_.clear();
  modified_ = false;
  emit changed();
  emit currentScreenChanged(0);
}

QJsonObject ProjectModel::toJson() const {
  QJsonObject root{{"format", "trezor-pc-monitor"}, {"version", 2}};
  QJsonArray screens;
  for (const ScreenModel &screen : screens_) {
    QJsonArray widgets;
    for (const WidgetModel &widget : screen.widgets) {
      widgets.append(QJsonObject{
          {"id", widget.id},
          {"type", int(widget.type)},
          {"x", widget.geometry.x()},
          {"y", widget.geometry.y()},
          {"width", widget.geometry.width()},
          {"height", widget.geometry.height()},
          {"text", widget.text},
          {"metric", widget.metric},
          {"minimum", widget.minimum},
          {"maximum", widget.maximum},
          {"precision", widget.precision},
          {"fontFamily", widget.font.family()},
          {"fontSize", widget.font.pixelSize() > 0 ? widget.font.pixelSize()
                                                    : widget.font.pointSize()},
          {"fontBold", widget.font.bold()},
          {"resource", widget.resourceIndex},
          {"arg0", widget.arg0},
          {"arg1", widget.arg1},
          {"inverted", widget.inverted},
      });
    }
    screens.append(QJsonObject{{"name", screen.name},
                               {"enabled", screen.enabled},
                               {"widgets", widgets},
                               {"leftShort", bindingToJson(screen.leftShort)},
                               {"leftLong", bindingToJson(screen.leftLong)},
                               {"rightShort", bindingToJson(screen.rightShort)},
                               {"rightLong", bindingToJson(screen.rightLong)}});
  }
  root["screens"] = screens;

  QJsonArray resources;
  for (const ResourceModel &resource : resources_) {
    QJsonArray durations;
    for (int duration : resource.durationsMs) durations.append(duration);
    resources.append(QJsonObject{{"id", resource.id},
                                 {"name", resource.name},
                                 {"frameCount", resource.frames.size()},
                                 {"durations", durations},
                                 {"threshold", resource.threshold},
                                 {"dither", resource.dither},
                                 {"inverted", resource.inverted}});
  }
  root["resources"] = resources;

  QJsonArray actions;
  for (const HostAction &action : actions_) {
    actions.append(QJsonObject{{"id", action.id},
                               {"name", action.name},
                               {"type", int(action.type)},
                               {"value", action.value},
                               {"allowWhenLocked", action.allowWhenLocked}});
  }
  root["actions"] = actions;
  return root;
}

bool ProjectModel::fromJson(const QJsonObject &root, QString *error) {
  const int version = root.value("version").toInt();
  if (root.value("format") != "trezor-pc-monitor" ||
      (version != 1 && version != 2)) {
    if (error) *error = QStringLiteral("Неподдерживаемый формат проекта");
    return false;
  }
  QVector<ScreenModel> newScreens;
  for (const QJsonValue &screenValue : root.value("screens").toArray()) {
    QJsonObject object = screenValue.toObject();
    ScreenModel screen;
    screen.name = object.value("name").toString();
    screen.enabled = object.value("enabled").toBool(true);
    for (const QJsonValue &widgetValue : object.value("widgets").toArray()) {
      QJsonObject value = widgetValue.toObject();
      WidgetModel widget;
      widget.id = value.value("id").toString(newId());
      widget.type = tm_widget_type_t(value.value("type").toInt());
      widget.geometry = QRect(value.value("x").toInt(), value.value("y").toInt(),
                              value.value("width").toInt(),
                              value.value("height").toInt());
      widget.text = value.value("text").toString();
      widget.metric = value.value("metric").toString();
      widget.minimum = value.value("minimum").toInt();
      widget.maximum = value.value("maximum").toInt();
      // Version 1 exposed the protocol's centi-units in the editor. Convert
      // old projects once so users see 100 %, 90 C and 240 FPS.
      if (version == 1) {
        widget.minimum /= 100;
        widget.maximum = qMax(1, widget.maximum / 100);
      }
      widget.precision = quint8(value.value("precision").toInt());
      widget.font = QFont(value.value("fontFamily").toString("Spleen 5x8"));
      widget.font.setPixelSize(value.value("fontSize").toInt(8));
      widget.font.setBold(value.value("fontBold").toBool());
      widget.resourceIndex = value.value("resource").toInt(-1);
      widget.arg0 = quint16(value.value("arg0").toInt());
      widget.arg1 = quint16(value.value("arg1").toInt());
      widget.inverted = value.value("inverted").toBool();
      screen.widgets << widget;
    }
    screen.leftShort = bindingFromJson(object.value("leftShort").toObject());
    screen.leftLong = bindingFromJson(object.value("leftLong").toObject());
    screen.rightShort = bindingFromJson(object.value("rightShort").toObject());
    screen.rightLong = bindingFromJson(object.value("rightLong").toObject());
    newScreens << screen;
  }
  if (newScreens.isEmpty() || newScreens.size() > int(TM_MAX_SCREENS)) {
    if (error) *error = QStringLiteral("Проект должен содержать от 1 до 8 экранов");
    return false;
  }
  QVector<ResourceModel> newResources;
  for (const QJsonValue &resourceValue : root.value("resources").toArray()) {
    QJsonObject value = resourceValue.toObject();
    ResourceModel resource;
    resource.id = value.value("id").toString(newId());
    resource.name = value.value("name").toString();
    resource.frames.resize(value.value("frameCount").toInt());
    for (const QJsonValue &duration : value.value("durations").toArray())
      resource.durationsMs << duration.toInt(100);
    resource.threshold = value.value("threshold").toInt(128);
    resource.dither = value.value("dither").toString("ordered");
    resource.inverted = value.value("inverted").toBool();
    newResources << resource;
  }
  QVector<HostAction> newActions;
  for (const QJsonValue &actionValue : root.value("actions").toArray()) {
    QJsonObject value = actionValue.toObject();
    newActions << HostAction{quint16(value.value("id").toInt()),
                             value.value("name").toString(),
                             HostActionType(value.value("type").toInt()),
                             value.value("value").toString(),
                             value.value("allowWhenLocked").toBool()};
  }
  screens_ = newScreens;
  resources_ = newResources;
  actions_ = newActions;
  currentScreen_ = 0;
  return true;
}

bool ProjectModel::save(const QString &path, QString *error) {
  QHash<QString, QByteArray> files;
  files["project.json"] = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
  for (const ResourceModel &resource : resources_) {
    for (int i = 0; i < resource.frames.size(); i++) {
      QByteArray png;
      QBuffer buffer(&png);
      buffer.open(QIODevice::WriteOnly);
      resource.frames[i].save(&buffer, "PNG");
      files[QStringLiteral("assets/%1/%2.png").arg(resource.id).arg(i)] = png;
    }
  }
  if (!ZipStore::write(path, files, error)) return false;
  filePath_ = QFileInfo(path).absoluteFilePath();
  setModified(false);
  return true;
}

bool ProjectModel::load(const QString &path, QString *error) {
  QHash<QString, QByteArray> files;
  if (!ZipStore::read(path, &files, error)) return false;
  QJsonParseError jsonError;
  QJsonDocument document = QJsonDocument::fromJson(files.value("project.json"),
                                                    &jsonError);
  if (jsonError.error != QJsonParseError::NoError || !document.isObject()) {
    if (error) *error = jsonError.errorString();
    return false;
  }
  if (!fromJson(document.object(), error)) return false;
  for (ResourceModel &resource : resources_) {
    for (int i = 0; i < resource.frames.size(); i++) {
      QByteArray data = files.value(
          QStringLiteral("assets/%1/%2.png").arg(resource.id).arg(i));
      if (!resource.frames[i].loadFromData(data, "PNG")) {
        if (error) *error = QStringLiteral("Не удалось прочитать ресурс %1").arg(resource.name);
        return false;
      }
    }
  }
  filePath_ = QFileInfo(path).absoluteFilePath();
  setModified(false);
  emit changed();
  emit currentScreenChanged(0);
  return true;
}

QString widgetTypeName(tm_widget_type_t type) {
  static const QStringList names = {
      {},          "Статический текст", "Значение",       "Изображение",
      "Анимация", "Горизонтальная шкала", "Вертикальная шкала",
      "Сегменты", "Стрелка",          "Кольцо",         "График",
      "Рамка",    "Линия",            "Условная иконка", "Предупреждение"};
  return int(type) >= 0 && int(type) < names.size() ? names[int(type)]
                                                    : QStringLiteral("Неизвестно");
}

QString actionTypeName(HostActionType type) {
  static const QStringList names = {"Нет", "Сочетание клавиш", "Media key",
                                    "Запуск приложения", "URL", "Команда",
                                    "PowerShell"};
  return int(type) >= 0 && int(type) < names.size() ? names[int(type)]
                                                    : QStringLiteral("Нет");
}
