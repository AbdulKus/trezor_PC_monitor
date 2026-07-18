#include "packcompiler.h"

#include <QFontMetrics>
#include <QPainter>
#include <QSet>

#include <algorithm>
#include <cstring>

#include "protocol.h"
#include "pixelfont.h"

namespace {
template <typename T>
void appendStruct(QByteArray &output, const T &value) {
  output.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

template <typename T>
void appendVector(QByteArray &output, const QVector<T> &values) {
  if (!values.isEmpty())
    output.append(reinterpret_cast<const char *>(values.constData()),
                  values.size() * qsizetype(sizeof(T)));
}

QString fontKey(const QFont &font) {
  return QStringLiteral("%1|%2|%3")
      .arg(font.family())
      .arg(font.pixelSize() > 0 ? font.pixelSize() : font.pointSize())
      .arg(font.bold());
}

QByteArray utf8z(const QString &value) {
  QByteArray result = value.toUtf8();
  result.append('\0');
  return result;
}

tm_action_t compileBinding(const ButtonBinding &binding) {
  return tm_action_t{quint8(binding.type), binding.target, binding.hostActionId};
}

QSize mediaSize(const WidgetModel &widget) {
  const int x = qBound(0, widget.geometry.x(), 127);
  const int y = qBound(0, widget.geometry.y(), 63);
  return QSize(qBound(1, widget.geometry.width(), 128 - x),
               qBound(1, widget.geometry.height(), 64 - y));
}

QString mediaKey(int resourceIndex, const QSize &size) {
  return QStringLiteral("%1:%2x%3")
      .arg(resourceIndex)
      .arg(size.width())
      .arg(size.height());
}

struct FontBuild {
  QFont font;
  QSet<uint> characters;
  QVector<tm_glyph_t> glyphs;
  QVector<QByteArray> glyphData;
  tm_font_t record{};
};

struct AnimationBuild {
  tm_animation_t record{};
  QVector<tm_animation_frame_t> frames;
  QVector<QByteArray> encodedFrames;
};
}  // namespace

QByteArray PackCompiler::monochromeBits(const QImage &source, QSize size,
                                        int threshold, const QString &dither,
                                        bool inverted) {
  QImage image = source.convertToFormat(QImage::Format_ARGB32)
                     .scaled(size, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
  int stride = (size.width() + 7) / 8;
  QByteArray result(stride * size.height(), '\0');
  QVector<double> errors(size.width() + 2);
  QVector<double> nextErrors(size.width() + 2);
  static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6},
                                  {3, 11, 1, 9}, {15, 7, 13, 5}};
  for (int y = 0; y < size.height(); y++) {
    std::fill(nextErrors.begin(), nextErrors.end(), 0.0);
    for (int x = 0; x < size.width(); x++) {
      QRgb rgba = image.pixel(x, y);
      int alpha = qAlpha(rgba);
      double luminance = (qRed(rgba) * 299 + qGreen(rgba) * 587 +
                          qBlue(rgba) * 114) /
                         1000.0;
      if (alpha < 32) luminance = 255;
      int localThreshold = threshold;
      if (dither == "ordered")
        localThreshold += (bayer[y & 3][x & 3] - 8) * 8;
      if (dither == "floyd") luminance += errors[x + 1];
      bool black = luminance < localThreshold;
      bool on = inverted ? !black : black;
      if (on) result[y * stride + x / 8] |= char(0x80 >> (x & 7));
      if (dither == "floyd") {
        double quantized = black ? 0.0 : 255.0;
        double error = luminance - quantized;
        errors[x + 2] += error * 7.0 / 16.0;
        nextErrors[x] += error * 3.0 / 16.0;
        nextErrors[x + 1] += error * 5.0 / 16.0;
        nextErrors[x + 2] += error / 16.0;
      }
    }
    errors.swap(nextErrors);
  }
  return result;
}

QByteArray PackCompiler::rle(const QByteArray &bytes) {
  QByteArray output;
  for (qsizetype i = 0; i < bytes.size();) {
    unsigned char value = static_cast<unsigned char>(bytes[i]);
    int count = 1;
    while (i + count < bytes.size() && count < 255 &&
           static_cast<unsigned char>(bytes[i + count]) == value)
      count++;
    output.append(char(count));
    output.append(char(value));
    i += count;
  }
  return output;
}

