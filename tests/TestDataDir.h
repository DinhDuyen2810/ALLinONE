#pragma once

// Thư mục dữ liệu tạm dùng chung cho mọi bộ test: tạo thư mục tạm, ép AppPaths vào đó, và - khác
// QTemporaryDir trần - XÓA ĐƯỢC nó khi `main` kết thúc.
//
// Nguyên nhân thật của hàng trăm thư mục "<tên test>-XXXXXX" sót lại trong %TEMP% (đã tái hiện: chạy
// qr_ui_tests, thư mục còn lại chỉ chứa `logs\app.log` + `logs\autoclick.log`): Logger là singleton tĩnh,
// sống tới SAU khi `main` trả về và giữ hai tệp log mở suốt. `QTemporaryDir dataDir;` khai báo trong `main`
// bị hủy TRƯỚC Logger; trên Windows tệp đang mở không xóa được, nên removeRecursively() xóa hết phần còn
// lại rồi thất bại ở thư mục `logs\` - và thư mục tạm nằm lại mãi. Bộ nào không đụng tới Logger thì không rò.
//
// Dùng thay cho `QTemporaryDir dataDir;` (kế thừa QTemporaryDir nên path()/filePath()/isValid() và các hàm
// nhận `const QTemporaryDir&` dùng như cũ). Khai báo NGAY SAU đối tượng ứng dụng, trước mọi thứ đụng tới
// Logger/các store.
#include <QDir>
#include <QTemporaryDir>
#include <QThread>

#include "core/AppPaths.h"
#include "core/Logger.h"

class TestDataDir : public QTemporaryDir
{
public:
    TestDataDir()
    {
        if (isValid())
            AppPaths::setDataDirOverride(path());
    }

    ~TestDataDir()
    {
        if (!isValid())
            return;
        // Đóng hai tệp log TRƯỚC khi xóa (và không cho Logger mở lại: dòng log nào tới sau đây - vd từ
        // destructor của biến tĩnh - chỉ còn ra kênh debug, không dựng lại thư mục vừa xóa). Gọi instance()
        // chứ không kiểm "Logger đã tạo chưa": nếu chưa thì tạo luôn bây giờ, lúc `main` còn chạy, để nó
        // không được tạo lần đầu SAU khi thư mục đã bị xóa.
        Logger::instance().closeFiles();

        // Thử lại ngắn: phần mềm diệt virus/trình lập chỉ mục có thể còn giữ một tệp vừa ghi trong vài
        // chục mili giây. Hết ~1 giây vẫn không xóa được thì destructor của QTemporaryDir (chạy ngay sau
        // đây) thử lần cuối và tự in cảnh báo kèm đường dẫn.
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (QDir(path()).removeRecursively())
                break;
            QThread::msleep(50);
        }
    }

    TestDataDir(const TestDataDir&) = delete;
    TestDataDir& operator=(const TestDataDir&) = delete;
};
