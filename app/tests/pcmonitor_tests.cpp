#include <QtTest>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cstring>

#include "packcompiler.h"
#include "designcanvas.h"
#include "fpsscaler.h"
#include "protocol.h"
#include "projectmodel.h"
#include "zipstore.h"
#include "monitor.h"
#include "pixelfont.h"
#include "resourceimporter.h"
#include "resourcepreview.h"

extern "C" void test_set_timer_ms(uint32_t value);

namespace {
tm_packet_t packet(quint8 type, quint8 sequence, const QByteArray &payload = {}) {
  tm_packet_t result{};
  result.magic[0] = 'T';
  result.magic[1] = 'M';
  result.version = TM_PROTOCOL_VERSION;
  result.type = type;
  result.sequence = sequence;
  result.payload_length = quint16(qMin<int>(payload.size(), TM_PACKET_PAYLOAD_SIZE));
  std::memcpy(result.payload, payload.constData(), result.payload_length);
  result.crc32 = tm_crc32(reinterpret_cast<const uint8_t *>(&result), 60);
  return result;
}

tm_packet_t sendAndReceive(const tm_packet_t &outgoing) {
  monitor_handle_packet(reinterpret_cast<const uint8_t *>(&outgoing));
  tm_packet_t incoming{};
  const bool available = monitor_take_packet(reinterpret_cast<uint8_t *>(&incoming));
  Q_ASSERT(available);
  return incoming;
}
}  // namespace

class PcMonitorTests : public QObject {
  Q_OBJECT

 private slots:
  void protocolLayoutAndCrc();
  void zipRoundTripAndRejectsTruncation();
  void projectRoundTripWithBitmap();
  void bitmapFontsAreExactAndContainCyrillic();
  void oneBitConversionRespondsImmediately();
  void animatedGifImportsEveryFrame();
  void livePreviewRendersAndAnimates();
  void selectedWidgetMovesOnePixelWithArrowKeys();
  void fpsScaleUsesStablePresetBands();
  void packIsDeterministicAndChecksLimits();
  void firmwareParserTransactionsButtonsAndFuzz();
};

void PcMonitorTests::fpsScaleUsesStablePresetBands() {
  QVERIFY(FpsScaler::accepts(4, 121.0));
  QVERIFY(FpsScaler::accepts(4, 151.0));
  QVERIFY(!FpsScaler::accepts(4, 120.0));
  QVERIFY(!FpsScaler::accepts(4, 152.0));

  FpsScaler down;
  QVERIFY(!down.update(144.0, 0));
  QCOMPARE(down.maximum(), 144.0);
  for (int timestamp = 250; timestamp < 10000; timestamp += 250) {
    QVERIFY(!down.update(60.0, timestamp));
    QCOMPARE(down.maximum(), 144.0);
  }
  QVERIFY(down.update(60.0, 10000));
  QCOMPARE(down.maximum(), 60.0);

  FpsScaler dip;
  dip.update(144.0, 0);
  dip.update(30.0, 250);
  for (int timestamp = 500; timestamp <= 12000; timestamp += 250)
    QVERIFY(!dip.update(144.0, timestamp));
  QCOMPARE(dip.maximum(), 144.0);

  FpsScaler up;
  up.update(144.0, 0);
  for (int timestamp = 250; timestamp < 10000; timestamp += 250)
    QVERIFY(!up.update(152.0, timestamp));
  QVERIFY(up.update(152.0, 10000));
  QCOMPARE(up.maximum(), 165.0);
}

