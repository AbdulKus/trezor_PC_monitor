#include "designcanvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QtMath>

#include <algorithm>

#include "pixelfont.h"
#include "packcompiler.h"

namespace {
constexpr int kSparklineSamples = 16;

QString autoRangeKey(const WidgetModel &widget) {
  if (!widget.id.isEmpty()) return widget.id;
  return QStringLiteral("%1:%2:%3:%4:%5")
      .arg(widget.metric)
      .arg(widget.geometry.x())
      .arg(widget.geometry.y())
      .arg(widget.geometry.width())
      .arg(widget.geometry.height());
}

double niceAutoMaximum(double peak) {
  if (!qIsFinite(peak) || peak <= 0.0) return 1.0;
  const double padded = peak * 1.15;
  const double magnitude = qPow(10.0, qFloor(qLn(padded) / qLn(10.0)));
  const double step = qMax(magnitude / 10.0, 0.000001);
  return qCeil(padded / step) * step;
}
}  // namespace

DesignCanvas::DesignCanvas(QWidget *parent) : QWidget(parent) {
  setMinimumSize(540, 350);
  setFocusPolicy(Qt::StrongFocus);
  setToolTip(QStringLiteral(
      "Выберите элемент и двигайте его стрелками с шагом 1 пиксель."));
  setMouseTracking(true);
  animationClock_.start();
  animationTimer_.setInterval(83);  // device animations are capped at 12 FPS
  connect(&animationTimer_, &QTimer::timeout, this,
          qOverload<>(&DesignCanvas::update));
  animationTimer_.start();
}
void DesignCanvas::setProject(ProjectModel *project) {
  project_ = project;
  selected_ = -1;
  update();
}
void DesignCanvas::setSamples(const QHash<QString, MetricSample> &samples) {
  samples_ = samples;
  QSet<QString> updatedMetrics;
  for (auto it = samples.constBegin(); it != samples.constEnd(); ++it) {
    if (!it.value().valid) continue;
    QVector<double> &values = history_[it.key()];
    values << it.value().value;
    while (values.size() > kSparklineSamples) values.removeFirst();
    updatedMetrics.insert(it.key());
  }
  if (project_) {
    for (const ScreenModel &screen : project_->screens()) {
      for (const WidgetModel &widget : screen.widgets) {
        if (widget.type != TM_WIDGET_SPARKLINE || !widget.autoRange ||
            !updatedMetrics.contains(widget.metric))
          continue;
        const QVector<double> &values = history_[widget.metric];
        const double peak = values.isEmpty()
                                ? 0.0
                                : *std::max_element(values.cbegin(), values.cend());
        const double target = niceAutoMaximum(qMax(0.0, peak));
        const QString key = autoRangeKey(widget);
        const double previous = autoRangeTargets_.value(key, target);
        const bool changed = autoRangeTargets_.contains(key) &&
                             !qFuzzyCompare(previous + 1.0, target + 1.0);
        autoRangeTargets_[key] = target;
        QVector<bool> &markers = autoRangeMarkers_[key];
        markers << changed;
        while (markers.size() > values.size()) markers.removeFirst();
        while (markers.size() < values.size()) markers.prepend(false);
      }
    }
  }
  update();
}
void DesignCanvas::setSelectedWidget(int index) {
  selected_ = index;
  update();
}

QRect DesignCanvas::canvasRect() const {
  int scale = qMax(1, qMin((width() - 30) / 128, (height() - 30) / 64));
  QSize size(128 * scale, 64 * scale);
  return QRect((width() - size.width()) / 2, (height() - size.height()) / 2,
               size.width(), size.height());
}
QPoint DesignCanvas::devicePoint(const QPoint &position) const {
  QRect area = canvasRect();
  int scale = area.width() / 128;
  return QPoint(qBound(0, (position.x() - area.x()) / scale, 127),
                qBound(0, (position.y() - area.y()) / scale, 63));
}

