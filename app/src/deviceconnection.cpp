#include "deviceconnection.h"

#ifdef Q_OS_WIN
#include <windows.h>
extern "C" {
#include <hidsdi.h>
}
#include <setupapi.h>
#endif

#include <QDateTime>
#include <QMutexLocker>
#include <QtMath>

#include <cstring>

namespace {
tm_packet_t makePacket(quint8 type, quint8 sequence, const QByteArray &payload) {
  tm_packet_t packet{};
  packet.magic[0] = 'T';
  packet.magic[1] = 'M';
  packet.version = TM_PROTOCOL_VERSION;
  packet.type = type;
  packet.sequence = sequence;
  packet.payload_length = quint16(qMin<int>(payload.size(), TM_PACKET_PAYLOAD_SIZE));
  memcpy(packet.payload, payload.constData(), packet.payload_length);
  packet.crc32 = tm_crc32(reinterpret_cast<const uint8_t *>(&packet), 60);
  return packet;
}

#ifdef Q_OS_WIN
class HidTransport {
 public:
  ~HidTransport() { close(); }

  bool openMonitor() {
    close();
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO devices = SetupDiGetClassDevsW(
        &guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return false;
    for (DWORD index = 0;; index++) {
      SP_DEVICE_INTERFACE_DATA interfaceData{};
      interfaceData.cbSize = sizeof(interfaceData);
      if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, index,
                                       &interfaceData))
        break;
      DWORD required = 0;
      SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0,
                                       &required, nullptr);
      QByteArray storage(required, '\0');
      auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(storage.data());
      detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
      if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail,
                                            required, nullptr, nullptr))
        continue;
      HANDLE candidate = CreateFileW(
          detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
          FILE_FLAG_OVERLAPPED, nullptr);
      if (candidate == INVALID_HANDLE_VALUE) continue;
      HIDD_ATTRIBUTES attributes{};
      attributes.Size = sizeof(attributes);
      wchar_t product[128]{};
      if (HidD_GetAttributes(candidate, &attributes) &&
          attributes.VendorID == 0x1209 && attributes.ProductID == 0x53c1 &&
          HidD_GetProductString(candidate, product, sizeof(product)) &&
          QString::fromWCharArray(product) == QStringLiteral("Trezor PC Monitor")) {
        handle_ = candidate;
        break;
      }
      CloseHandle(candidate);
    }
    SetupDiDestroyDeviceInfoList(devices);
    return handle_ != INVALID_HANDLE_VALUE;
  }

  void close() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }

  bool writePacket(const tm_packet_t &packet, DWORD timeoutMs) {
    if (handle_ == INVALID_HANDLE_VALUE) return false;
    uint8_t report[65]{};
    memcpy(report + 1, &packet, sizeof(packet));
    OVERLAPPED overlap{};
    overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD written = 0;
    BOOL ok = WriteFile(handle_, report, sizeof(report), &written, &overlap);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
      if (WaitForSingleObject(overlap.hEvent, timeoutMs) == WAIT_OBJECT_0)
        ok = GetOverlappedResult(handle_, &overlap, &written, FALSE);
      else
        CancelIoEx(handle_, &overlap);
    }
    CloseHandle(overlap.hEvent);
    return ok && written == sizeof(report);
  }

  bool readPacket(tm_packet_t *packet, DWORD timeoutMs) {
    if (handle_ == INVALID_HANDLE_VALUE) return false;
    uint8_t report[65]{};
    OVERLAPPED overlap{};
    overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD read = 0;
    BOOL ok = ReadFile(handle_, report, sizeof(report), &read, &overlap);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
      if (WaitForSingleObject(overlap.hEvent, timeoutMs) == WAIT_OBJECT_0)
        ok = GetOverlappedResult(handle_, &overlap, &read, FALSE);
      else
        CancelIoEx(handle_, &overlap);
    }
    CloseHandle(overlap.hEvent);
    if (!ok || read < 64) return false;
    const uint8_t *source = read == 65 ? report + 1 : report;
    memcpy(packet, source, sizeof(*packet));
    return packet->magic[0] == 'T' && packet->magic[1] == 'M' &&
           packet->payload_length <= TM_PACKET_PAYLOAD_SIZE &&
           tm_crc32(reinterpret_cast<const uint8_t *>(packet), 60) ==
               packet->crc32;
  }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};
#endif

