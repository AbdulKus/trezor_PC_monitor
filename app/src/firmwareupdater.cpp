#include "firmwareupdater.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
extern "C" {
#include <hidsdi.h>
}
#include <setupapi.h>
#include <winusb.h>
#endif

#include <cstring>
#include <functional>

namespace {
#pragma pack(push, 1)
struct FirmwareHeader {
  quint32 magic;
  quint32 hdrlen;
  quint32 expiry;
  quint32 codelen;
  quint32 version;
  quint32 fixVersion;
  quint32 hwModel;
  quint8 hwRevision;
  quint8 monotonic;
  quint8 reserved1[2];
  quint8 hashes[512];
  quint8 rest[480];
};
#pragma pack(pop)
static_assert(sizeof(FirmwareHeader) == 1024);

QByteArray varint(quint32 value) {
  QByteArray output;
  do {
    quint8 byte = value & 0x7f;
    value >>= 7;
    if (value) byte |= 0x80;
    output.append(char(byte));
  } while (value);
  return output;
}

#ifdef Q_OS_WIN
struct LibUsbContext;
struct LibUsbDevice;
struct LibUsbDeviceHandle;
struct LibUsbDescriptor {
  quint8 length;
  quint8 descriptorType;
  quint16 usbVersion;
  quint8 deviceClass;
  quint8 deviceSubClass;
  quint8 deviceProtocol;
  quint8 maxPacketSize;
  quint16 vendorId;
  quint16 productId;
  quint16 deviceVersion;
  quint8 manufacturerIndex;
  quint8 productIndex;
  quint8 serialIndex;
  quint8 configurationCount;
};

class BootloaderHid {
 public:
  ~BootloaderHid() {
    if (usbHandle_) {
      usbRelease_(usbHandle_, 0);
      usbClose_(usbHandle_);
    }
    if (usbContext_) usbExit_(usbContext_);
    if (usbLibrary_) FreeLibrary(usbLibrary_);
    if (winusb_) WinUsb_Free(winusb_);
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
  }
  bool open() {
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO devices = SetupDiGetClassDevsW(
        &guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) return false;
    for (DWORD i = 0;; i++) {
      SP_DEVICE_INTERFACE_DATA info{};
      info.cbSize = sizeof(info);
      if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, i, &info)) break;
      DWORD required = 0;
      SetupDiGetDeviceInterfaceDetailW(devices, &info, nullptr, 0, &required, nullptr);
      QByteArray storage(required, '\0');
      auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(storage.data());
      detail->cbSize = sizeof(*detail);
      if (!SetupDiGetDeviceInterfaceDetailW(devices, &info, detail, required,
                                            nullptr, nullptr))
        continue;
      HANDLE handle = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
      if (handle == INVALID_HANDLE_VALUE) continue;
      HIDD_ATTRIBUTES attributes{};
      attributes.Size = sizeof(attributes);
      if (HidD_GetAttributes(handle, &attributes) &&
          attributes.VendorID == 0x1209 && attributes.ProductID == 0x53c0) {
        handle_ = handle;
        break;
      }
      CloseHandle(handle);
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (handle_ != INVALID_HANDLE_VALUE) return true;