void DesignCanvas::paintWidget(QPainter &p, const WidgetModel &widget) {
  QRect r = widget.geometry;
  MetricSample sample = samples_.value(widget.metric);
  double ratio = widget.maximum > widget.minimum
                     ? qBound(0.0, (sample.value - widget.minimum) /
                                       double(widget.maximum - widget.minimum),
                              1.0)
                     : 0.0;
  p.setPen(Qt::white);
  p.setBrush(Qt::NoBrush);
  switch (widget.type) {
    case TM_WIDGET_STATIC_TEXT:
    case TM_WIDGET_DYNAMIC_TEXT:
    case TM_WIDGET_WARNING: {
      QString text = widget.text;
      text.replace("{v}", sample.valid ? QString::number(sample.value, 'f', widget.precision)
                                        : QStringLiteral("--"));
      if (PixelFonts::isPixelFont(widget.font.family())) {
        PixelFonts::draw(p, r.x(), r.y(), text, widget.font.family());
      } else {
        QFont font = widget.font;
        font.setPixelSize(qMax(5, font.pixelSize() > 0 ? font.pixelSize()
                                                      : font.pointSize()));
        font.setStyleStrategy(QFont::NoAntialias);
        p.setFont(font);
        p.drawText(r, Qt::AlignLeft | Qt::AlignTop, text);
      }
      break;
    }
    case TM_WIDGET_BAR_HORIZONTAL:
      p.drawRect(r.adjusted(0, 0, -1, -1));
      p.fillRect(QRect(r.x() + 1, r.y() + 1,
                       int((r.width() - 2) * ratio), r.height() - 2), Qt::white);
      break;
    case TM_WIDGET_BAR_VERTICAL: {
      p.drawRect(r.adjusted(0, 0, -1, -1));
      int h = int((r.height() - 2) * ratio);
      p.fillRect(r.x() + 1, r.bottom() - h, r.width() - 2, h, Qt::white);
      break;
    }
    case TM_WIDGET_SEGMENTS: {
      int count = widget.arg0 ? widget.arg0 : 5;
      count = qBound(1, count, r.width());
      const int gap = count > 1 ? 1 : 0;
      const int available = qMax(1, r.width() - gap * (count - 1));
      const int base = available / count;
      const int remainder = available % count;
      int cursor = r.x();
      const int lit = qBound(0, int(ratio * count + 0.5), count);
      for (int i = 0; i < count; i++) {
        const int width = base + (i < remainder ? 1 : 0);
        QRect segment(cursor, r.y(), width, r.height());
        if (i < lit) p.fillRect(segment, Qt::white);
        else p.drawRect(segment.adjusted(0, 0, -1, -1));
        cursor += width + gap;
      }
      break;
    }
    case TM_WIDGET_RING:
      p.drawArc(r.adjusted(1, 1, -1, -1), 90 * 16,
                int(-ratio * 360 * 16));
      break;
    case TM_WIDGET_NEEDLE:
      p.drawRect(r.adjusted(0, 0, -1, -1));
      p.drawLine(r.center().x(), r.bottom(),
                 r.left() + int(ratio * r.width()), r.top());
      break;
    case TM_WIDGET_SPARKLINE: {
      const QVector<double> values = history_.value(widget.metric);
      double rangeMinimum = widget.minimum;
      double rangeMaximum = widget.maximum;
      QVector<bool> scaleMarkers;
      if (widget.autoRange) {
        const QString key = autoRangeKey(widget);
        const double peak = values.isEmpty()
                                ? qMax(0.0, sample.value)
                                : qMax(0.0, *std::max_element(values.cbegin(),
                                                              values.cend()));
        const double target = autoRangeTargets_.value(key,
                                                       niceAutoMaximum(peak));
        double displayed = autoRangeMaximums_.value(key, target);
        const double factor = target > displayed ? 0.42 : 0.035;
        displayed += (target - displayed) * factor;
        if (qAbs(target - displayed) < qMax(0.000001, target * 0.002))
          displayed = target;
        displayed = qMax(0.000001, displayed);
        autoRangeMaximums_[key] = displayed;
        rangeMinimum = 0.0;
        rangeMaximum = displayed;
        scaleMarkers = autoRangeMarkers_.value(key);
      }
      if (values.size() < 2) {
        const double value = values.isEmpty() ? sample.value : values.constLast();
        const double normalized = rangeMaximum > rangeMinimum
            ? qBound(0.0, (value - rangeMinimum) /
                              (rangeMaximum - rangeMinimum), 1.0)
            : 0.0;
        const int y = r.bottom() -
                      int(normalized * qMax(0, r.height() - 1));
        p.drawLine(r.left(), y, r.right(), y);
        break;
      }
      if (widget.autoRange) {
        while (scaleMarkers.size() < values.size()) scaleMarkers.prepend(false);
        const int markerOffset = scaleMarkers.size() - values.size();
        for (int i = 0; i < values.size(); ++i) {
          if (!scaleMarkers.value(i + markerOffset)) continue;
          const int x = r.left() + i * qMax(0, r.width() - 1) /
                                     qMax(1, values.size() - 1);
          for (int y = r.top(); y <= r.bottom(); y += 3) p.drawPoint(x, y);
        }
      }
      auto point = [&](int index) {
        const double normalized = rangeMaximum > rangeMinimum
            ? qBound(0.0, (values[index] - rangeMinimum) /
                              (rangeMaximum - rangeMinimum), 1.0)
            : 0.0;
        return QPoint(r.left() + index * qMax(0, r.width() - 1) /
                                     qMax(1, values.size() - 1),
                      r.bottom() - int(normalized * qMax(0, r.height() - 1)));
      };
      for (int i = 1; i < values.size(); ++i) p.drawLine(point(i - 1), point(i));
      break;
    }
    case TM_WIDGET_IMAGE:
    case TM_WIDGET_ANIMATION:
    case TM_WIDGET_CONDITIONAL_ICON:
      if (project_ && widget.resourceIndex >= 0 &&
          widget.resourceIndex < project_->resources().size() &&
          !project_->resources()[widget.resourceIndex].frames.isEmpty()) {
        const ResourceModel &resource =
            project_->resources()[widget.resourceIndex];
        int frameIndex = 0;
        if (widget.type == TM_WIDGET_ANIMATION && resource.frames.size() > 1) {
          int totalDuration = 0;
          for (int i = 0; i < resource.frames.size(); ++i)
            totalDuration += i < resource.durationsMs.size()
                                 ? qMax(20, resource.durationsMs[i])
                                 : 100;
          int position = totalDuration > 0
                             ? int(animationClock_.elapsed() % totalDuration)
                             : 0;
          for (int i = 0; i < resource.frames.size(); ++i) {
            const int duration = i < resource.durationsMs.size()
                                     ? qMax(20, resource.durationsMs[i])
                                     : 100;
            if (position < duration) {
              frameIndex = i;
              break;
            }
            position -= duration;
          }
        }
        const QSize size = resource.frames.first().size().boundedTo(QSize(128, 64));
        const QByteArray bits = PackCompiler::monochromeBits(
            resource.frames[frameIndex], size, resource.threshold,
            resource.dither, resource.inverted);
        const int stride = (size.width() + 7) / 8;
        for (int y = 0; y < r.height(); ++y) {
          const int sourceY = y * size.height() / qMax(1, r.height());
          for (int x = 0; x < r.width(); ++x) {
            const int sourceX = x * size.width() / qMax(1, r.width());
            if (quint8(bits[sourceY * stride + sourceX / 8]) &
                (0x80 >> (sourceX & 7)))
              p.drawPoint(r.x() + x, r.y() + y);
          }
        }
      } else {
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.drawLine(r.topLeft(), r.bottomRight());
        p.drawLine(r.topRight(), r.bottomLeft());
      }
      break;
    case TM_WIDGET_FRAME:
      p.drawRect(r.adjusted(0, 0, -1, -1));
      break;
    case TM_WIDGET_LINE:
      p.drawLine(r.topLeft(), r.bottomRight());
      break;
    default:
      break;
  }
}

