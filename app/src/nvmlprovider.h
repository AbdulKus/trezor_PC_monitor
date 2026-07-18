#pragma once

#include <QHash>
#include <QLibrary>
#include <QString>

class NvmlProvider {
 public:
  ~NvmlProvider();
  QHash<QString, double> poll(const QString &adapterName);
  void shutdown();

 private:
  using Device = void *;
  bool initialize();

  QLibrary library_;
  bool initialized_ = false;
  int (*init_)() = nullptr;
  int (*shutdown_)() = nullptr;
  int (*getCount_)(unsigned int *) = nullptr;
  int (*getHandle_)(unsigned int, Device *) = nullptr;
  int (*getName_)(Device, char *, unsigned int) = nullptr;
  int (*getTemperature_)(Device, unsigned int, unsigned int *) = nullptr;
  int (*getPower_)(Device, unsigned int *) = nullptr;
  int (*getClock_)(Device, unsigned int, unsigned int *) = nullptr;
  int (*getFan_)(Device, unsigned int *) = nullptr;
};
