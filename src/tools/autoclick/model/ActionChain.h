#pragma once

#include "Action.h"
#include <string>
#include <vector>

struct ActionChain
{
    std::string id;
    std::string name{"Default Chain"};
    std::string description{"Action chain description"};
    bool enabled{true};
    int repeatCount{1}; // -1 for infinite
    /// Chuỗi đang được "ghim lên" - chỉ là một nhãn ghi nhớ + kích hoạt hành động "đưa lên đầu danh sách
    /// NGAY LÚC ghim" (xem ChainListWidget::onCustomContextMenuRequested), KHÔNG phải ràng buộc sắp xếp
    /// "luôn đứng trên mọi chuỗi chưa ghim" - người dùng vẫn kéo-thả tự do sau đó (yêu cầu người dùng,
    /// v1.19.8).
    bool pinned{false};

    std::vector<Action> actions;

    /**
     * @brief Tạo bản sao độc lập (deep-copy) theo yêu cầu mục 79 trong thiết kế
     */
    ActionChain clone() const;

    /// Chỉ số (trong `actions`) của các hành động ĐANG BẬT, theo đúng thứ tự chạy. ActionRunner chỉ chạy
    /// các hành động này, nên "hành động thứ k đang chạy" KHÔNG phải hàng thứ k trong danh sách nếu có
    /// hành động bị tắt đứng trước - giao diện phải ánh xạ ngược qua danh sách này để tô đúng hàng.
    std::vector<int> enabledActionIndices() const;
};
