#include "fpsscaler.h"

#include <QtMath>

#include <array>

namespace {
constexpr int kConfirmationMs = 10000;
constexpr std::array<double, 12> kPresets = {
    30, 60, 90, 120, 144, 165, 180, 200, 240, 300, 360, 480};
}

int FpsScaler::presetCount() { return int(kPresets.size()); }

double FpsScaler::presetValue(int index) {
  return index >= 0 && index < presetCount() ? kPresets[index] : kPresets[0];
}

bool FpsScaler::accepts(int index, double fps) {
  if (index < 0 || index >= presetCount()) return false;
  const double lower = index == 0 ? 0.0 : kPresets[index - 1] + 1.0;
  const double upper = index + 1 == presetCount()
      ? 10000.0
      : kPresets[index] +
            qMax(6.0, (kPresets[index + 1] - kPresets[index]) / 3.0);
  return fps >= lower && fps <= upper;
}

int FpsScaler::nearestPreset(double fps) {
  int best = 0;
  double distance = qAbs(fps - kPresets[0]);
  for (int i = 1; i < presetCount(); ++i) {
    const double next = qAbs(fps - kPresets[i]);
    if (next < distance) {
      best = i;
      distance = next;
    }
  }
  return best;
}

int FpsScaler::candidatePreset(double fps, int current) {
  int candidate = nearestPreset(fps);
  if (candidate == current) {
    if (fps > kPresets[current] && current + 1 < presetCount())
      candidate = current + 1;
    else if (fps < kPresets[current] && current > 0)
      candidate = current - 1;
  }
  return candidate;
}

bool FpsScaler::update(double fps, qint64 timestampMs) {
  fps = qMax(0.0, fps);
  const int elapsed = lastTimestampMs_ >= 0
      ? qBound(100, int(timestampMs - lastTimestampMs_), 1000)
      : 250;
  lastTimestampMs_ = timestampMs;
  if (currentPreset_ < 0) {
    currentPreset_ = nearestPreset(fps);
    return false;
  }
  if (candidatePreset_ >= 0) {
    if (accepts(candidatePreset_, fps)) {
      candidateEvidenceMs_ = qMin(kConfirmationMs,
                                  candidateEvidenceMs_ + elapsed);
    } else {
      candidateEvidenceMs_ = qMax(0, candidateEvidenceMs_ - elapsed * 2);
      if (candidateEvidenceMs_ == 0) candidatePreset_ = -1;
    }
  }
  if (candidatePreset_ < 0 && !accepts(currentPreset_, fps)) {
    const int next = candidatePreset(fps, currentPreset_);
    if (next != currentPreset_ && accepts(next, fps)) {
      candidatePreset_ = next;
      candidateEvidenceMs_ = elapsed;
    }
  }
  if (candidatePreset_ >= 0 && candidateEvidenceMs_ >= kConfirmationMs) {
    currentPreset_ = candidatePreset_;
    candidatePreset_ = -1;
    candidateEvidenceMs_ = 0;
    return true;
  }
  return false;
}

double FpsScaler::maximum() const {
  return currentPreset_ >= 0 ? kPresets[currentPreset_] : kPresets[0];
}
