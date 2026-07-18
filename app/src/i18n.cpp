#include "i18n.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>

namespace {
bool english = false;

const QHash<QString, QString> &translations() {
  static const QHash<QString, QString> values = {
      {"&Файл", "&File"}, {"Новый", "New"}, {"Открыть…", "Open…"},
      {"Сохранить", "Save"}, {"Сохранить как…", "Save as…"},
      {"Выход", "Exit"}, {"&Вид", "&View"},
      {"Тема интерфейса", "Interface theme"},
      {"Язык интерфейса", "Interface language"},
      {"Запускать вместе с Windows", "Start with Windows"},
      {"Открыть", "Open"}, {"Загрузить макет", "Upload layout"},
      {"Устройство", "Device"}, {"Экраны", "Screens"},
      {"Датчики", "Sensors"}, {"Медиа", "Media"}, {"Кнопки", "Buttons"},
      {"Состояние:", "Status:"}, {"Пакет:", "Package:"},
      {"Проверить проект", "Validate project"},
      {"Загрузить на Trezor", "Upload to Trezor"},
      {"В bootloader", "Enter bootloader"},
      {"Установить firmware…", "Install firmware…"},
      {"Настроить FPS / PresentMon…", "Configure FPS / PresentMon…"},
      {"+ Экран", "+ Screen"}, {"Удалить экран", "Delete screen"},
      {"Выше", "Up"}, {"Ниже", "Down"},
      {"Слои текущего экрана", "Current screen layers"},
      {"Удалить элемент", "Delete widget"},
      {"Новый элемент", "New widget"},
      {"Добавить элемент", "Add widget"},
      {"Точный пиксельный preview", "Exact pixel preview"},
      {"Защита OLED от выгорания", "OLED burn-in protection"},
      {"Свойства элемента", "Widget properties"},
      {"Ширина:", "Width:"}, {"Высота:", "Height:"},
      {"Текст / {v}:", "Text / {v}:"}, {"Метрика:", "Metric:"},
      {"Шрифт:", "Font:"}, {"Размер, px:", "Size, px:"},
      {"Изображение / GIF:", "Image / GIF:"},
      {"Минимум:", "Minimum:"}, {"Максимум:", "Maximum:"},
      {"Сегментов:", "Segments:"},
      {"Автомасштаб максимума", "Automatic maximum scale"},
      {"Подобрать по датчику", "Suggest for sensor"},
      {"Канал", "Channel"}, {"Значение", "Value"},
      {"Единица", "Unit"}, {"Статус", "Status"},
      {"Изображения и GIF", "Images and GIFs"},
      {"Импорт PNG/GIF…", "Import PNG/GIF…"},
      {"Новая пиктограмма", "New icon"},
      {"Добавить на текущий экран", "Add to current screen"},
      {"Переименовать", "Rename"}, {"Удалить ресурс", "Delete resource"},
      {"Карандаш / ластик", "Pencil / eraser"}, {"Очистить", "Clear"},
      {"1-битное преобразование", "1-bit conversion"},
      {"Без дизеринга", "No dithering"},
      {"Упорядоченный 4×4", "Ordered 4×4"},
      {"Инвертировать при выводе", "Invert output"},
      {"Авто по фону", "Detect from background"},
      {"Порог:", "Threshold:"}, {"Дизеринг:", "Dithering:"},
      {"Выберите ресурс", "Select a resource"},
      {"Пиксельный редактор одиночного изображения", "Single-image pixel editor"},
      {"Редактор исходных пикселей (для PNG/иконок)", "Source pixel editor (PNG/icons)"},
      {"Действия текущего экрана", "Current screen actions"},
      {"Назначения кнопок", "Button assignments"},
      {"Общие кнопки для всех экранов", "Shared buttons for all screens"},
      {"При включении назначения текущего экрана копируются на все экраны.",
       "When enabled, the current screen assignments are copied to every screen."},
      {"Переименовать экран", "Rename screen"},
      {"Название экрана:", "Screen name:"},
      {"Двойной щелчок — переименовать.", "Double-click to rename."},
      {"Левая — нажатие", "Left — press"},
      {"Левая — удержание", "Left — hold"},
      {"Правая — нажатие", "Right — press"},
      {"Правая — удержание", "Right — hold"},
      {"Действия ПК", "PC actions"}, {"+ Действие", "+ Action"},
      {"Проверить выбранное", "Test selected"}, {"Удалить", "Delete"},
      {"Название:", "Name:"}, {"Тип:", "Type:"}, {"Значение:", "Value:"},
      {"Нет", "None"}, {"Предыдущий экран", "Previous screen"},
      {"Следующий экран", "Next screen"}, {"Не выбрано", "Not selected"},
      {"Основной", "Main"}, {"Обзор", "Overview"},
      {"Крупно: главное", "Large: essentials"},
      {"Плотно: все данные", "Dense: all metrics"},
      {"Графики", "Graphs"}, {"Память", "Memory"},
      {"Статический текст", "Static text"}, {"Изображение", "Image"},
      {"Анимация", "Animation"},
      {"Горизонтальная шкала", "Horizontal bar"},
      {"Вертикальная шкала", "Vertical bar"}, {"Сегменты", "Segments"},
      {"Стрелка", "Needle"}, {"Кольцо", "Ring"}, {"График", "Graph"},
      {"Рамка", "Frame"}, {"Линия", "Line"},
      {"Условная иконка", "Conditional icon"}, {"Предупреждение", "Warning"},
      {"Сочетание клавиш", "Keyboard shortcut"},
      {"Запуск приложения", "Launch application"}, {"Команда", "Command"},
      {"Поиск устройства…", "Searching for device…"},
      {"Макет ещё не скомпилирован", "Layout has not been compiled"},
      {"Ожидание Trezor PC Monitor…", "Waiting for Trezor PC Monitor…"},
      {"Trezor PC Monitor подключён", "Trezor PC Monitor connected"},
      {"Макет загружен", "Layout uploaded"},
      {"Ошибка загрузки макета", "Layout upload failed"},
      {"Связь потеряна, переподключение…", "Connection lost, reconnecting…"},
      {"PresentMon API не установлен", "PresentMon API is not installed"},
      {"PresentMon service недоступен", "PresentMon service is unavailable"},
      {"PresentMon подключён", "PresentMon connected"},
      {"Выберите элемент и двигайте его стрелками с шагом 1 пиксель.",
       "Select a widget and move it one pixel at a time with the arrow keys."},
      {"Порядок снизу вверх. Выберите элемент для настройки или удаления.",
       "Layers are ordered bottom to top. Select one to edit or delete it."},
      {"Оставляет безопасный край и каждые 2 минуты сдвигает изображение на 1 пиксель по часовой стрелке.",
       "Keeps a safe border and moves the image clockwise every 2 minutes."},
      {"Ширина безопасного края и максимальное отклонение pixel shift.",
       "Safe border width and maximum pixel-shift offset."},
      {"Для изображений и GIF это реальный размер на OLED.",
       "For images and GIFs this is the actual OLED size."},
      {"Ноль остаётся снизу, а верхняя граница плавно следует за значениями с запасом.",
       "Zero stays at the bottom while the upper bound adapts with headroom."},
      {"Программа уже запущена. Закройте существующее окно или используйте меню в трее.",
       "Trezor PC Monitor is already running. Open the existing window from the tray or close it first."},
      {"Отбросить несохранённые изменения?",
       "Discard unsaved changes?"},
  };
  return values;
}

const QList<QPair<QString, QString>> &phraseTranslations() {
  static const QList<QPair<QString, QString>> values = {
      {"Горизонтальная шкала", "Horizontal bar"},
      {"Вертикальная шкала", "Vertical bar"},
      {"Статический текст", "Static text"},
      {"Условная иконка", "Conditional icon"},
      {"Предупреждение", "Warning"}, {"Изображение", "Image"},
      {"Анимация", "Animation"}, {"Сегменты", "Segments"},
      {"Стрелка", "Needle"}, {"Кольцо", "Ring"},
      {"График", "Graph"}, {"Рамка", "Frame"}, {"Линия", "Line"},
      {"Значение", "Value"},
      {"Переход в bootloader…", "Entering bootloader…"},
      {"Открыт последний проект:", "Restored last project:"},
      {"Экран включён и попадёт на устройство", "Screen is enabled and will be uploaded"},
      {"Экран сохранён в проекте, но не компилируется", "Screen is stored but excluded from the package"},
      {"Перейти:", "Go to:"}, {"ПК:", "PC:"},
      {"кадров", "frames"}, {"кадр", "frame"},
      {"инверсия", "inverted"}, {"Статическое изображение", "Static image"},
      {"Пиктограмма", "Icon"}, {"Экран ", "Screen "},
      {" px отступ", " px margin"}, {" байт, ", " bytes, "},
      {" каналов", " channels"},
      {"Неизвестно", "Unknown"},
  };
  return values;
}

class TranslationFilter : public QObject {
 public:
  using QObject::QObject;
 protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (english && event->type() == QEvent::Show) I18n::translateUi(watched);
    return QObject::eventFilter(watched, event);
  }
};

