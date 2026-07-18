#pragma once

#include <QHash>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "projectmodel.h"
#include "telemetry.h"

class DesignCanvas : public QWidget {
  Q_OBJECT
 public:
  explicit DesignCanvas(QWidget *parent = nullptr);
  void setProject(ProjectModel *project);
  void setSamples(const QHash<QString, MetricSample> &samples);
  int selectedWidget() const { return selected_; }
  void setSelectedWidget(int index);
  void setPixelPerfect(bool enabled) { pixelPerfect_ = enabled; update(); }
 signals:
  void widgetSelected(int index);
  void widgetGeometryChanged(int index, const QRect &geometry);
 protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
 private:
  QRect canvasRect() const;
  QPoint devicePoint(const QPoint &position) const;
  void paintWidget(QPainter &painter, const WidgetModel &widget);
  ProjectModel *project_ = nullptr;
  QHash<QString, MetricSample> samples_;
  QHash<QString, QVector<double>> history_;
  int selected_ = -1;
  bool dragging_ = false;
  bool pixelPerfect_ = true;
  QPoint dragOffset_;
  QElapsedTimer animationClock_;
  QTimer animationTimer_;
};