void PcMonitorTests::selectedWidgetMovesOnePixelWithArrowKeys() {
  ProjectModel project;
  project.screens().first().widgets.clear();
  WidgetModel widget;
  widget.id = QStringLiteral("keyboard-nudge");
  widget.geometry = QRect(10, 10, 20, 8);
  project.screens().first().widgets << widget;

  DesignCanvas canvas;
  canvas.setProject(&project);
  canvas.setSelectedWidget(0);
  QSignalSpy moved(&canvas, &DesignCanvas::widgetGeometryChanged);

  QTest::keyClick(&canvas, Qt::Key_Right);
  QTest::keyClick(&canvas, Qt::Key_Down);
  QCOMPARE(project.screens().first().widgets.first().geometry.topLeft(),
           QPoint(11, 11));
  QCOMPARE(moved.count(), 2);

  project.screens().first().widgets.first().geometry.moveTopLeft(QPoint(0, 0));
  QTest::keyClick(&canvas, Qt::Key_Left);
  QTest::keyClick(&canvas, Qt::Key_Up);
  QCOMPARE(project.screens().first().widgets.first().geometry.topLeft(),
           QPoint(0, 0));
  QCOMPARE(moved.count(), 2);
}

void PcMonitorTests::livePreviewRendersAndAnimates() {
  ResourceModel resource;
  QString error;
  const QString path = QStringLiteral(PCMONITOR_SOURCE_DIR)
                           + QStringLiteral("/app/tests/data/animated.gif");
  QVERIFY2(ResourceImporter::importFile(path, &resource, &error),
           qPrintable(error));
  const QSize size = resource.frames.first().size().boundedTo(QSize(128, 64));
  const QByteArray firstBits = PackCompiler::monochromeBits(
      resource.frames.first(), size, resource.threshold, resource.dither,
      resource.inverted);
  int differentFrame = -1;
  for (int i = 1; i < resource.frames.size(); ++i) {
    if (PackCompiler::monochromeBits(resource.frames[i], size,
                                     resource.threshold, resource.dither,
                                     resource.inverted) != firstBits) {
      differentFrame = i;
      break;
    }
  }
  QVERIFY2(differentFrame > 0,
           "Imported GIF frames collapse to one identical 1-bit image");
  resource.frames = {resource.frames.first(), resource.frames[differentFrame]};
  resource.durationsMs = {100, 100};

  ResourcePreview preview;
  preview.resize(680, 380);
  preview.setResource(resource);
  preview.show();
  QTest::qWait(30);
  const QImage first = preview.oledImage();
  bool changed = false;
  for (int i = 0; i < 20 && !changed; ++i) {
    QTest::qWait(83);
    changed = preview.oledImage() != first;
  }
  QVERIFY2(changed, "1-bit GIF preview remained a static frame");
}

void PcMonitorTests::oneBitConversionRespondsImmediately() {
  QImage gradient(16, 16, QImage::Format_ARGB32);
  for (int y = 0; y < gradient.height(); ++y)
    for (int x = 0; x < gradient.width(); ++x) {
      const int value = x * 255 / (gradient.width() - 1);
      gradient.setPixelColor(x, y, QColor(value, value, value));
    }
  const QByteArray plain = PackCompiler::monochromeBits(
      gradient, gradient.size(), 128, QStringLiteral("none"), false);
  const QByteArray ordered = PackCompiler::monochromeBits(
      gradient, gradient.size(), 128, QStringLiteral("ordered"), false);
  const QByteArray inverted = PackCompiler::monochromeBits(
      gradient, gradient.size(), 128, QStringLiteral("ordered"), true);
  QVERIFY(plain != ordered);
  QCOMPARE(ordered.size(), inverted.size());
  for (qsizetype i = 0; i < ordered.size(); ++i)
    QCOMPARE(quint8(inverted[i]), quint8(~quint8(ordered[i])));
}

