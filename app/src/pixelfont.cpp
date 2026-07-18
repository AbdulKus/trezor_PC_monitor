#include "pixelfont.h"

#include <QFile>
#include <QPainter>
#include <QRegularExpression>

namespace {
QString resourceFor(const QString &family) {
  if (family == "Spleen 5x8") return ":/fonts/spleen-5x8.bdf";
  if (family == "Spleen 6x12") return ":/fonts/spleen-6x12.bdf";
  if (family == "Spleen 8x16") return ":/fonts/spleen-8x16.bdf";
  return {};
}
}  // namespace

QStringList PixelFonts::families() {
  return {"Spleen 5x8", "Spleen 6x12", "Spleen 8x16"};
}

bool PixelFonts::isPixelFont(const QString &family) {
  return !resourceFor(family).isEmpty();
}

int PixelFonts::pixelHeight(const QString &family) {
  if (family.endsWith("5x8")) return 8;
  if (family.endsWith("6x12")) return 12;
  if (family.endsWith("8x16")) return 16;
  return 8;
}

PixelFontData PixelFonts::load(const QString &family) {
  static const bool resourcesReady = [] {
    Q_INIT_RESOURCE(fonts);
    return true;
  }();
  Q_UNUSED(resourcesReady);
  static QHash<QString, PixelFontData> cache;
  if (cache.contains(family)) return cache.value(family);
  PixelFontData font;
  QFile file(resourceFor(family));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return font;
  const QList<QByteArray> lines = file.readAll().split('\n');
  int globalYOffset = 0;
  bool inGlyph = false;
  bool inBitmap = false;
  int encoding = -1;
  int advance = 0;
  int bbxWidth = 0;
  int bbxHeight = 0;
  int bbxX = 0;
  int bbxY = 0;
  QList<QByteArray> bitmapRows;

  auto finishGlyph = [&] {
    if (!inGlyph || encoding < 0 || font.width <= 0 || font.height <= 0)
      return;
    PixelGlyphData glyph;
    glyph.width = font.width;
    glyph.advance = advance > 0 ? advance : font.width;
    const int stride = (font.width + 7) / 8;
    glyph.bits = QByteArray(stride * font.height, '\0');
    const int top = font.baseline - (bbxHeight + bbxY);
    for (int row = 0; row < bitmapRows.size() && row < bbxHeight; ++row) {
      const QByteArray source = QByteArray::fromHex(bitmapRows[row].trimmed());
      for (int x = 0; x < bbxWidth; ++x) {
        const int targetX = x + bbxX;
        const int targetY = row + top;
        if (targetX < 0 || targetX >= font.width || targetY < 0 ||
            targetY >= font.height || x / 8 >= source.size())
          continue;
        if ((quint8(source[x / 8]) & (0x80 >> (x & 7))) != 0)
          glyph.bits[targetY * stride + targetX / 8] |=
              char(0x80 >> (targetX & 7));
      }
    }
    font.glyphs.insert(uint(encoding), glyph);
  };

  for (QByteArray line : lines) {
    line = line.trimmed();
    if (line.startsWith("FONTBOUNDINGBOX ") && !inGlyph) {
      const QList<QByteArray> fields = line.mid(16).split(' ');
      if (fields.size() >= 4) {
        font.width = fields[0].toInt();
        font.height = fields[1].toInt();
        globalYOffset = fields[3].toInt();
        font.baseline = font.height + globalYOffset;
      }
    } else if (line.startsWith("STARTCHAR")) {
      finishGlyph();
      inGlyph = true;
      inBitmap = false;
      encoding = -1;
      advance = font.width;
      bbxWidth = font.width;
      bbxHeight = font.height;
      bbxX = 0;
      bbxY = globalYOffset;
      bitmapRows.clear();
    } else if (line == "ENDCHAR") {
      finishGlyph();
      inGlyph = false;
      inBitmap = false;
    } else if (inGlyph && line.startsWith("ENCODING ")) {
      encoding = line.mid(9).toInt();
    } else if (inGlyph && line.startsWith("DWIDTH ")) {
      advance = line.mid(7).split(' ').value(0).toInt();
    } else if (inGlyph && line.startsWith("BBX ")) {
      const QList<QByteArray> fields = line.mid(4).split(' ');
      if (fields.size() >= 4) {
        bbxWidth = fields[0].toInt();
        bbxHeight = fields[1].toInt();
        bbxX = fields[2].toInt();
        bbxY = fields[3].toInt();
      }
    } else if (inGlyph && line == "BITMAP") {
      inBitmap = true;
    } else if (inGlyph && inBitmap) {
      bitmapRows << line;
    }
  }
  finishGlyph();
  cache.insert(family, font);
  return font;
}

void PixelFonts::draw(QPainter &painter, int x, int y, const QString &text,
                      const QString &family) {
  const PixelFontData font = load(family);
  if (!font.valid()) return;
  int cursor = x;
  const QList<uint> codepoints = text.toUcs4();
  for (uint codepoint : codepoints) {
    PixelGlyphData glyph = font.glyphs.value(
        codepoint, font.glyphs.value(uint('?')));
    const int stride = (glyph.width + 7) / 8;
    for (int gy = 0; gy < font.height; ++gy)
      for (int gx = 0; gx < glyph.width; ++gx)
        if ((quint8(glyph.bits[gy * stride + gx / 8]) &
             (0x80 >> (gx & 7))) != 0)
          painter.drawPoint(cursor + gx, y + gy);
    cursor += glyph.advance;
  }
}
