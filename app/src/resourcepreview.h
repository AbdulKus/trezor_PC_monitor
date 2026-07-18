#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include "projectmodel.h"

class ResourcePreview : public QWidget {
  Q_OBJECT
 public:
  explicit ResourcePreview(QWidget *parent = nullptr);
  void setResource(const ResourceModel &resource);
  void clearResource();
  QImage oledImage() const;

 protected:
  void paintEvent(QPaintEvent *) override;

 private:
  int currentFrame() const;

  ResourceModel resource_;
  bool hasResource_ = false;
  QElapsedTimer clock_;
  QTimer timer_;
};
