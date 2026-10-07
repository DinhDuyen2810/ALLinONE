#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

/// Tùy chọn khi khởi chạy một phiên gương + điều khiển màn hình (giống hệt các cờ dòng lệnh thật của
/// scrcpy.exe - xem `scrcpy --help`). Đặt tên tiếng Việt cho dễ hiểu, giá trị 0/false là mặc định của
/// chính scrcpy (không truyền cờ đó ra dòng lệnh).
struct ScrcpyOptions
{
    QString serial;              // rỗng = để scrcpy tự chọn (chỉ hợp lệ khi đúng 1 thiết bị đang kết nối)
    bool switchToWireless{false}; // dùng --tcpip: tự dò IP thiết bị (đang cắm USB) rồi chuyển sang điều khiển không dây
    int maxSize{0};               // 0 = độ phân giải gốc (--max-size)
    int bitRateMbps{8};           // --video-bit-rate, đơn vị Mbps
    int maxFps{0};                 // 0 = không giới hạn khung hình/giây (--max-fps)
    bool stayAwake{true};         // --stay-awake: không để điện thoại tự khóa màn hình khi đang cắm USB
    bool turnScreenOff{false};    // --turn-screen-off: tắt màn hình điện thoại lúc điều khiển (riêng tư + tiết kiệm pin)
    bool alwaysOnTop{false};      // --always-on-top
    bool fullscreen{false};       // --fullscreen
    bool disableAudio{false};     // --no-audio (mặc định scrcpy CÓ chuyển âm thanh điện thoại sang máy tính)
    QString recordFilePath;       // --record <file>.mp4, rỗng = không ghi hình phiên điều khiển
    QString windowTitle{"One for ALL - Android"};
};

/**
 * @brief Khởi chạy/quản lý tiến trình `scrcpy.exe` (gương màn hình + điều khiển chạm/phím/chuột thời
 * gian thực qua cáp USB hoặc Wi-Fi) - xem AdbController.h vì sao dùng scrcpy có sẵn thay vì tự viết lại
 * luồng mã hóa/giải mã H.264 và tiêm sự kiện mức thấp. Tiến trình chạy NỀN, không chặn - cửa sổ gương
 * là cửa sổ RIÊNG của chính scrcpy (không nhúng vào cửa sổ Qt ở bản này).
 */
class ScrcpyLauncher : public QObject
{
    Q_OBJECT

public:
    explicit ScrcpyLauncher(QObject* parent = nullptr);
    ~ScrcpyLauncher() override;

    /// Khởi chạy một phiên mới - thất bại ngay (trả false) nếu đã có phiên đang chạy hoặc thiếu
    /// scrcpy.exe/adb.exe. Lỗi phát sinh SAU KHI tiến trình đã khởi động (vd không tìm thấy thiết bị)
    /// chỉ biết được qua tín hiệu finished()/errorOccurred(), không qua giá trị trả về của start().
    bool start(const ScrcpyOptions& options, QString* error = nullptr);

    void stop();
    bool isRunning() const;

signals:
    void started();
    void finished(int exitCode);
    void errorOccurred(QString message);

private:
    QProcess* m_process{nullptr};
    qint64 m_lastPid{0}; // PID của scrcpy.exe đã khởi chạy gần nhất - lưu riêng vì sau khi tiến trình đã
                         // thoát/bị buộc dừng, QProcess::processId() có thể trả về 0, mà vẫn cần PID này
                         // để quét dọn tiến trình adb.exe con còn sót (xem stop() trong .cpp).
};

namespace ScrcpyLauncherInternal
{
/// Dựng danh sách đối số dòng lệnh cho scrcpy.exe từ ScrcpyOptions - tách riêng để kiểm thử được mà
/// không cần chạy thật scrcpy.exe.
QStringList buildArguments(const ScrcpyOptions& options);
}
