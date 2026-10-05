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
};