    static const GUID kWinUsbGuids[] = {
        {0x0263b512, 0x88cb, 0x4136,
         {0x96, 0x13, 0x5c, 0x8e, 0x10, 0x9d, 0x8e, 0xf5}},
        {0xa5dcbf10, 0x6530, 0x11d2,
         {0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed}},
    };
    for (const GUID &interfaceGuid : kWinUsbGuids) {
      devices = SetupDiGetClassDevsW(
          &interfaceGuid, nullptr, nullptr,
          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
      if (devices == INVALID_HANDLE_VALUE) continue;
      for (DWORD i = 0;; i++) {
        SP_DEVICE_INTERFACE_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &interfaceGuid, i,
                                         &info))
          break;
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &info, nullptr, 0, &required,
                                         nullptr);
        QByteArray storage(required, '\0');
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(
            storage.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &info, detail, required,
                                              nullptr, nullptr))
          continue;
        HANDLE candidate = CreateFileW(
            detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (candidate == INVALID_HANDLE_VALUE) continue;
        WINUSB_INTERFACE_HANDLE interfaceHandle = nullptr;
        USB_DEVICE_DESCRIPTOR descriptor{};
        ULONG received = 0;
        if (WinUsb_Initialize(candidate, &interfaceHandle) &&
            WinUsb_GetDescriptor(interfaceHandle, USB_DEVICE_DESCRIPTOR_TYPE,
                                 0, 0,
                                 reinterpret_cast<PUCHAR>(&descriptor),
                                 sizeof(descriptor), &received) &&
            descriptor.idVendor == 0x1209 && descriptor.idProduct == 0x53c0) {
          handle_ = candidate;
          winusb_ = interfaceHandle;
          ULONG timeout = 1000;
          WinUsb_SetPipePolicy(winusb_, 0x01, PIPE_TRANSFER_TIMEOUT,
                               sizeof(timeout), &timeout);
          WinUsb_SetPipePolicy(winusb_, 0x81, PIPE_TRANSFER_TIMEOUT,
                               sizeof(timeout), &timeout);
          break;
        }
        if (interfaceHandle) WinUsb_Free(interfaceHandle);
        CloseHandle(candidate);
      }
      SetupDiDestroyDeviceInfoList(devices);
      if (handle_ != INVALID_HANDLE_VALUE) break;
    }
    if (handle_ != INVALID_HANDLE_VALUE) return true;

    const QString libraryPath =
        QCoreApplication::applicationDirPath() + "/libusb-1.0.dll";
    usbLibrary_ = LoadLibraryW(reinterpret_cast<const wchar_t *>(
        QDir::toNativeSeparators(libraryPath).utf16()));
    if (!usbLibrary_) usbLibrary_ = LoadLibraryW(L"libusb-1.0.dll");
    if (!usbLibrary_) return false;
    usbInit_ = reinterpret_cast<UsbInit>(GetProcAddress(usbLibrary_, "libusb_init"));
    usbExit_ = reinterpret_cast<UsbExit>(GetProcAddress(usbLibrary_, "libusb_exit"));
    usbList_ = reinterpret_cast<UsbList>(GetProcAddress(usbLibrary_, "libusb_get_device_list"));
    usbFreeList_ = reinterpret_cast<UsbFreeList>(GetProcAddress(usbLibrary_, "libusb_free_device_list"));
    usbDescriptor_ = reinterpret_cast<UsbDescriptor>(GetProcAddress(usbLibrary_, "libusb_get_device_descriptor"));
    usbOpen_ = reinterpret_cast<UsbOpen>(GetProcAddress(usbLibrary_, "libusb_open"));
    usbClose_ = reinterpret_cast<UsbClose>(GetProcAddress(usbLibrary_, "libusb_close"));
    usbClaim_ = reinterpret_cast<UsbClaim>(GetProcAddress(usbLibrary_, "libusb_claim_interface"));
    usbRelease_ = reinterpret_cast<UsbRelease>(GetProcAddress(usbLibrary_, "libusb_release_interface"));
    usbTransfer_ = reinterpret_cast<UsbTransfer>(GetProcAddress(usbLibrary_, "libusb_interrupt_transfer"));
    if (!usbInit_ || !usbExit_ || !usbList_ || !usbFreeList_ ||
        !usbDescriptor_ || !usbOpen_ || !usbClose_ || !usbClaim_ ||
        !usbRelease_ || !usbTransfer_ || usbInit_(&usbContext_) != 0)
      return false;
    LibUsbDevice **list = nullptr;
    const qint64 count = usbList_(usbContext_, &list);
    for (qint64 i = 0; i < count; ++i) {
      LibUsbDescriptor descriptor{};
      if (usbDescriptor_(list[i], &descriptor) != 0 ||
          descriptor.vendorId != 0x1209 || descriptor.productId != 0x53c0)
        continue;
      LibUsbDeviceHandle *candidate = nullptr;
      if (usbOpen_(list[i], &candidate) == 0 &&
          usbClaim_(candidate, 0) == 0) {
        usbHandle_ = candidate;
        break;
      }
      if (candidate) usbClose_(candidate);
    }
    if (list) usbFreeList_(list, 1);
    return usbHandle_ != nullptr;
  }
  bool report(const QByteArray &packet, DWORD timeout = 1000) {
    if (usbHandle_) {
      int transferred = 0;
      return usbTransfer_(
                 usbHandle_, 0x01,
                 reinterpret_cast<unsigned char *>(
                     const_cast<char *>(packet.constData())),
                 qMin(64, packet.size()), &transferred, timeout) == 0 &&
             transferred == 64;
    }
    if (winusb_) {
      ULONG pipeTimeout = timeout;
      WinUsb_SetPipePolicy(winusb_, 0x01, PIPE_TRANSFER_TIMEOUT,
                           sizeof(pipeTimeout), &pipeTimeout);
      ULONG written = 0;
      return WinUsb_WritePipe(
                 winusb_, 0x01,
                 reinterpret_cast<PUCHAR>(const_cast<char *>(packet.constData())),
                 ULONG(qMin(64, packet.size())), &written, nullptr) &&
             written == 64;
    }
    QByteArray output(65, '\0');
    memcpy(output.data() + 1, packet.constData(), qMin(64, packet.size()));
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD written = 0;
    BOOL ok = WriteFile(handle_, output.constData(), output.size(), &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING &&
        WaitForSingleObject(ov.hEvent, timeout) == WAIT_OBJECT_0)
      ok = GetOverlappedResult(handle_, &ov, &written, FALSE);
    if (!ok) CancelIoEx(handle_, &ov);
    CloseHandle(ov.hEvent);
    return ok;
  }
  QByteArray readReport(DWORD timeout) {
    if (usbHandle_) {
      QByteArray input(64, '\0');
      int transferred = 0;
      if (usbTransfer_(usbHandle_, 0x81,
                       reinterpret_cast<unsigned char *>(input.data()),
                       input.size(), &transferred, timeout) != 0 ||
          transferred != 64)
        return {};
      return input;
    }
    if (winusb_) {
      ULONG pipeTimeout = timeout;
      WinUsb_SetPipePolicy(winusb_, 0x81, PIPE_TRANSFER_TIMEOUT,
                           sizeof(pipeTimeout), &pipeTimeout);
      QByteArray input(64, '\0');
      ULONG received = 0;
      if (!WinUsb_ReadPipe(winusb_, 0x81,
                           reinterpret_cast<PUCHAR>(input.data()), input.size(),
                           &received, nullptr) ||
          received != 64)
        return {};
      return input;
    }
    QByteArray input(65, '\0');
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD read = 0;
    BOOL ok = ReadFile(handle_, input.data(), input.size(), &read, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING &&
        WaitForSingleObject(ov.hEvent, timeout) == WAIT_OBJECT_0)
      ok = GetOverlappedResult(handle_, &ov, &read, FALSE);
    if (!ok) CancelIoEx(handle_, &ov);
    CloseHandle(ov.hEvent);
    if (!ok || read < 64) return {};
    return read == 65 ? input.mid(1, 64) : input.left(64);
  }
  bool sendMessage(quint16 id, const QByteArray &payload,
                   std::function<void(int)> progress = {}) {
    quint32 size = payload.size();
    QByteArray packet(64, '\0');
    packet[0] = '?'; packet[1] = '#'; packet[2] = '#';
    packet[3] = char(id >> 8); packet[4] = char(id);
    packet[5] = char(size >> 24); packet[6] = char(size >> 16);
    packet[7] = char(size >> 8); packet[8] = char(size);
    int copied = qMin(55, payload.size());
    memcpy(packet.data() + 9, payload.constData(), copied);
    if (!report(packet)) return false;
    while (copied < payload.size()) {
      packet.fill('\0');
      packet[0] = '?';
      int amount = qMin(63, payload.size() - copied);
      memcpy(packet.data() + 1, payload.constData() + copied, amount);
      if (!report(packet)) return false;
      copied += amount;
      if (progress) progress(copied * 100 / payload.size());
    }
    return true;
  }
  bool readMessage(quint16 *id, QByteArray *payload, DWORD timeout) {
    QByteArray first = readReport(timeout);
    if (first.size() != 64 || first[0] != '?' || first[1] != '#' || first[2] != '#')
      return false;
    *id = (quint8(first[3]) << 8) | quint8(first[4]);
    quint32 size = (quint32(quint8(first[5])) << 24) |
                   (quint32(quint8(first[6])) << 16) |
                   (quint32(quint8(first[7])) << 8) | quint8(first[8]);
    *payload = first.mid(9, qMin<quint32>(55, size));
    while (payload->size() < int(size)) {
      QByteArray continuation = readReport(timeout);
      if (continuation.size() != 64 || continuation[0] != '?') return false;
      payload->append(continuation.mid(1, qMin(63, int(size) - payload->size())));
    }
    return true;
  }
 private:
  using UsbInit = int (*)(LibUsbContext **);
  using UsbExit = void (*)(LibUsbContext *);
  using UsbList = qint64 (*)(LibUsbContext *, LibUsbDevice ***);
  using UsbFreeList = void (*)(LibUsbDevice **, int);
  using UsbDescriptor = int (*)(LibUsbDevice *, LibUsbDescriptor *);
  using UsbOpen = int (*)(LibUsbDevice *, LibUsbDeviceHandle **);
  using UsbClose = void (*)(LibUsbDeviceHandle *);
  using UsbClaim = int (*)(LibUsbDeviceHandle *, int);
  using UsbRelease = int (*)(LibUsbDeviceHandle *, int);
  using UsbTransfer = int (*)(LibUsbDeviceHandle *, unsigned char,
                              unsigned char *, int, int *, unsigned int);

  HANDLE handle_ = INVALID_HANDLE_VALUE;
  WINUSB_INTERFACE_HANDLE winusb_ = nullptr;
  HMODULE usbLibrary_ = nullptr;
  LibUsbContext *usbContext_ = nullptr;
  LibUsbDeviceHandle *usbHandle_ = nullptr;
  UsbInit usbInit_ = nullptr;
  UsbExit usbExit_ = nullptr;
  UsbList usbList_ = nullptr;
  UsbFreeList usbFreeList_ = nullptr;
  UsbDescriptor usbDescriptor_ = nullptr;
  UsbOpen usbOpen_ = nullptr;
  UsbClose usbClose_ = nullptr;
  UsbClaim usbClaim_ = nullptr;
  UsbRelease usbRelease_ = nullptr;
  UsbTransfer usbTransfer_ = nullptr;
};
#endif
}  // namespace

