#pragma once

class QApplication;

/// Điểm vào của TIẾN TRÌNH CHẠY KẾ HOẠCH DỜI PHÂN VÙNG: `OneForAll.exe --run-partition-plan --plan-b64 <kế hoạch> --wait-pid <pid> --cleanup-dir <thư mục>`.
/// Do chính ứng dụng khởi chạy qua UAC từ một bản sao trong thư mục tạm (xem PartitionRelocator.h). `app` đã được main() dựng + áp giao diện.
/// Thêm `--selftest`: chỉ dựng cửa sổ (không hiện, không chạy gì), in "RUNNER_SELFTEST_OK" ra stdout rồi thoát 0 - để test kiểm tra bản sao tối thiểu chạy được.
int runPartitionPlanMode(QApplication& app, int argc, char* argv[]);