void DesignCanvas::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#111827"));
  QRect area = canvasRect();
  painter.fillRect(area, Qt::black);
  int scale = area.width() / 128;
  const bool hasScreen = project_ &&
                         project_->currentScreen() < project_->screens().size();
  if (pixelPerfect_) {
    QImage oled(128, 64, QImage::Format_ARGB32);
    oled.fill(Qt::black);
    QPainter devicePainter(&oled);
    devicePainter.setRenderHint(QPainter::Antialiasing, false);
    devicePainter.setRenderHint(QPainter::TextAntialiasing, false);
    devicePainter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (hasScreen) {
      const auto &widgets = project_->screens()[project_->currentScreen()].widgets;
      for (const WidgetModel &widget : widgets) paintWidget(devicePainter, widget);
    }
    devicePainter.end();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(area, oled);
  } else {
    painter.save();
    painter.translate(area.topLeft());
    painter.scale(scale, scale);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    if (hasScreen) {
      const auto &widgets = project_->screens()[project_->currentScreen()].widgets;
      for (const WidgetModel &widget : widgets) paintWidget(painter, widget);
    }
    painter.restore();
  }
  if (hasScreen) {
    const auto &widgets = project_->screens()[project_->currentScreen()].widgets;
    if (selected_ >= 0 && selected_ < widgets.size()) {
      QRect selected(area.x() + widgets[selected_].geometry.x() * scale,
                     area.y() + widgets[selected_].geometry.y() * scale,
                     widgets[selected_].geometry.width() * scale,
                     widgets[selected_].geometry.height() * scale);
      QPen selection(QColor("#38bdf8"), 2);
      selection.setStyle(Qt::DashLine);
      painter.setPen(selection);
      painter.drawRect(selected.adjusted(0, 0, -1, -1));
    }
  }
  if (pixelPerfect_ && scale >= 2) {
    painter.setPen(QColor(255, 255, 255, 18));
    for (int x = 1; x < 128; ++x)
      painter.drawLine(area.x() + x * scale, area.y(),
                       area.x() + x * scale, area.bottom());
    for (int y = 1; y < 64; ++y)
      painter.drawLine(area.x(), area.y() + y * scale,
                       area.right(), area.y() + y * scale);
  }
  painter.setPen(QPen(QColor("#475569"), 2));
  painter.drawRect(area.adjusted(-1, -1, 0, 0));
  painter.setPen(QColor("#cbd5e1"));
  painter.drawText(area.x(), area.y() - 8,
                   pixelPerfect_ ? "128 × 64 · PIXEL PREVIEW"
                                 : "128 × 64 · SMOOTH PREVIEW");
}