tm_metric_entry_t metricEntry(quint16 channel, const MetricSample &sample) {
  tm_metric_entry_t entry{};
  entry.channel_id = channel;
  if (!sample.valid) {
    entry.status = TM_STATUS_UNAVAILABLE;
    return entry;
  }
  entry.status = sample.estimated ? TM_STATUS_ESTIMATED : TM_STATUS_VALID;
  if (sample.unit == "%" || sample.unit == "C" || sample.unit == "W" ||
      sample.unit.isEmpty()) {
    entry.scale_exponent = -2;
    entry.value = qint32(qRound64(
        qBound(-21474836.0, sample.value, 21474836.0) * 100.0));
  } else {
    entry.scale_exponent = 0;
    entry.value = qint32(qRound64(
        qBound(-2147483647.0, sample.value, 2147483647.0)));
  }
  return entry;
}
}  // namespace

DeviceConnection::DeviceConnection(QObject *parent) : QThread(parent) {}
DeviceConnection::~DeviceConnection() {
  stop();
  wait(2000);
}

void DeviceConnection::uploadPack(const QByteArray &pack,
                                  const QHash<QString, quint16> &channels) {
  QMutexLocker lock(&mutex_);
  command_.type = PendingCommand::Upload;
  command_.pack = pack;
  command_.channels = channels;
}
void DeviceConnection::updateMetrics(
    const QHash<QString, MetricSample> &samples) {
  QMutexLocker lock(&mutex_);
  latestSamples_ = samples;
  metricsDirty_ = true;
}
void DeviceConnection::setPage(quint8 page) {
  QMutexLocker lock(&mutex_);
  command_.type = PendingCommand::SetPage;
  command_.page = page;
}
void DeviceConnection::requestBootloader() {
  QMutexLocker lock(&mutex_);
  command_.type = PendingCommand::Bootloader;
}
void DeviceConnection::stop() { stopping_ = true; }

