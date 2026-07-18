#include "iconeditor.h"

#include <QMouseEvent>
#include <QPainter>

IconEditor::IconEditor(QWidget *parent) : QWidget(parent) {
  image_ = QImage(32, 32, QImage::Format_Mono);
  image_.fill(1);
  setMinimumSize(320, 320);
}
void IconEditor::setImage(const QImage &image) {
  image_ = image.convertToFormat(QImage::Format_Mono);
  update();
}
void IconEditor::clear() {
  image_.fill(1);
  emit imageChanged(image_);
  update();
}
void IconEditor::invert() {
  image_.invertPixels();
  emit imageChanged(image_);
  update();
}
void IconEditor::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#808080"));
  int scale = qMax(1, qMin(width() / image_.width(), height() / image_.height()));
  QRect area((width() - image_.width() * scale) / 2,
             (height() - image_.height() * scale) / 2,
             image_.width() * scale, image_.height() * scale);
  painter.fillRect(area, Qt::white);
  for (int y = 0; y < image_.height(); y++)
    for (int x = 0; x < image_.width(); x++)
      if (qGray(image_.pixel(x, y)) < 128)
        painter.fillRect(area.x() + x * scale, area.y() + y * scale, scale,
                         scale, Qt::black);
  if (scale >= 5) {
    painter.setPen(QColor(190, 190, 190));
    for (int x = 0; x <= image_.width(); x++)
      painter.drawLine(area.x() + x * scale, area.y(), area.x() + x * scale,
                       area.bottom());
    for (int y = 0; y <= image_.height(); y++)
      painter.drawLine(area.x(), area.y() + y * scale, area.right(),
                       area.y() + y * scale);
  }
}
void IconEditor::apply(const QPoint &position) {
  int scale = qMax(1, qMin(width() / image_.width(), height() / image_.height()));
  QPoint origin((width() - image_.width() * scale) / 2,
                (height() - image_.height() * scale) / 2);
  QPoint pixel = (position - origin) / scale;
  if (pixel.x() < 0 || pixel.x() >= image_.width() || pixel.y() < 0 ||
      pixel.y() >= image_.height())
    return;
  image_.setPixel(pixel, erase_ ? 1 : 0);
  emit imageChanged(image_);
  update();
}
void IconEditor::mousePressEvent(QMouseEvent *event) {
  apply(event->position().toPoint());
}
void IconEditor::mouseMoveEvent(QMouseEvent *event) {
  if (event->buttons() & Qt::LeftButton) apply(event->position().toPoint());
}
