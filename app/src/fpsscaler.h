#pragma once

#include <QtGlobal>

class FpsScaler {
 public:
  bool update(double fps, qint64 timestampMs);
  double maximum() const;
  int presetIndex() const { return currentPreset_; }
  static int presetCount();
  static double presetValue(int index);
  static bool accepts(int index, double fps);

 private:
  static int nearestPreset(double fps);
  static int candidatePreset(double fps, int current);
  int currentPreset_ = -1;
  int candidatePreset_ = -1;
  int candidateEvidenceMs_ = 0;
  qint64 lastTimestampMs_ = -1;
};