void DeviceConnection::run() {
#ifndef Q_OS_WIN
  emit statusChanged(QStringLiteral("USB HID поддерживается только на Windows"));
  return;
#else
  HidTransport transport;
  quint8 sequence = 1;
  bool connected = false;
  QHash<QString, quint16> activeChannels;
  QHash<quint16, QByteArray> lastEntries;
  qint64 lastFullSnapshot = 0;
  QString lastStatus;
  auto setStatus = [&](const QString &status) {
    if (status == lastStatus) return;
    lastStatus = status;
    emit statusChanged(status);
  };

  auto dispatch = [&](const tm_packet_t &packet) {
    if (packet.type == TM_MSG_BUTTON_EVENT &&
        packet.payload_length >= sizeof(tm_button_event_t)) {
      tm_button_event_t event;
      memcpy(&event, packet.payload, sizeof(event));
      emit buttonEvent(event.action_id, event.event_id);
    }
  };
  auto transact = [&](quint8 type, const QByteArray &payload, quint8 expected,
                      int timeout = 300, tm_packet_t *response = nullptr) {
    quint8 currentSequence = sequence++;
    tm_packet_t outgoing = makePacket(type, currentSequence, payload);
    for (int attempt = 0; attempt < 3 && !stopping_; attempt++) {
      if (!transport.writePacket(outgoing, timeout)) continue;
      qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeout;
      while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        tm_packet_t incoming{};
        if (!transport.readPacket(&incoming, 50)) continue;
        if (incoming.type == TM_MSG_BUTTON_EVENT) {
          dispatch(incoming);
          continue;
        }
        if (incoming.sequence == currentSequence && incoming.type == expected) {
          if (response) *response = incoming;
          return true;
        }
        if (incoming.sequence == currentSequence && incoming.type == TM_MSG_NACK)
          return false;
      }
    }
    return false;
  };

  while (!stopping_) {
    if (!connected) {
      if (!transport.openMonitor()) {
        setStatus(QStringLiteral("Ожидание Trezor PC Monitor…"));
        msleep(500);
        continue;
      }
      if (!transact(TM_MSG_HELLO, {}, TM_MSG_CAPABILITIES, 500)) {
        transport.close();
        msleep(500);
        continue;
      }
      connected = true;
      emit connectedChanged(true);
      setStatus(QStringLiteral("Trezor PC Monitor подключён"));
    }

    PendingCommand command;
    QHash<QString, MetricSample> samples;
    bool sendMetrics = false;
    {
      QMutexLocker lock(&mutex_);
      command = command_;
      command_.type = PendingCommand::None;
      if (metricsDirty_) {
        samples = latestSamples_;
        metricsDirty_ = false;
        sendMetrics = true;
      }
    }
    bool transportOk = true;
    if (command.type == PendingCommand::Upload) {
      quint16 transaction = quint16(QDateTime::currentMSecsSinceEpoch());
      QByteArray begin;
      begin.append(reinterpret_cast<const char *>(&transaction), 2);
      quint32 size = command.pack.size();
      quint32 crc = tm_crc32(
          reinterpret_cast<const uint8_t *>(command.pack.constData()), size);
      begin.append(reinterpret_cast<const char *>(&size), 4);
      begin.append(reinterpret_cast<const char *>(&crc), 4);
      transportOk = transact(TM_MSG_PACK_BEGIN, begin, TM_MSG_ACK);
      for (quint32 offset = 0; transportOk && offset < size; offset += 46) {
        QByteArray payload;
        payload.append(reinterpret_cast<const char *>(&transaction), 2);
        payload.append(reinterpret_cast<const char *>(&offset), 4);
        payload.append(command.pack.mid(offset, 46));
        transportOk = transact(TM_MSG_PACK_DATA, payload, TM_MSG_ACK);
        emit uploadProgress(int(qMin<quint32>(100, (offset + 46) * 100 / size)));
      }
      if (transportOk) {
        QByteArray commit(reinterpret_cast<const char *>(&transaction), 2);
        transportOk = transact(TM_MSG_PACK_COMMIT, commit, TM_MSG_ACK);
      }
      if (transportOk) {
        tm_packet_t state{};
        transportOk = transact(TM_MSG_HELLO, {}, TM_MSG_CAPABILITIES, 500,
                               &state) &&
                      state.payload_length >= 13 && state.payload[12] == 1;
      }
      if (transportOk) {
        activeChannels = command.channels;
        lastEntries.clear();
        lastFullSnapshot = 0;
        emit packUploaded(true, QStringLiteral("Макет загружен"));
      } else {
        emit packUploaded(false, QStringLiteral("Ошибка загрузки макета"));
      }
    } else if (command.type == PendingCommand::SetPage) {
      QByteArray page(1, char(command.page));
      transportOk = transact(TM_MSG_SET_PAGE, page, TM_MSG_ACK);
    } else if (command.type == PendingCommand::Bootloader) {
      transportOk = transact(TM_MSG_REBOOT_BOOTLOADER, {}, TM_MSG_ACK);
      if (transportOk) {
        transport.close();
        connected = false;
        emit connectedChanged(false);
        setStatus(QStringLiteral("Переход в bootloader…"));
        msleep(1000);
        continue;
      }
    }

    if (sendMetrics && !activeChannels.isEmpty()) {
      // The firmware deliberately expires metrics when the host disappears.  A
      // full snapshot must therefore be sent well inside that window even when
      // a value (RAM usage or a frame-rate cap, for example) is perfectly
      // stable.  Deltas are still sent immediately between snapshots.
      bool full = QDateTime::currentMSecsSinceEpoch() - lastFullSnapshot >= 1500;
      QVector<tm_metric_entry_t> entries;
      for (auto it = activeChannels.constBegin(); it != activeChannels.constEnd(); ++it) {
        tm_metric_entry_t entry = metricEntry(it.value(), samples.value(it.key()));
        QByteArray bytes(reinterpret_cast<const char *>(&entry), sizeof(entry));
        if (full || lastEntries.value(it.value()) != bytes) {
          entries << entry;
          lastEntries[it.value()] = bytes;
        }
      }
      if (full) lastFullSnapshot = QDateTime::currentMSecsSinceEpoch();
      for (int offset = 0; transportOk && offset < entries.size(); offset += 6) {
        int count = qMin(6, entries.size() - offset);
        QByteArray payload(reinterpret_cast<const char *>(entries.constData() + offset),
                           count * int(sizeof(tm_metric_entry_t)));
        transportOk = transact(TM_MSG_METRICS, payload, TM_MSG_ACK);
      }
    }
    if (!transportOk) {
      transport.close();
      connected = false;
      emit connectedChanged(false);
      setStatus(QStringLiteral("Связь потеряна, переподключение…"));
      msleep(300);
      continue;
    }
    tm_packet_t spontaneous{};
    if (transport.readPacket(&spontaneous, 20)) dispatch(spontaneous);
    msleep(20);
  }
  transport.close();
  if (connected) emit connectedChanged(false);
#endif
}