PackCompileResult PackCompiler::compile(const ProjectModel &project) {
  PackCompileResult result;
  if (project.screens().isEmpty() ||
      project.screens().size() > int(TM_MAX_SCREENS)) {
    result.error = QStringLiteral("Количество экранов должно быть от 1 до 8");
    return result;
  }
  QVector<const ScreenModel *> enabledScreens;
  QVector<int> pageMap(project.screens().size(), -1);
  for (int i = 0; i < project.screens().size(); ++i) {
    if (!project.screens()[i].enabled) continue;
    pageMap[i] = enabledScreens.size();
    enabledScreens << &project.screens()[i];
  }
  if (enabledScreens.isEmpty()) {
    result.error = QStringLiteral("Включите хотя бы один экран");
    return result;
  }

  QHash<QString, int> fontIds;
  QVector<FontBuild> fonts;
  QSet<QString> metricNames;
  int widgetCount = 0;
  for (const ScreenModel *screenPointer : enabledScreens) {
    const ScreenModel &screen = *screenPointer;
    if (screen.widgets.size() > int(TM_MAX_WIDGETS_PER_SCREEN)) {
      result.error = QStringLiteral("На экране «%1» больше 24 виджетов")
                         .arg(screen.name);
      return result;
    }
    widgetCount += screen.widgets.size();
    for (const WidgetModel &widget : screen.widgets) {
      if (!widget.metric.isEmpty() && widget.type != TM_WIDGET_STATIC_TEXT &&
          widget.type != TM_WIDGET_IMAGE && widget.type != TM_WIDGET_ANIMATION &&
          widget.type != TM_WIDGET_FRAME && widget.type != TM_WIDGET_LINE)
        metricNames.insert(widget.metric);
      if (widget.type == TM_WIDGET_STATIC_TEXT ||
          widget.type == TM_WIDGET_DYNAMIC_TEXT ||
          widget.type == TM_WIDGET_WARNING) {
        QString key = fontKey(widget.font);
        int fontId = fontIds.value(key, -1);
        if (fontId < 0) {
          if (fonts.size() >= int(TM_MAX_FONTS)) {
            result.error = QStringLiteral("Использовано больше 8 шрифтов");
            return result;
          }
          fontId = fonts.size();
          fontIds.insert(key, fontId);
          FontBuild build;
          build.font = widget.font;
          if (build.font.pixelSize() <= 0)
            build.font.setPixelSize(qMax(6, build.font.pointSize()));
          fonts << build;
        }
        QString glyphText = widget.text + QStringLiteral("0123456789.-+% CMWHzGB/:");
        for (uint codepoint : glyphText.toUcs4()) fonts[fontId].characters.insert(codepoint);
        fonts[fontId].characters.insert('?');
        fonts[fontId].characters.insert(' ');
      }
    }
  }
  QStringList sortedMetrics = metricNames.values();
  sortedMetrics.sort();
  if (sortedMetrics.size() > int(TM_MAX_CHANNELS)) {
    result.error = QStringLiteral("Использовано больше 64 каналов телеметрии");
    return result;
  }
  for (int i = 0; i < sortedMetrics.size(); i++) result.channels[sortedMetrics[i]] = i;

  QVector<tm_image_t> images;
  QVector<QByteArray> imageData;
  QVector<AnimationBuild> animations;
  QHash<QString, int> imageMap;
  QHash<QString, int> animationMap;
  // Assets are compiled only for enabled screen placements and immediately at
  // their final OLED size. A 16x12 GIF therefore occupies 16x12 worth of data,
  // not a hidden 128x64 source that firmware scales at runtime.
  for (const ScreenModel *screenPointer : enabledScreens) {
    for (const WidgetModel &widget : screenPointer->widgets) {
      const bool imageWidget = widget.type == TM_WIDGET_IMAGE ||
                               widget.type == TM_WIDGET_CONDITIONAL_ICON;
      const bool animationWidget = widget.type == TM_WIDGET_ANIMATION;
      if (!imageWidget && !animationWidget) continue;
      if (widget.resourceIndex < 0 ||
          widget.resourceIndex >= project.resources().size()) {
        result.error = QStringLiteral("Для элемента «%1» не выбран ресурс")
                           .arg(widgetTypeName(widget.type));
        return result;
      }
      const ResourceModel &resource = project.resources()[widget.resourceIndex];
      if (resource.frames.isEmpty()) {
        result.error = QStringLiteral("Ресурс «%1» не содержит кадров")
                           .arg(resource.name);
        return result;
      }
      if (animationWidget != (resource.frames.size() > 1)) {
        result.error = animationWidget
                           ? QStringLiteral("Для анимации выбран не GIF-ресурс")
                           : QStringLiteral("Для изображения выбран GIF-ресурс");
        return result;
      }
      const QSize size = mediaSize(widget);
      const QString key = mediaKey(widget.resourceIndex, size);
      if (imageWidget) {
        if (imageMap.contains(key)) continue;
      if (images.size() >= int(TM_MAX_IMAGES)) {
        result.error = QStringLiteral("Больше 64 изображений");
        return result;
      }
      QByteArray bits = monochromeBits(resource.frames.first(), size,
                                       resource.threshold, resource.dither,
                                       resource.inverted);
      QByteArray encoded = rle(bits);
      tm_image_t image{quint8(size.width()), quint8(size.height()), 1, 0, 0,
                       quint32(encoded.size())};
        imageMap.insert(key, images.size());
      images << image;
      imageData << encoded;
      } else {
        if (animationMap.contains(key)) continue;
      if (animations.size() >= int(TM_MAX_ANIMATIONS)) {
        result.error = QStringLiteral("Больше 16 анимаций");
        return result;
      }
      AnimationBuild build;
      int minDuration = resource.durationsMs.isEmpty()
                            ? 100
                            : *std::min_element(resource.durationsMs.begin(),
                                                resource.durationsMs.end());
      int fps = qBound(1, 1000 / qMax(1, minDuration), 12);
      build.record = tm_animation_t{quint8(size.width()), quint8(size.height()),
                                    quint8(qMin(resource.frames.size(), 255)),
                                    quint8(fps), 0};
      QByteArray previous((size.width() + 7) / 8 * size.height(), '\0');
      for (int f = 0; f < build.record.frame_count; f++) {
        QByteArray current = monochromeBits(resource.frames[f], size,
                                            resource.threshold, resource.dither,
                                            resource.inverted);
        QByteArray delta(current.size(), '\0');
        for (qsizetype i = 0; i < current.size(); i++)
          delta[i] = current[i] ^ previous[i];
        QByteArray encoded = rle(delta);
        int duration = f < resource.durationsMs.size()
                           ? resource.durationsMs[f]
                           : 1000 / fps;
        build.frames << tm_animation_frame_t{0, quint16(encoded.size()),
                                              quint16(qBound(20, duration, 5000))};
        build.encodedFrames << encoded;
        previous = current;
      }
        animationMap.insert(key, animations.size());
      animations << build;
      }
    }
  }

  for (FontBuild &font : fonts) {
    if (PixelFonts::isPixelFont(font.font.family())) {
      const PixelFontData pixelFont = PixelFonts::load(font.font.family());
      if (!pixelFont.valid()) {
        result.error = QStringLiteral("Не удалось загрузить пиксельный шрифт %1")
                           .arg(font.font.family());
        return result;
      }
      font.record.height = quint8(pixelFont.height);
      font.record.baseline = quint8(pixelFont.baseline);
      QList<uint> characters = font.characters.values();
      std::sort(characters.begin(), characters.end());
      for (uint codepoint : characters) {
        const PixelGlyphData glyphData = pixelFont.glyphs.value(
            codepoint, pixelFont.glyphs.value(uint('?')));
        if (glyphData.bits.isEmpty()) continue;
        tm_glyph_t glyph{codepoint,
                         quint8(glyphData.width),
                         quint8(glyphData.advance),
                         0,
                         qint8(pixelFont.baseline),
                         quint16(glyphData.bits.size()),
                         0,
                         0};
        font.glyphs << glyph;
        font.glyphData << glyphData.bits;
      }
      font.record.glyph_count = quint16(font.glyphs.size());
      continue;
    }
    QFontMetrics metrics(font.font);
    QList<uint> characters = font.characters.values();
    std::sort(characters.begin(), characters.end());
    int height = qBound(1, metrics.height(), 32);
    font.record.height = quint8(height);
    font.record.baseline = quint8(qMin(metrics.ascent(), height));
    for (uint codepoint : characters) {
      QString text = QString::fromUcs4(&codepoint, 1);
      int width = qBound(1, metrics.horizontalAdvance(text), 32);
      QImage image(width, height, QImage::Format_ARGB32);
      image.fill(Qt::transparent);
      QPainter painter(&image);
      painter.setPen(Qt::black);
      painter.setFont(font.font);
      painter.setRenderHint(QPainter::Antialiasing, false);
      painter.setRenderHint(QPainter::TextAntialiasing, false);
      painter.drawText(0, metrics.ascent(), text);
      painter.end();
      QByteArray bits = monochromeBits(image, image.size(), 128, "none", false);
      tm_glyph_t glyph{codepoint,
                       quint8(width),
                       quint8(qBound(1, metrics.horizontalAdvance(text), 32)),
                       0,
                       qint8(font.record.baseline),
                       quint16(bits.size()),
                       0,
                       0};
      font.glyphs << glyph;
      font.glyphData << bits;
    }
    font.record.glyph_count = quint16(font.glyphs.size());
  }

  quint32 screensOffset = sizeof(tm_pack_header_t);
  quint32 widgetsOffset =
      screensOffset + enabledScreens.size() * sizeof(tm_screen_t);
  quint32 fontsOffset = widgetsOffset + widgetCount * sizeof(tm_widget_t);
  quint32 imagesOffset = fontsOffset + fonts.size() * sizeof(tm_font_t);
  quint32 animationsOffset = imagesOffset + images.size() * sizeof(tm_image_t);
  quint32 variableOffset =
      animationsOffset + animations.size() * sizeof(tm_animation_t);
  for (FontBuild &font : fonts) {
    font.record.glyphs_offset = variableOffset;
    variableOffset += font.glyphs.size() * sizeof(tm_glyph_t);
  }
  for (AnimationBuild &animation : animations) {
    animation.record.frames_offset = variableOffset;
    variableOffset += animation.frames.size() * sizeof(tm_animation_frame_t);
  }
  quint32 resourcesOffset = variableOffset;
  QByteArray resources;
  auto addResource = [&](const QByteArray &data) {
    quint32 offset = resourcesOffset + resources.size();
    resources.append(data);
    return offset;
  };

  QVector<tm_screen_t> screenRecords;
  QVector<tm_widget_t> widgetRecords;
  quint16 firstWidget = 0;
  auto mappedBinding = [&](const ButtonBinding &binding) {
    if (binding.type != TM_ACTION_GO_TO_PAGE)
      return compileBinding(binding);
    if (binding.target >= pageMap.size() || pageMap[binding.target] < 0) {
      result.warnings << QStringLiteral(
          "Переход на выключенный экран отключён при сборке");
      return tm_action_t{TM_ACTION_NONE, 0, 0};
    }
    return tm_action_t{TM_ACTION_GO_TO_PAGE,
                       quint8(pageMap[binding.target]), binding.hostActionId};
  };
  for (const ScreenModel *screenPointer : enabledScreens) {
    const ScreenModel &screen = *screenPointer;
    tm_screen_t record{};
    record.name_offset = addResource(utf8z(screen.name));
    record.first_widget = firstWidget;
    record.widget_count = quint8(screen.widgets.size());
    record.left_short = mappedBinding(screen.leftShort);
    record.left_long = mappedBinding(screen.leftLong);
    record.right_short = mappedBinding(screen.rightShort);
    record.right_long = mappedBinding(screen.rightLong);
    screenRecords << record;
    for (const WidgetModel &widget : screen.widgets) {
      tm_widget_t output{};
      output.type = quint8(widget.type);
      output.flags = widget.inverted ? 1 : 0;
      output.x = quint8(qBound(0, widget.geometry.x(), 127));
      output.y = quint8(qBound(0, widget.geometry.y(), 63));
      output.width = quint8(qBound(1, widget.geometry.width(), 128 - output.x));
      output.height = quint8(qBound(1, widget.geometry.height(), 64 - output.y));
      output.channel_id = result.channels.value(widget.metric, 0);
      // Firmware gauges use centi-units internally. The editor deliberately
      // exposes normal sensor units (100 %, 16384 MiB, 90 C, ...).
      output.minimum = qint32(qBound(qint64(INT32_MIN),
                                    qint64(widget.minimum) * 100,
                                    qint64(INT32_MAX)));
      output.maximum = qint32(qBound(qint64(INT32_MIN),
                                    qint64(widget.maximum) * 100,
                                    qint64(INT32_MAX)));
      output.precision = widget.precision;
      output.arg0 = widget.arg0;
      output.arg1 = widget.arg1;
      output.resource_offset = addResource(utf8z(widget.text));
      output.font_id = 0xff;
      output.resource_id = 0xffff;
      if (widget.type == TM_WIDGET_STATIC_TEXT ||
          widget.type == TM_WIDGET_DYNAMIC_TEXT ||
          widget.type == TM_WIDGET_WARNING)
        output.font_id = quint8(fontIds.value(fontKey(widget.font), 0));
      const bool mediaWidget = widget.type == TM_WIDGET_IMAGE ||
                               widget.type == TM_WIDGET_ANIMATION ||
                               widget.type == TM_WIDGET_CONDITIONAL_ICON;
      if (mediaWidget) {
        if (widget.resourceIndex < 0 ||
            widget.resourceIndex >= project.resources().size()) {
          result.error = QStringLiteral("Для элемента «%1» не выбран ресурс")
                             .arg(widgetTypeName(widget.type));
          return result;
        }
        const QString key = mediaKey(widget.resourceIndex,
                                     QSize(output.width, output.height));
        const int mapped = widget.type == TM_WIDGET_ANIMATION
                               ? animationMap.value(key, -1)
                               : imageMap.value(key, -1);
        if (mapped < 0) {
          result.error = widget.type == TM_WIDGET_ANIMATION
                             ? QStringLiteral("Для анимации выбран не GIF-ресурс")
                             : QStringLiteral("Для изображения выбран GIF-ресурс");
          return result;
        }
        output.resource_id = quint16(mapped);
      }
      widgetRecords << output;
      firstWidget++;
    }
  }

  for (int i = 0; i < images.size(); i++)
    images[i].data_offset = addResource(imageData[i]);
  for (FontBuild &font : fonts) {
    for (int g = 0; g < font.glyphs.size(); g++)
      font.glyphs[g].data_offset = addResource(font.glyphData[g]);
  }
  for (AnimationBuild &animation : animations) {
    for (int f = 0; f < animation.frames.size(); f++)
      animation.frames[f].data_offset = addResource(animation.encodedFrames[f]);
  }

  tm_pack_header_t header{};
  memcpy(header.magic, "TMPK", 4);
  header.version = TM_PACK_VERSION;
  header.header_size = sizeof(header);
  header.screen_count = quint8(screenRecords.size());
  header.channel_count = quint8(result.channels.size());
  header.font_count = quint8(fonts.size());
  header.image_count = quint8(images.size());
  header.animation_count = quint8(animations.size());
  header.widget_count = quint16(widgetRecords.size());
  header.screens_offset = screensOffset;
  header.widgets_offset = widgetsOffset;
  header.fonts_offset = fontsOffset;
  header.images_offset = imagesOffset;
  header.animations_offset = animationsOffset;
  header.resources_offset = resourcesOffset;
  header.resources_size = resources.size();
  header.total_size = resourcesOffset + resources.size();
  if (header.total_size > TM_MAX_PACK_SIZE) {
    result.error = QStringLiteral("Пакет занимает %1 байт, лимит — %2")
                       .arg(header.total_size)
                       .arg(TM_MAX_PACK_SIZE);
    return result;
  }

  QByteArray output;
  appendStruct(output, header);
  appendVector(output, screenRecords);
  appendVector(output, widgetRecords);
  for (const FontBuild &font : fonts) appendStruct(output, font.record);
  appendVector(output, images);
  for (const AnimationBuild &animation : animations)
    appendStruct(output, animation.record);
  for (const FontBuild &font : fonts) appendVector(output, font.glyphs);
  for (const AnimationBuild &animation : animations)
    appendVector(output, animation.frames);
  output.append(resources);
  if (output.size() != int(header.total_size)) {
    result.error = QStringLiteral("Внутренняя ошибка компоновки пакета");
    return result;
  }
  auto *outputHeader = reinterpret_cast<tm_pack_header_t *>(output.data());
  outputHeader->crc32 = 0;
  outputHeader->crc32 = tm_crc32(
      reinterpret_cast<const uint8_t *>(output.constData()), output.size());
  result.data = output;
  return result;
}
