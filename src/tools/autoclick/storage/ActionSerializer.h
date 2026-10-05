#pragma once

#include "../model/ActionChain.h"
#include <vector>
#include <QString>

class ActionSerializer
{
public:
    static bool saveToFile(const QString& filePath, const std::vector<ActionChain>& chains);
    static bool loadFromFile(const QString& filePath, std::vector<ActionChain>& outChains);

    static QString toJsonString(const std::vector<ActionChain>& chains);
    static bool fromJsonString(const QString& jsonStr, std::vector<ActionChain>& outChains);

    static void ensureProfilesDir();
    static QString getDefaultProfilePath();
};