void translateObject(QObject *object) {
  if (auto *label = qobject_cast<QLabel *>(object)) label->setText(I18n::text(label->text()));
  if (auto *button = qobject_cast<QAbstractButton *>(object)) button->setText(I18n::text(button->text()));
  if (auto *group = qobject_cast<QGroupBox *>(object)) group->setTitle(I18n::text(group->title()));
  if (auto *menu = qobject_cast<QMenu *>(object)) menu->setTitle(I18n::text(menu->title()));
  if (auto *line = qobject_cast<QLineEdit *>(object))
    line->setPlaceholderText(I18n::text(line->placeholderText()));
  if (auto *spin = qobject_cast<QSpinBox *>(object)) {
    spin->setPrefix(I18n::text(spin->prefix()));
    spin->setSuffix(I18n::text(spin->suffix()));
  }
  if (auto *combo = qobject_cast<QComboBox *>(object))
    for (int i = 0; i < combo->count(); ++i) combo->setItemText(i, I18n::text(combo->itemText(i)));
  if (auto *list = qobject_cast<QListWidget *>(object))
    for (int i = 0; i < list->count(); ++i) list->item(i)->setText(I18n::text(list->item(i)->text()));
  if (auto *tabs = qobject_cast<QTabWidget *>(object))
    for (int i = 0; i < tabs->count(); ++i) tabs->setTabText(i, I18n::text(tabs->tabText(i)));
  if (auto *table = qobject_cast<QTableWidget *>(object)) {
    for (int column = 0; column < table->columnCount(); ++column)
      if (table->horizontalHeaderItem(column))
        table->horizontalHeaderItem(column)->setText(
            I18n::text(table->horizontalHeaderItem(column)->text()));
  }
  if (auto *action = qobject_cast<QAction *>(object)) {
    action->setText(I18n::text(action->text()));
    action->setToolTip(I18n::text(action->toolTip()));
  }
  if (auto *widget = qobject_cast<QWidget *>(object))
    widget->setToolTip(I18n::text(widget->toolTip()));
}
}  // namespace

