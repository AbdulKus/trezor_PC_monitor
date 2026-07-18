#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

class QPainter;

struct PixelGlyphData {
  int width = 0;
  int advance = 0;
  QByteArray bits;
};

struct PixelFontData {
  int width = 0;
  int height = 0;
  int baseline = 0;
  QHash<uint, PixelGlyphData> glyphs;
  bool valid() const { return width > 0 && height > 0 && !glyphs.isEmpty(); }
};

class PixelFonts {
 public:
  static QStringList families();
  static bool isPixelFont(const QString &family);
  static int pixelHeight(const QString &family);
  static PixelFontData load(const QString &family);
  static void draw(QPainter &painter, int x, int y, const QString &text,
                   const QString &family);
};
