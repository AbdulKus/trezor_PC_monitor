#pragma once

#include <QImage>
#include <QWidget>

class IconEditor : public QWidget {
  Q_OBJECT
 public:
  explicit IconEditor(QWidget *parent = nullptr);
  void setImage(const QImage &image);
  QImage image() const { return image_; }
  void setErase(bool erase) { erase_ = erase; }
 public slots:
  void clear();
  void invert();
 signals:
  void imageChanged(const QImage &image);
 protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
 private:
  void apply(const QPoint &position);
  QImage image_;
  bool erase_ = false;
};