void DesignCanvas::keyPressEvent(QKeyEvent *event) {
  QPoint delta;
  switch (event->key()) {
    case Qt::Key_Left: delta = QPoint(-1, 0); break;
    case Qt::Key_Right: delta = QPoint(1, 0); break;
    case Qt::Key_Up: delta = QPoint(0, -1); break;
    case Qt::Key_Down: delta = QPoint(0, 1); break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
  if (!project_ || selected_ < 0 ||
      project_->currentScreen() >= project_->screens().size()) {
    QWidget::keyPressEvent(event);
    return;
  }
  WidgetModel &widget =
      project_->screens()[project_->currentScreen()].widgets[selected_];
  QPoint position = widget.geometry.topLeft() + delta;
  position.setX(qBound(0, position.x(), 128 - widget.geometry.width()));
  position.setY(qBound(0, position.y(), 64 - widget.geometry.height()));
  if (position != widget.geometry.topLeft()) {
    widget.geometry.moveTopLeft(position);
    emit widgetGeometryChanged(selected_, widget.geometry);
    update();
  }
  event->accept();
}

void DesignCanvas::mousePressEvent(QMouseEvent *event) {
  setFocus(Qt::MouseFocusReason);
  if (!project_ || project_->currentScreen() >= project_->screens().size()) return;
  QPoint point = devicePoint(event->position().toPoint());
  const auto &widgets = project_->screens()[project_->currentScreen()].widgets;
  selected_ = -1;
  for (int i = widgets.size() - 1; i >= 0; i--)
    if (widgets[i].geometry.contains(point)) {
      selected_ = i;
      dragOffset_ = point - widgets[i].geometry.topLeft();
      dragging_ = true;
      break;
    }
  emit widgetSelected(selected_);
  update();
}
void DesignCanvas::mouseMoveEvent(QMouseEvent *event) {
  if (!dragging_ || !project_ || selected_ < 0) return;
  QPoint point = devicePoint(event->position().toPoint()) - dragOffset_;
  WidgetModel &widget =
      project_->screens()[project_->currentScreen()].widgets[selected_];
  point.setX(qBound(0, point.x(), 128 - widget.geometry.width()));
  point.setY(qBound(0, point.y(), 64 - widget.geometry.height()));
  widget.geometry.moveTopLeft(point);
  emit widgetGeometryChanged(selected_, widget.geometry);
  update();
}
void DesignCanvas::mouseReleaseEvent(QMouseEvent *) { dragging_ = false; }