void PcMonitorTests::animatedGifImportsEveryFrame() {
  ResourceModel resource;
  QString error;
  const QString path = QStringLiteral(PCMONITOR_SOURCE_DIR)
                           + QStringLiteral("/app/tests/data/animated.gif");
  QVERIFY2(ResourceImporter::importFile(path, &resource, &error),
           qPrintable(error));
  QVERIFY(resource.frames.size() >= 2);
  QVERIFY(resource.frames.first() != resource.frames.last());
  QCOMPARE(resource.durationsMs.size(), resource.frames.size());
  for (int delay : resource.durationsMs)
    QVERIFY(delay >= 20 && delay <= 5000);

  ResourceModel compact = resource;
  compact.frames.resize(3);
  compact.durationsMs.resize(3);
  ProjectModel project;
  project.resources() << compact;
  WidgetModel animation;
  animation.id = QStringLiteral("gif-test");
  animation.type = TM_WIDGET_ANIMATION;
  animation.resourceIndex = 0;
  animation.geometry = QRect(0, 0, 16, 12);
  project.screens().first().widgets << animation;
  const PackCompileResult compiled = PackCompiler::compile(project);
  QVERIFY2(compiled.ok(), qPrintable(compiled.error));
  const auto *header = reinterpret_cast<const tm_pack_header_t *>(
      compiled.data.constData());
  QCOMPARE(header->animation_count, quint16(1));
  const auto *record = reinterpret_cast<const tm_animation_t *>(
      compiled.data.constData() + header->animations_offset);
  QCOMPARE(record->frame_count, quint8(3));
  QCOMPARE(record->width, quint8(16));
  QCOMPARE(record->height, quint8(12));

  project.screens().first().widgets.last().geometry = QRect(0, 0, 128, 64);
  const PackCompileResult fullSize = PackCompiler::compile(project);
  QVERIFY2(fullSize.ok(), qPrintable(fullSize.error));
  QVERIFY(fullSize.data.size() > compiled.data.size());

  QImage darkBackground(8, 8, QImage::Format_ARGB32);
  darkBackground.fill(Qt::black);
  darkBackground.setPixelColor(4, 4, Qt::white);
  QVERIFY(ResourceImporter::suggestedInversion(darkBackground));
  darkBackground.fill(Qt::transparent);
  QVERIFY(!ResourceImporter::suggestedInversion(darkBackground));
}

void PcMonitorTests::bitmapFontsAreExactAndContainCyrillic() {
  for (const QString &family : PixelFonts::families()) {
    const PixelFontData font = PixelFonts::load(family);
    QVERIFY2(font.valid(), qPrintable(family));
    QCOMPARE(font.height, PixelFonts::pixelHeight(family));
    QVERIFY(font.glyphs.contains(uint('A')));
    QVERIFY(!font.glyphs.value(uint('A')).bits.isEmpty());
  }
  const PixelFontData cyrillic = PixelFonts::load(QStringLiteral("Spleen 6x12"));
  QVERIFY(cyrillic.glyphs.contains(uint(u'Я')));
}

void PcMonitorTests::protocolLayoutAndCrc() {
  QCOMPARE(sizeof(tm_packet_t), size_t(64));
  QCOMPARE(sizeof(tm_metric_entry_t), size_t(8));
  const QByteArray known("123456789");
  QCOMPARE(tm_crc32(reinterpret_cast<const uint8_t *>(known.constData()),
                    quint32(known.size())),
           quint32(0xcbf43926));

  tm_metric_entry_t metric{};
  metric.status = TM_STATUS_VALID;
  metric.scale_exponent = -2;
  char formatted[32];
  metric.value = 6460;
  tm_format_metric(&metric, 0, formatted);
  QCOMPARE(QByteArray(formatted), QByteArray("65"));
  metric.value = 6449;
  tm_format_metric(&metric, 0, formatted);
  QCOMPARE(QByteArray(formatted), QByteArray("64"));
  metric.value = -6460;
  tm_format_metric(&metric, 0, formatted);
  QCOMPARE(QByteArray(formatted), QByteArray("-65"));
  metric.value = 6499;
  tm_format_metric(&metric, 1, formatted);
  QCOMPARE(QByteArray(formatted), QByteArray("65.0"));
  metric.scale_exponent = 0;
  metric.value = 65;
  tm_format_metric(&metric, 2, formatted);
  QCOMPARE(QByteArray(formatted), QByteArray("65.00"));
  metric.status = TM_STATUS_STALE;
  tm_format_metric(&metric, 0, formatted);
  QCOMPARE(QByteArray(formatted), QByteArray("--"));
}