FirmwareUpdater::FirmwareUpdater(QObject *parent) : QObject(parent) {}

bool FirmwareUpdater::validate(const QByteArray &firmware, QString *error,
                               QString *sha256) const {
  if (sha256)
    *sha256 = QCryptographicHash::hash(firmware, QCryptographicHash::Sha256).toHex().toUpper();
  if (firmware.size() < 1024 + 4096 || firmware.size() > 960 * 1024) {
    if (error) *error = QStringLiteral("Некорректный размер firmware");
    return false;
  }
  const auto *header = reinterpret_cast<const FirmwareHeader *>(firmware.constData());
  if (memcmp(&header->magic, "TRZF", 4) != 0) {
    if (error) *error = QStringLiteral("Bootloader ожидает внутренний заголовок TRZF");
    return false;
  }
  if (header->hwModel != 0x31423154 ||
      header->codelen != quint32(firmware.size() - 1024)) {
    if (error) *error = QStringLiteral("Образ не предназначен для Trezor One T1B1");
    return false;
  }
  quint32 total = 1024 + header->codelen;
  int chunks = (total + 65535) / 65536;
  for (int chunk = 0; chunk < chunks; chunk++) {
    int offset = chunk == 0 ? 1024 : chunk * 65536;
    int length = chunk == 0 ? 63 * 1024 : 64 * 1024;
    QByteArray data(length, char(0xff));
    const QByteArray present = firmware.mid(offset, length);
    data.replace(0, present.size(), present);
    QByteArray digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    if (memcmp(digest.constData(), header->hashes + chunk * 32, 32) != 0) {
      if (error) *error = QStringLiteral("Не совпадает hash chunk %1").arg(chunk);
      return false;
    }
  }
  for (int chunk = chunks; chunk < 16; chunk++)
    for (int i = 0; i < 32; i++)
      if (header->hashes[chunk * 32 + i] != 0) {
        if (error) *error = QStringLiteral("Ненулевой hash неиспользуемого chunk");
        return false;
      }
  return true;
}

