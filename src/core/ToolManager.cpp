#include "ToolManager.h"
#include "IconHelper.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>

ToolManager& ToolManager::instance()
{
    static ToolManager s_instance;
    return s_instance;
}

void ToolManager::registerTool(std::unique_ptr<ITool> tool)
{
    if (tool)
    {
        m_tools.push_back(std::move(tool));
    }
}

ITool* ToolManager::getTool(const QString& id) const
{
    for (const auto& tool : m_tools)
    {
        if (tool && tool->id() == id)
        {
            return tool.get();
        }
    }
    return nullptr;
}

const std::vector<std::unique_ptr<ITool>>& ToolManager::getAllTools() const
{
    return m_tools;
}

PlaceholderTool::PlaceholderTool(QString id, QString name, QString description, QString iconPath)
    : m_id(std::move(id))
    , m_name(std::move(name))
    , m_description(std::move(description))
    , m_iconPath(std::move(iconPath))
    , m_icon(IconHelper::makeBadgedIcon(m_iconPath, 36, 8, 3))
{
}

QWidget* PlaceholderTool::createWindow()
{
    auto* widget = new QWidget();
    widget->setWindowTitle(m_name);
    widget->setWindowIcon(m_icon);
    widget->resize(480, 320);
    widget->setStyleSheet("background-color: #f6f8fa; color: #1f2328; font-family: 'Segoe UI', sans-serif;");

    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(36, 36, 36, 36);
    layout->setSpacing(16);

    auto* iconLabel = new QLabel(widget);
    iconLabel->setPixmap(IconHelper::makeBadgedPixmap(m_iconPath, 84, 18, 10));
    iconLabel->setAlignment(Qt::AlignCenter);

    auto* titleLabel = new QLabel(m_name, widget);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet("color: #1f2328;");
    titleLabel->setAlignment(Qt::AlignCenter);

    auto* descCard = new QLabel(m_description + "\n\n(Tính năng này đang trong lộ trình phát triển của bộ công cụ One for ALL)", widget);
    descCard->setWordWrap(true);
    descCard->setAlignment(Qt::AlignCenter);
    descCard->setStyleSheet(
        "background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 12px; "
        "padding: 16px; color: #57606a; font-size: 13px; line-height: 1.5;"
    );

    layout->addStretch();
    layout->addWidget(iconLabel);
    layout->addWidget(titleLabel);
    layout->addWidget(descCard);
    layout->addStretch();

    return widget;
}