void PcMonitorTests::zipRoundTripAndRejectsTruncation() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.filePath("sample.tmon");
  const QHash<QString, QByteArray> expected{
      {"project.json", "{\"version\":1}"},
      {QString::fromUtf8("assets/иконка.bin"), QByteArray("\0\1\2", 3)}};
  QString error;
  QVERIFY2(ZipStore::write(path, expected, &error), qPrintable(error));
  QHash<QString, QByteArray> actual;
  QVERIFY2(ZipStore::read(path, &actual, &error), qPrintable(error));
  QCOMPARE(actual, expected);

  const QString shortPath = directory.filePath("short.tmon");
  QFile shortFile(shortPath);
  QVERIFY(shortFile.open(QIODevice::WriteOnly));
  QCOMPARE(shortFile.write("PK"), qint64(2));
  shortFile.close();
  QVERIFY(!ZipStore::read(shortPath, &actual, &error));
}

void PcMonitorTests::projectRoundTripWithBitmap() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  ProjectModel original;
  ResourceModel resource;
  resource.id = "test-icon";
  resource.name = QString::fromUtf8("Тестовая иконка");
  QImage frame(9, 7, QImage::Format_ARGB32);
  frame.fill(Qt::white);
  for (int i = 0; i < 7; ++i) frame.setPixelColor(i + 1, i, Qt::black);
  resource.frames << frame;
  resource.durationsMs << 100;
  original.resources() << resource;
  original.screens()[1].enabled = false;
  original.screens().first().widgets[5].autoRange = true;
  original.setBurnInProtection(true);
  original.setPixelShiftInset(2);
  original.actions() << HostAction{42, QStringLiteral("Copy"),
                                   HostActionType::Shortcut,
                                   QStringLiteral("CTRL+C"), false};
  original.screens().first().leftShort =
      ButtonBinding{TM_ACTION_HOST, 0, 42};

  const QString path = directory.filePath("monitor.tmon");
  QString error;
  QVERIFY2(original.save(path, &error), qPrintable(error));
  ProjectModel loaded;
  QVERIFY2(loaded.load(path, &error), qPrintable(error));
  QCOMPARE(loaded.screens().size(), original.screens().size());
  QCOMPARE(loaded.screens()[1].enabled, false);
  QCOMPARE(loaded.resources().size(), 1);
  QCOMPARE(loaded.resources().first().frames.first(), frame);
  QVERIFY(loaded.screens().first().widgets[5].autoRange);
  QVERIFY(loaded.burnInProtection());
  QCOMPARE(loaded.pixelShiftInset(), 2);
  QCOMPARE(loaded.actions().size(), 1);
  QCOMPARE(loaded.actions().first().value, QStringLiteral("CTRL+C"));
  QCOMPARE(loaded.screens().first().leftShort.hostActionId, quint16(42));
}

