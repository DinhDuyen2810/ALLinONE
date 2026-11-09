#include "PartitionRunMain.h"

#include "PartitionRunWindow.h"
#include "core/Logger.h"
#include "engine/PartitionRelocator.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>

#include <cstdio>
#include <cstring>

int runPartitionPlanMode(QApplication& app, int argc, char* argv[])
{
    QString planArg, cleanupDir;
    quint32 waitPid = 0;
    bool selftest = false;
    for (int i = 2; i < argc; ++i)
    {
        const bool hasValue = i + 1 < argc;
        if (std::strcmp(argv[i], "--selftest") == 0)
            selftest = true;
        else if (std::strcmp(argv[i], "--plan-b64") == 0 && hasValue)
            planArg = QString::fromLocal8Bit(argv[++i]);
        else if (std::strcmp(argv[i], "--wait-pid") == 0 && hasValue)
            waitPid = QString::fromLocal8Bit(argv[++i]).toUInt();
        else if (std::strcmp(argv[i], "--cleanup-dir") == 0 && hasValue)
            cleanupDir = QString::fromLocal8Bit(argv[++i]);
    }

    PartitionRelocator::Plan plan;
    QString error;
    if (!PartitionRelocator::decodeArgument(planArg, &plan, &error))
    {
        Logger::instance().info("PartitionRun", "Kế hoạch không hợp lệ: " + error);
        std::fprintf(stderr, "PARTITION_PLAN_INVALID: %s\n", qPrintable(error));
        return 2;
    }
    Logger::instance().info("PartitionRun", QString("Khởi động chế độ chạy kế hoạch từ %1").arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath())));

    PartitionRunWindow window(plan, waitPid, cleanupDir);
    if (selftest)
    {
        std::printf("RUNNER_SELFTEST_OK\n");
        std::fflush(stdout);
        return 0;
    }
    window.show();
    window.raise();
    window.activateWindow();
    return app.exec();
}