namespace I18n {

QString settingsPath() {
  const QString directory = QCoreApplication::applicationDirPath() +
                            QStringLiteral("/portable-data");
  QDir().mkpath(directory);
  return directory + QStringLiteral("/settings.ini");
}

void initialize(QApplication &application) {
  QSettings settings(settingsPath(), QSettings::IniFormat);
  english = settings.value(QStringLiteral("ui/language"),
                           QStringLiteral("ru")).toString() ==
            QStringLiteral("en");
  application.installEventFilter(new TranslationFilter(&application));
}

QString language() { return english ? QStringLiteral("en") : QStringLiteral("ru"); }
bool isEnglish() { return english; }

void setLanguage(const QString &value) {
  english = value.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0;
  QSettings settings(settingsPath(), QSettings::IniFormat);
  settings.setValue(QStringLiteral("ui/language"), language());
  settings.sync();
}

QString text(const QString &source) {
  if (!english || source.isEmpty()) return source;
  const auto exact = translations().constFind(source);
  if (exact != translations().constEnd()) return exact.value();
  QString result = source;
  for (const auto &pair : phraseTranslations()) result.replace(pair.first, pair.second);
  return result;
}

void translateUi(QObject *root) {
  if (!english || !root) return;
  translateObject(root);
  const auto children = root->findChildren<QObject *>();
  for (QObject *child : children) translateObject(child);
}

}  // namespace I18n