void PcMonitorTests::packIsDeterministicAndChecksLimits() {
  ProjectModel project;
  QCOMPARE(project.screens().first().name, QStringLiteral("Основной"));
  QCOMPARE(project.screens().first().widgets.size(), 12);
  QCOMPARE(project.screens().first().widgets[5].geometry, QRect(59, 45, 67, 17));
  QCOMPARE(project.screens().first().widgets[7].metric,
           QStringLiteral("gpu.active.load"));
  const PackCompileResult first = PackCompiler::compile(project);
  QVERIFY2(first.ok(), qPrintable(first.error));
  const PackCompileResult second = PackCompiler::compile(project);
  QVERIFY2(second.ok(), qPrintable(second.error));
  QCOMPARE(first.data, second.data);
  QVERIFY(first.data.size() <= int(TM_MAX_PACK_SIZE));

  tm_pack_header_t header{};
  std::memcpy(&header, first.data.constData(), sizeof(header));
  QCOMPARE(QByteArray(reinterpret_cast<const char *>(header.magic), 4),
           QByteArray("TMPK", 4));
  QCOMPARE(header.version, quint16(TM_PACK_VERSION));
  QCOMPARE(header.total_size, quint32(first.data.size()));
  const auto *widgets = reinterpret_cast<const tm_widget_t *>(
      first.data.constData() + header.widgets_offset);
  bool foundPercentGauge = false;
  bool foundAutoRangeGraph = false;
  bool foundFpsPresetGraph = false;
  for (quint16 i = 0; i < header.widget_count; ++i) {
    if (widgets[i].type == TM_WIDGET_BAR_HORIZONTAL &&
        widgets[i].maximum == 10000) {
      QCOMPARE(widgets[i].minimum, qint32(0));
      QCOMPARE(widgets[i].maximum, qint32(10000));
      foundPercentGauge = true;
    }
    if (widgets[i].type == TM_WIDGET_SPARKLINE &&
        (widgets[i].flags & TM_WIDGET_FLAG_AUTO_RANGE) != 0) {
      foundAutoRangeGraph = true;
      if ((widgets[i].flags & TM_WIDGET_FLAG_FPS_PRESETS) != 0)
        foundFpsPresetGraph = true;
    }
  }
  QVERIFY(foundPercentGauge);
  QVERIFY(foundAutoRangeGraph);
  QVERIFY(foundFpsPresetGraph);

  project.setBurnInProtection(true);
  project.setPixelShiftInset(3);
  const PackCompileResult protectedPack = PackCompiler::compile(project);
  QVERIFY2(protectedPack.ok(), qPrintable(protectedPack.error));
  const auto *protectedHeader = reinterpret_cast<const tm_pack_header_t *>(
      protectedPack.data.constData());
  QVERIFY((protectedHeader->reserved[0] & TM_PACK_FLAG_PIXEL_SHIFT) != 0);
  QCOMPARE(protectedHeader->reserved[1], quint8(3));
  const quint32 expectedCrc = header.crc32;
  QByteArray withoutCrc = first.data;
  reinterpret_cast<tm_pack_header_t *>(withoutCrc.data())->crc32 = 0;
  QCOMPARE(tm_crc32(reinterpret_cast<const uint8_t *>(withoutCrc.constData()),
                    quint32(withoutCrc.size())),
           expectedCrc);

  ProjectModel filtered;
  filtered.screens()[0].rightShort.type = TM_ACTION_GO_TO_PAGE;
  filtered.screens()[0].rightShort.target = 2;
  filtered.screens()[1].enabled = false;
  const PackCompileResult filteredPack = PackCompiler::compile(filtered);
  QVERIFY2(filteredPack.ok(), qPrintable(filteredPack.error));
  const auto *filteredHeader = reinterpret_cast<const tm_pack_header_t *>(
      filteredPack.data.constData());
  QCOMPARE(filteredHeader->screen_count,
           quint8(filtered.screens().size() - 1));
  const auto *filteredScreens = reinterpret_cast<const tm_screen_t *>(
      filteredPack.data.constData() + filteredHeader->screens_offset);
  QCOMPARE(filteredScreens[0].right_short.type,
           quint8(TM_ACTION_GO_TO_PAGE));
  QCOMPARE(filteredScreens[0].right_short.target, quint8(1));
  for (ScreenModel &screen : filtered.screens()) screen.enabled = false;
  QVERIFY(!PackCompiler::compile(filtered).ok());

  WidgetModel extra = project.screens().first().widgets.first();
  while (project.screens().first().widgets.size() <= int(TM_MAX_WIDGETS_PER_SCREEN)) {
    extra.id = QString::number(project.screens().first().widgets.size());
    project.screens().first().widgets << extra;
  }
  const PackCompileResult tooMany = PackCompiler::compile(project);
  QVERIFY(!tooMany.ok());
}

