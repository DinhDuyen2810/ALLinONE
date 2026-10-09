#include "ActionChain.h"
#include <chrono>

ActionChain ActionChain::clone() const
{
    ActionChain copy;
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    copy.id = id + "_copy_" + std::to_string(now);
    copy.name = name + " (Copy)";
    copy.description = description;
    copy.enabled = enabled;
    copy.repeatCount = repeatCount;
    // Deep-copy vector of Action values
    copy.actions = actions;
    return copy;
}

std::vector<int> ActionChain::enabledActionIndices() const
{
    std::vector<int> indices;
    for (size_t i = 0; i < actions.size(); ++i)
    {
        if (actions[i].enabled)
            indices.push_back(static_cast<int>(i));
    }
    return indices;
}
