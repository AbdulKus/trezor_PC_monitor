#include "zipstore.h"

#include <QDataStream>
#include <QFile>

#include "protocol.h"

namespace {
void write16(QByteArray &out, quint16 value) {
  out.append(char(value & 0xff));
  out.append(char((value >> 8) & 0xff));
}
void write32(QByteArray &out, quint32 value) {
  write16(out, quint16(value & 0xffff));
  write16(out, quint16(value >> 16));
}
quint16 read16(const QByteArray &in, qsizetype offset) {
  return quint16(quint8(in[offset])) | (quint16(quint8(in[offset + 1])) << 8);
}
quint32 read32(const QByteArray &in, qsizetype offset) {
  return quint32(read16(in, offset)) | (quint32(read16(in, offset + 2)) << 16);
}
struct Entry {
  QByteArray name;
  QByteArray data;
  quint32 crc = 0;
  quint32 offset = 0;
};
}  // namespace

bool ZipStore::write(const QString &path,
                     const QHash<QString, QByteArray> &files, QString *error) {
  QVector<Entry> entries;
  QStringList names = files.keys();
  names.sort();
  QByteArray archive;
  for (const QString &name : names) {
    Entry entry;
    entry.name = name.toUtf8();
    entry.data = files.value(name);
    entry.crc = tm_crc32(reinterpret_cast<const uint8_t *>(entry.data.constData()),
                         quint32(entry.data.size()));
    entry.offset = quint32(archive.size());
    write32(archive, 0x04034b50);
    write16(archive, 20);
    write16(archive, 0x0800);
    write16(archive, 0);
    write16(archive, 0);
    write16(archive, 0);
    write32(archive, entry.crc);
    write32(archive, quint32(entry.data.size()));
    write32(archive, quint32(entry.data.size()));
    write16(archive, quint16(entry.name.size()));
    write16(archive, 0);
    archive.append(entry.name);
    archive.append(entry.data);
    entries.push_back(entry);
  }
  quint32 centralOffset = quint32(archive.size());
  for (const Entry &entry : entries) {
    write32(archive, 0x02014b50);
    write16(archive, 20);
    write16(archive, 20);
    write16(archive, 0x0800);
    write16(archive, 0);
    write16(archive, 0);
    write16(archive, 0);
    write32(archive, entry.crc);
    write32(archive, quint32(entry.data.size()));
    write32(archive, quint32(entry.data.size()));
    write16(archive, quint16(entry.name.size()));
    write16(archive, 0);
    write16(archive, 0);
    write16(archive, 0);
    write16(archive, 0);
    write32(archive, 0);
    write32(archive, entry.offset);
    archive.append(entry.name);
  }
  quint32 centralSize = quint32(archive.size()) - centralOffset;
  write32(archive, 0x06054b50);
  write16(archive, 0);
  write16(archive, 0);
  write16(archive, quint16(entries.size()));
  write16(archive, quint16(entries.size()));
  write32(archive, centralSize);
  write32(archive, centralOffset);
  write16(archive, 0);

  QFile output(path);
  if (!output.open(QIODevice::WriteOnly) || output.write(archive) != archive.size()) {
    if (error) *error = output.errorString();
    return false;
  }
  return true;
}

bool ZipStore::read(const QString &path, QHash<QString, QByteArray> *files,
                    QString *error) {
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    if (error) *error = input.errorString();
    return false;
  }
  return read(input.readAll(), files, error);
}

bool ZipStore::read(const QByteArray &archive,
                    QHash<QString, QByteArray> *files, QString *error) {
  if (archive.size() < 22) {
    if (error) *error = QStringLiteral("Некорректный ZIP: файл слишком короткий");
    return false;
  }
  qsizetype start = qMax<qsizetype>(0, archive.size() - 65557);
  qsizetype eocd = -1;
  for (qsizetype i = archive.size() - 22; i >= start; i--) {
    if (read32(archive, i) == 0x06054b50) {
      eocd = i;
      break;
    }
  }
  if (eocd < 0) {
    if (error) *error = QStringLiteral("Некорректный ZIP: EOCD не найден");
    return false;
  }
  quint16 count = read16(archive, eocd + 10);
  quint32 offset = read32(archive, eocd + 16);
  files->clear();
  for (quint16 i = 0; i < count; i++) {
    if (offset + 46 > quint32(archive.size()) ||
        read32(archive, offset) != 0x02014b50) {
      if (error) *error = QStringLiteral("Повреждён central directory");
      return false;
    }
    quint16 method = read16(archive, offset + 10);
    quint32 crc = read32(archive, offset + 16);
    quint32 compressed = read32(archive, offset + 20);
    quint32 uncompressed = read32(archive, offset + 24);
    quint16 nameLength = read16(archive, offset + 28);
    quint16 extraLength = read16(archive, offset + 30);
    quint16 commentLength = read16(archive, offset + 32);
    quint32 localOffset = read32(archive, offset + 42);
    if (method != 0 || compressed != uncompressed ||
        offset + 46 + nameLength > quint32(archive.size()) ||
        localOffset + 30 > quint32(archive.size()) ||
        read32(archive, localOffset) != 0x04034b50) {
      if (error) *error = QStringLiteral("ZIP использует неподдерживаемое сжатие");
      return false;
    }
    QByteArray name = archive.mid(offset + 46, nameLength);
    quint16 localName = read16(archive, localOffset + 26);
    quint16 localExtra = read16(archive, localOffset + 28);
    quint32 dataOffset = localOffset + 30 + localName + localExtra;
    if (dataOffset + compressed > quint32(archive.size())) {
      if (error) *error = QStringLiteral("ZIP entry выходит за границы файла");
      return false;
    }
    QByteArray data = archive.mid(dataOffset, compressed);
    if (tm_crc32(reinterpret_cast<const uint8_t *>(data.constData()),
                 quint32(data.size())) != crc) {
      if (error) *error = QStringLiteral("CRC ресурса не совпадает");
      return false;
    }
    files->insert(QString::fromUtf8(name), data);
    offset += 46 + nameLength + extraLength + commentLength;
  }
  return true;
}
