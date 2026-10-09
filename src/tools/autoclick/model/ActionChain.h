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
