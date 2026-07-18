#include "resourcepreview.h"

#include <QPainter>

#include "packcompiler.h"

ResourcePreview::ResourcePreview(QWidget *parent) : QWidget(parent) {
  setMinimumSize(420, 230);
  clock_.start();
  timer_.setInterval(83);
  connect(&timer_, &QTimer::timeout, this, qOverload<>(&ResourcePreview::update));
  timer_.start();
}

void ResourcePreview::setResource(const ResourceModel &resource) {
  resource_ = resource;
  hasResource_ = !resource_.frames.isEmpty();
  clock_.restart();
  update();
}

void ResourcePreview::clearResource() {
  hasResource_ = false;
  resource_.frames.clear();
  update();
}

int ResourcePreview::currentFrame() const {
  if (resource_.frames.size() < 2) return 0;
  int total = 0;
  for (int i = 0; i < resource_.frames.size(); ++i)
    total += i < resource_.durationsMs.size()
                 ? qMax(20, resource_.durationsMs[i])
                 : 100;
  int position = total > 0 ? int(clock_.elapsed() % total) : 0;
  for (int i = 0; i < resource_.frames.size(); ++i) {
    const int duration = i < resource_.durationsMs.size()
                             ? qMax(20, resource_.durationsMs[i])
                             : 100;
    if (position < duration) return i;
    position -= duration;
  }
  return 0;
}

QImage ResourcePreview::oledImage() const {
  QImage oled(128, 64, QImage::Format_ARGB32);
  oled.fill(Qt::black);
  if (!hasResource_) return oled;
  const QSize size = resource_.frames.first().size().boundedTo(QSize(128, 64));
  const QByteArray bits = PackCompiler::monochromeBits(
      resource_.frames[currentFrame()], size, resource_.threshold,
      resource_.dither, resource_.inverted);
  const int stride = (size.width() + 7) / 8;
  const int originX = (128 - size.width()) / 2;
  const int originY = (64 - size.height()) / 2;
  for (int y = 0; y < size.height(); ++y)
    for (int x = 0; x < size.width(); ++x)
      if (quint8(bits[y * stride + x / 8]) & (0x80 >> (x & 7)))
        oled.setPixelColor(originX + x, originY + y, Qt::white);
  return oled;
}

void ResourcePreview::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(QStringLiteral("#0f172a")));
  const int scale = qMax(1, qMin((width() - 24) / 128,
                                 (height() - 42) / 64));
  const QRect oled((width() - 128 * scale) / 2,
                   (height() - 64 * scale) / 2 + 8,
                   128 * scale, 64 * scale);
  painter.fillRect(oled, Qt::black);

  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter.drawImage(oled, oledImage());

  painter.setPen(QColor(QStringLiteral("#475569")));
  painter.drawRect(oled.adjusted(-1, -1, 0, 0));
  painter.setPen(QColor(QStringLiteral("#cbd5e1")));
  painter.drawText(oled.x(), oled.y() - 8,
                   hasResource_
                       ? QStringLiteral("ЖИВОЕ 1-БИТНОЕ ПРЕВЬЮ · %1 КАДР.")
                             .arg(resource_.frames.size())
                       : QStringLiteral("ЖИВОЕ 1-БИТНОЕ ПРЕВЬЮ"));
}
