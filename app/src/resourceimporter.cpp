#include "resourceimporter.h"

#include <QFileInfo>
#include <QImageReader>
#include <QUuid>

bool ResourceImporter::suggestedInversion(const QImage &image) {
  if (image.isNull()) return false;
  const QImage rgba = image.convertToFormat(QImage::Format_ARGB32);
  const QPoint corners[] = {{0, 0}, {rgba.width() - 1, 0},
                            {0, rgba.height() - 1},
                            {rgba.width() - 1, rgba.height() - 1}};
  int sum = 0;
  for (const QPoint &point : corners) {
    const QRgb pixel = rgba.pixel(point);
    if (qAlpha(pixel) < 32) {
      sum += 255;
    } else {
      sum += (qRed(pixel) * 299 + qGreen(pixel) * 587 +
              qBlue(pixel) * 114) / 1000;
    }
  }
  // Dark opaque corners usually mean a dark GIF/video background. OLED pixels
  // should remain off there, so light foreground becomes the active color.
  return sum / 4 < 128;
}

bool ResourceImporter::importFile(const QString &path, ResourceModel *resource,
                                  QString *error) {
  if (!resource) {
    if (error) *error = QStringLiteral("Не передан объект ресурса");
    return false;
  }

  QImageReader reader(path);
  reader.setAutoTransform(true);
  reader.setDecideFormatFromContent(true);

  ResourceModel result;
  result.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  result.name = QFileInfo(path).completeBaseName();

  // read() advances animated formats. jumpToNextImage() after read() would
  // skip every other frame and makes some GIF plugins stop after frame zero.
  while (reader.canRead() && result.frames.size() < 255) {
    const QImage frame = reader.read();
    if (frame.isNull()) break;
    const int delay = reader.nextImageDelay();
    result.frames << frame;
    result.durationsMs << qBound(20, delay > 0 ? delay : 100, 5000);
  }

  if (result.frames.isEmpty()) {
    if (error) *error = reader.errorString();
    return false;
  }
  result.inverted = suggestedInversion(result.frames.first());
  *resource = std::move(result);
  return true;
}