void PcMonitorTests::firmwareParserTransactionsButtonsAndFuzz() {
  ProjectModel project;
  const PackCompileResult compiled = PackCompiler::compile(project);
  QVERIFY2(compiled.ok(), qPrintable(compiled.error));

  monitor_init();
  tm_packet_t response = sendAndReceive(packet(TM_MSG_HELLO, 1));
  QCOMPARE(response.type, quint8(TM_MSG_CAPABILITIES));
  QCOMPARE(response.sequence, quint8(1));

  tm_packet_t corrupt = packet(TM_MSG_PING, 2, "crc");
  corrupt.payload[0] ^= 1;
  response = sendAndReceive(corrupt);
  QCOMPARE(response.type, quint8(TM_MSG_NACK));
  QCOMPARE(response.payload[0], quint8(TM_ERROR_BAD_CRC));

  const quint16 transaction = 0x3142;
  const quint32 size = quint32(compiled.data.size());
  const quint32 crc = tm_crc32(
      reinterpret_cast<const uint8_t *>(compiled.data.constData()), size);
  QByteArray begin;
  begin.append(reinterpret_cast<const char *>(&transaction), sizeof(transaction));
  begin.append(reinterpret_cast<const char *>(&size), sizeof(size));
  begin.append(reinterpret_cast<const char *>(&crc), sizeof(crc));
  response = sendAndReceive(packet(TM_MSG_PACK_BEGIN, 3, begin));
  QCOMPARE(response.type, quint8(TM_MSG_ACK));

  quint8 sequence = 4;
  for (quint32 offset = 0; offset < size; offset += 46) {
    QByteArray data;
    data.append(reinterpret_cast<const char *>(&transaction), sizeof(transaction));
    data.append(reinterpret_cast<const char *>(&offset), sizeof(offset));
    data.append(compiled.data.mid(offset, 46));
    response = sendAndReceive(packet(TM_MSG_PACK_DATA, sequence++, data));
    QCOMPARE(response.type, quint8(TM_MSG_ACK));
  }
  QByteArray commit(reinterpret_cast<const char *>(&transaction), sizeof(transaction));
  response = sendAndReceive(packet(TM_MSG_PACK_COMMIT, sequence++, commit));
  QCOMPARE(response.type, quint8(TM_MSG_ACK));
  QVERIFY(monitor_pack_active());
  QCOMPARE(monitor_current_page(), quint8(0));

  monitor_button(TM_BUTTON_RIGHT, TM_GESTURE_SHORT);
  QCOMPARE(monitor_current_page(), quint8(1));
  monitor_button(TM_BUTTON_LEFT, TM_GESTURE_SHORT);
  QCOMPARE(monitor_current_page(), quint8(0));

  tm_metric_entry_t metric{0, TM_STATUS_VALID, -2, 7550};
  QByteArray metricData(reinterpret_cast<const char *>(&metric), sizeof(metric));
  test_set_timer_ms(100);
  response = sendAndReceive(packet(TM_MSG_METRICS, sequence++, metricData));
  QCOMPARE(response.type, quint8(TM_MSG_ACK));
  QCOMPARE(monitor_metric(0)->value, qint32(7550));
  monitor_tick(5201);
  QVERIFY((monitor_metric(0)->status & TM_STATUS_STALE) != 0);

  response = sendAndReceive(packet(TM_MSG_REBOOT_BOOTLOADER, sequence++));
  QCOMPARE(response.type, quint8(TM_MSG_ACK));
  QVERIFY(monitor_reboot_requested());

  monitor_init();
  response = sendAndReceive(packet(TM_MSG_PACK_BEGIN, sequence++, begin));
  QByteArray partial;
  const quint32 zeroOffset = 0;
  partial.append(reinterpret_cast<const char *>(&transaction), sizeof(transaction));
  partial.append(reinterpret_cast<const char *>(&zeroOffset), sizeof(zeroOffset));
  partial.append(compiled.data.left(10));
  response = sendAndReceive(packet(TM_MSG_PACK_DATA, sequence++, partial));
  response = sendAndReceive(packet(TM_MSG_PACK_COMMIT, sequence++, commit));
  QCOMPARE(response.type, quint8(TM_MSG_NACK));
  QVERIFY(!monitor_pack_active());

  QRandomGenerator random(0x544d0001);
  for (int i = 0; i < 2000; ++i) {
    tm_packet_t fuzz{};
    for (size_t byte = 0; byte < sizeof(fuzz); ++byte)
      reinterpret_cast<quint8 *>(&fuzz)[byte] = quint8(random.generate());
    monitor_handle_packet(reinterpret_cast<const uint8_t *>(&fuzz));
    tm_packet_t discarded{};
    while (monitor_take_packet(reinterpret_cast<uint8_t *>(&discarded))) {}
  }
}

QTEST_MAIN(PcMonitorTests)
#include "pcmonitor_tests.moc"
