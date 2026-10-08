#pragma once

#include "Tool.h"
#include <vector>
#include <memory>
#include <QString>

/**
 * @brief Quản lý danh sách các module công cụ (ToolManager)
 * Tuân thủ mục 51 trong tài liệu thiết kế.
 */
class ToolManager
{
public:
    static ToolManager& instance();

    void registerTool(std::unique_ptr<ITool> tool);
    ITool* getTool(const QString& id) const;
    const std::vector<std::unique_ptr<ITool>>& getAllTools() const;

    /// Gọi ITool::stopBackgroundWorkForQuit() trên TOÀN BỘ tool - lưới an toàn chung, nối vào
    /// QApplication::aboutToQuit (xem MainWindow.cpp) để dọn công việc nền có nguy cơ mồ côi tiến trình
    /// ngoài kể cả khi ứng dụng thoát qua qApp->quit() trực tiếp (không đi qua closeEvent() cửa sổ nào).
    void stopAllBackgroundWorkForQuit();

    /// Có tool nào (cửa sổ đang mở) đang thực hiện thao tác KHÔNG AN TOÀN để buộc dừng giữa chừng không -
    /// xem ITool::isWindowBusy(). `reason` (nếu khác nullptr) được gán tên tool đầu tiên đang bận.
    bool anyToolWindowBusy(QString* reason = nullptr) const;

private:
    ToolManager() = default;
    ~ToolManager() = default;
    ToolManager(const ToolManager&) = delete;
    ToolManager& operator=(const ToolManager&) = delete;

    std::vector<std::unique_ptr<ITool>> m_tools;
};

/**
 * @brief Lớp đại diện cho các công cụ dự kiến phát triển ở các Phase tiếp theo
 */
class PlaceholderTool : public ITool
{
public:
    PlaceholderTool(QString id, QString name, QString description, QString iconPath);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString description() const override { return m_description; }
    QString iconPath() const override { return m_iconPath; }
    QIcon icon() const override { return m_icon; }
    bool isAvailable() const override { return false; }

    QWidget* createWindow() override;

private:
    QString m_id;
    QString m_name;
    QString m_description;
    QString m_iconPath;
    QIcon m_icon;
};