void FirmwareUpdater::flash(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    emit finished(false, file.errorString());
    return;
  }
  QByteArray firmware = file.readAll();
  QString error;
  QString sha;
  if (!validate(firmware, &error, &sha)) {
    emit finished(false, error);
    return;
  }
  emit statusChanged(QStringLiteral("Проверен SHA-256 %1").arg(sha));
  QThread *worker = QThread::create([this, firmware]() {
#ifdef Q_OS_WIN
    BootloaderHid device;
    if (!device.open()) {
      emit finished(false, QStringLiteral("Trezor bootloader не найден"));
      return;
    }
    quint16 message = 0;
    QByteArray response;
    auto waitForSuccess = [&](DWORD timeout, const QString &prompt) {
      for (;;) {
        if (!device.readMessage(&message, &response, timeout)) return false;
        if (message == 2) return true;
        if (message != 26) return false;
        emit statusChanged(prompt);
        // ButtonAck only tells the bootloader that the host displayed its
        // prompt. The actual confirmation still requires the physical keys.
        if (!device.sendMessage(27, {})) return false;
      }
    };
    emit statusChanged(QStringLiteral("Подключение к bootloader…"));
    if (!device.sendMessage(0, {}) || !device.readMessage(&message, &response, 3000)) {
      emit finished(false, QStringLiteral("Bootloader не отвечает"));
      return;
    }
    QByteArray erase;
    erase.append(char(0x08));
    erase.append(varint(firmware.size()));
    emit statusChanged(QStringLiteral("Подтвердите установку на Trezor"));
    if (!device.sendMessage(6, erase) || !waitForSuccess(
            60000, QStringLiteral("Подтвердите стирание на Trezor"))) {
      emit finished(false, QStringLiteral("Стирание отменено или завершилось ошибкой"));
      return;
    }
    QByteArray upload;
    upload.append(char(0x0a));
    upload.append(varint(firmware.size()));
    upload.append(firmware);
    emit statusChanged(QStringLiteral("Загрузка firmware…"));
    if (!device.sendMessage(7, upload,
                            [this](int value) { emit progressChanged(value); }) ||
        !waitForSuccess(60000,
                        QStringLiteral("Подтвердите fingerprint на Trezor"))) {
      emit finished(false, QStringLiteral("Ошибка передачи firmware"));
      return;
    }
    emit progressChanged(100);
    emit finished(true, QStringLiteral("Firmware установлена"));
#else
    emit finished(false, QStringLiteral("Прошивка поддерживается только на Windows"));
#endif
  });
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}
