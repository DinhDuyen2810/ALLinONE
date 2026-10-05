# ONE FOR ALL

## Bộ công cụ đa chức năng viết bằng C++

**Phiên bản tài liệu:** 1.0  
**Module triển khai đầu tiên:** Auto Click  
**Ngôn ngữ:** C++20  
**GUI Framework:** Qt 6  
**Build System:** CMake  
**Nền tảng mục tiêu:** Windows 10/11

---

# 1. Tổng quan

## 1.1. Mục tiêu

`One for ALL` là một ứng dụng desktop tập hợp nhiều công cụ độc lập vào một giao diện duy nhất.

Các module dự kiến:

1. Auto Click
2. Connect Together
3. QR Tools
4. Disk Cleanup
5. Android Phone Control
6. Downloader
7. Security Gateway
8. VPN & Location
9. WiFi Connection

Ứng dụng chính chịu trách nhiệm:

- Hiển thị danh sách các công cụ.
- Mở từng công cụ trong cửa sổ riêng.
- Quản lý trạng thái các công cụ.
- Quản lý icon.
- Quản lý cấu hình chung.
- Cung cấp cơ chế mở rộng module.

Mỗi tool phải hoạt động tương đối độc lập với các tool khác.

---

# 2. Kiến trúc tổng thể

```text
+-------------------------------------------------------+
|                    ONE FOR ALL                        |
+-------------------------------------------------------+
|                                                       |
|  +-------------+     +-----------------------------+  |
|  | Tool List   |     |                             |  |
|  |             |     |       Tool Window           |  |
|  | Auto Click  | --> |                             |  |
|  | Connect     |     |       Auto Click            |  |
|  | QR Tools    |     |                             |  |
|  | Disk        |     |                             |  |
|  | Android     |     |                             |  |
|  | Downloader  |     |                             |  |
|  | Security    |     |                             |  |
|  | VPN         |     |                             |  |
|  | WiFi        |     |                             |  |
|  +-------------+     +-----------------------------+  |
|                                                       |
+-------------------------------------------------------+
```

Mỗi tool có thể được mở thành một cửa sổ riêng:

```text
OneForAll.exe
     |
     +--- AutoClickWindow
     |
     +--- ConnectTogetherWindow
     |
     +--- QRToolsWindow
     |
     +--- DiskCleanupWindow
     |
     +--- AndroidControlWindow
     |
     +--- DownloaderWindow
     |
     +--- SecurityGatewayWindow
     |
     +--- VPNLocationWindow
     |
     +--- WifiConnectionWindow
```

Trong giai đoạn đầu chỉ triển khai:

```text
OneForAll
    |
    +--- Auto Click
```

---

# 3. Auto Click

## 3.1. Mục tiêu

Auto Click không chỉ thực hiện click chuột.

Nó phải cho phép người dùng xây dựng một **chuỗi hành động**.

Ví dụ:

```text
Chuỗi: Đăng nhập hệ thống

1. Click chuột
2. Chờ 500 ms
3. Gõ username
4. Nhấn Tab
5. Gõ password
6. Nhấn Enter
7. Chờ 2 s
8. Click
9. Kéo chuột
10. Nhấn giữ
```

Người dùng có thể:

- Tạo nhiều chuỗi.
- Clone chuỗi.
- Xóa chuỗi.
- Thêm hành động.
- Sửa hành động.
- Xóa hành động.
- Clone hành động.
- Di chuyển hành động.
- Chèn hành động trước/sau.
- Chạy chuỗi.
- Chạy chuỗi nhiều lần.

---

# 4. Giao diện Auto Click

Giao diện chia thành 3 khu vực chính:

```text
+-----------------------------------------------------------------------+
| Auto Click                                                            |
+-------------------+--------------------------------+------------------+
|                   |                                |                  |
|  ACTION CHAINS    |        ACTION LIST             |    SETTINGS      |
|                   |                                |                  |
| +---------------+ |  01 Click                     | Action           |
| | Login         | |  02 Wait 500 ms               | Type: Click      |
| +---------------+ |  03 Type "admin"              |                  |
|                   |  04 Key Tab                   | Position         |
| | Download      | |  05 Type "123456"             | X: 100           |
| +---------------+ |  06 Key Enter                 | Y: 200           |
|                   |                                |                  |
|                   |                                | Wait Before      |
|                   |                                | 500 ms           |
|                   |                                |                  |
|                   |                                | Wait After       |
|                   |                                | 1000 ms          |
|                   |                                |                  |
|                   |                                | Duration         |
|                   |                                | 0 ms             |
|                   |                                |                  |
+-------------------+--------------------------------+------------------+
| [Add] [Clone] [Delete]       [Run] [Stop]       Repeat: [1]          |
+-----------------------------------------------------------------------+
```

---

# 5. Khu vực bên trái – Action Chains

Đây là danh sách các chuỗi hành động.

Ví dụ:

```text
ACTION CHAINS

+-----------------------+
| Login                 |
| Download              |
| Upload                |
| Test Web              |
| Game                  |
+-----------------------+
```

Mỗi chain gồm:

```text
Chain
├── ID
├── Name
├── Description
├── Enabled
├── RepeatCount
└── Actions[]
```

---

# 6. Thao tác với Chain

Mỗi chain hỗ trợ:

```text
+ Add
+ Clone
+ Delete
+ Rename
+ Run
```

Có thể mở context menu:

```text
Right Click

-------------------------
Run
Clone
Rename
Delete
-------------------------
Export
Import
-------------------------
```

---

# 7. Khu vực giữa – Action List

Danh sách hành động của chain.

Ví dụ:

```text
01 | Click       | X=500 Y=300
02 | Wait        | 1000 ms
03 | Type Text   | "hello"
04 | Key         | Enter
05 | Hotkey      | Win + Shift + S
06 | Scroll      | Down 5
07 | Drag        | (100,100) -> (500,500)
```

Mỗi action cần có:

```text
Index
Type
Description
Duration
Wait Before
Wait After
Enabled
```

---

# 8. Thao tác với Action

Một action phải hỗ trợ:

```text
Delete
Edit
Insert Before
Insert After
Clone
Move Up
Move Down
```

Context menu:

```text
-------------------------
Edit
Clone
-------------------------
Insert Before
Insert After
-------------------------
Move Up
Move Down
-------------------------
Delete
-------------------------
```

---

# 9. Các loại Action

Version đầu tiên hỗ trợ các loại sau:

```cpp
enum class ActionType
{
    MouseClick,
    MouseDrag,
    MouseHold,
    TypeText,
    Hotkey,
    KeyPress,
    Scroll
};
```

---

# 10. Mouse Click

## Chức năng

Nhấn chuột một lần.

Hỗ trợ:

```text
Left
Right
Middle
```

Thông tin:

```text
X
Y
Button
Wait Before
Wait After
Duration
```

Ví dụ:

```text
Mouse Click

X: 500
Y: 300

Button:
[ Left ]

Wait Before:
500 ms

Wait After:
1000 ms
```

---

# 11. Mouse Drag

Kéo chuột từ vị trí A đến B.

Thông tin:

```text
Start X
Start Y

End X
End Y

Duration
Wait Before
Wait After
```

Ví dụ:

```text
Drag

Start:
X = 100
Y = 200

End:
X = 800
Y = 600

Duration:
1000 ms
```

Thực hiện:

```text
Move -> MouseDown -> Move gradually -> MouseUp
```

Không nên thực hiện kéo bằng một lệnh di chuyển tức thời.

Phải chia thành nhiều bước nhỏ theo `Duration`.

---

# 12. Mouse Hold

Nhấn giữ chuột.

Ví dụ:

```text
Mouse Hold

Position:
X = 500
Y = 500

Button:
Left

Duration:
5000 ms
```

Thực hiện:

```text
Move mouse
MouseDown
Wait 5000 ms
MouseUp
```

---

# 13. Type Text

Cho phép gõ text.

Ví dụ:

```text
Type Text

Text:
aaaaaaaa

Duration:
1000 ms
```

Có thể hỗ trợ hai chế độ:

```text
Instant
Character by Character
```

Nếu:

```text
Duration = 1000 ms
Text = "Hello"
```

thì có thể chia:

```text
H -> 200 ms
e -> 200 ms
l -> 200 ms
l -> 200 ms
o -> 200 ms
```

Tuy nhiên cần tính lại nếu text có Unicode.

---

# 14. Hotkey

Hỗ trợ tổ hợp phím.

Ví dụ:

```text
Win + Shift + S
```

Cấu hình:

```text
Modifiers:

[✓] Win
[✓] Shift
[ ] Ctrl
[ ] Alt

Key:
S
```

Một số ví dụ:

```text
Ctrl + C
Ctrl + V
Ctrl + Shift + Esc
Alt + Tab
Win + Shift + S
```

---

# 15. Key Press

Phím đơn.

Ví dụ:

```text
Enter
Delete
Backspace
Tab
Esc
Space
F1
F2
F3
...
```

Cấu hình:

```text
Key:
[ Enter ]

Wait Before:
0

Wait After:
500
```

---

# 16. Scroll

Cuộn chuột.

Hỗ trợ:

```text
Up
Down
Left
Right
```

Ví dụ:

```text
Scroll

Direction:
Down

Amount:
5

Duration:
1000 ms
```

Có thể thực hiện:

```text
Scroll 1
Wait
Scroll 1
Wait
...
```

để tạo chuyển động tự nhiên.

---

# 17. Wait Before

Mỗi action có:

```text
Wait Before
```

Đây là thời gian chờ trước khi action bắt đầu.

Ví dụ:

```text
Action:

Click

Wait Before:
2000 ms
```

Runtime:

```text
WAIT 2000 ms
        |
        v
CLICK
```

---

# 18. Wait After

Thời gian chờ sau action.

Ví dụ:

```text
Click

Wait Before:
500 ms

Wait After:
2000 ms
```

Runtime:

```text
WAIT 500 ms
     |
     v
CLICK
     |
     v
WAIT 2000 ms
```

---

# 19. Duration

Duration là thời gian thực hiện action.

Không phải action nào cũng cần Duration.

| Action | Duration |
|---|---|
| Click | Không cần |
| Drag | Có |
| Hold | Có |
| Type Text | Có |
| Hotkey | Có thể có |
| Key Press | Có thể có |
| Scroll | Có |

---

# 20. Đơn vị thời gian

UI cho phép:

```text
Value
Unit
```

Unit:

```text
ms
s
m
h
```

Ví dụ:

```text
500 ms
2 s
1 m
1 h
```

Bên trong Core nên chuyển tất cả về:

```cpp
std::chrono::milliseconds
```

Ví dụ:

```text
2 s
```

sẽ trở thành:

```cpp
2000 ms
```

---

# 21. Khi chọn Action phải thao tác trên màn hình

Một yêu cầu quan trọng:

> Khi người dùng chọn loại hành động, ứng dụng cho phép người dùng tương tác trực tiếp với màn hình để lấy thông tin.

Ví dụ chọn:

```text
Mouse Click
```

UI hiển thị:

```text
Waiting for mouse position...

Click anywhere on screen
```

Người dùng click vào:

```text
X = 1250
Y = 720
```

Ứng dụng tự lấy:

```text
X = 1250
Y = 720
```

và đưa vào Action.

---

# 22. Coordinate Capture

Có thể triển khai bằng Win32 API.

Lấy vị trí chuột:

```cpp
POINT point;

GetCursorPos(&point);

int x = point.x;
int y = point.y;
```

---

# 23. Capture Click

Khi user chọn:

```text
Add Mouse Click
```

ứng dụng chuyển sang:

```text
Capture Mode
```

UI:

```text
+---------------------------------------+
| Capture Mouse Position               |
|                                       |
| Move mouse to target and click       |
|                                       |
| ESC = Cancel                          |
+---------------------------------------+
```

Sau khi click:

```text
X = 850
Y = 430
```

Action được tạo:

```text
Mouse Click
X = 850
Y = 430
```

---

# 24. Không được click vào chính UI

Khi capture position, nên cho phép:

```text
Minimize Auto Click window
```

hoặc sử dụng:

```text
transparent overlay
```

hoặc:

```text
global mouse hook
```

Cách phù hợp cho version đầu tiên:

```text
User chọn Add Action
        |
        v
Hide/Minimize main window
        |
        v
Global Mouse Hook
        |
        v
User click target
        |
        v
GetCursorPos()
        |
        v
Restore main window
```

---

# 25. Global Mouse Hook

Windows API:

```cpp
SetWindowsHookEx(
    WH_MOUSE_LL,
    MouseProc,
    nullptr,
    0
);
```

Callback:

```cpp
LRESULT CALLBACK MouseProc(
    int nCode,
    WPARAM wParam,
    LPARAM lParam
)
{
    if (nCode == HC_ACTION)
    {
        // xử lý mouse event
    }

    return CallNextHookEx(
        nullptr,
        nCode,
        wParam,
        lParam
    );
}
```

---

# 26. Keyboard Capture

Tương tự mouse.

Ví dụ người dùng chọn:

```text
Add Hotkey
```

Ứng dụng chuyển sang:

```text
Waiting for keyboard input...
```

Người dùng nhấn:

```text
Win + Shift + S
```

Ứng dụng ghi nhận:

```text
Win
Shift
S
```

Sau đó tạo:

```text
Hotkey:
Win + Shift + S
```

Có thể dùng:

```cpp
SetWindowsHookEx(
    WH_KEYBOARD_LL,
    KeyboardProc,
    nullptr,
    0
);
```

---

# 27. Runtime Engine

Không nên để UI trực tiếp thực hiện action.

Phải có:

```text
UI
 |
 v
Action Model
 |
 v
Action Runner
 |
 v
Input Controller
 |
 v
Windows
```

Kiến trúc:

```text
+------------------+
| AutoClickWindow  |
+--------+---------+
         |
         v
+------------------+
| ActionManager    |
+--------+---------+
         |
         v
+------------------+
| ActionRunner     |
+--------+---------+
         |
         v
+------------------+
| InputController  |
+--------+---------+
         |
         v
+------------------+
| Win32 API        |
+------------------+
```

---

# 28. Action Model

Cấu trúc cơ bản:

```cpp
struct Action
{
    ActionType type;

    std::chrono::milliseconds waitBefore{0};
    std::chrono::milliseconds waitAfter{0};
    std::chrono::milliseconds duration{0};

    bool enabled = true;

    // Mouse
    int x = 0;
    int y = 0;

    int startX = 0;
    int startY = 0;

    int endX = 0;
    int endY = 0;

    // Keyboard
    std::string text;

    // Scroll
    int scrollAmount = 0;

    // Mouse button
    int mouseButton = 0;

    // Keyboard
    int keyCode = 0;

    std::vector<int> modifiers;
};
```

---

# 29. Action Chain

```cpp
struct ActionChain
{
    std::string id;
    std::string name;
    std::string description;

    bool enabled = true;

    int repeatCount = 1;

    std::vector<Action> actions;
};
```

---

# 30. Project

Toàn bộ cấu hình:

```cpp
struct AutoClickProject
{
    std::string version;

    std::vector<ActionChain> chains;
};
```

---

# 31. Action Runner

Pseudo code:

```cpp
void ActionRunner::run(
    const ActionChain& chain
)
{
    for (int repeat = 0;
         repeat < chain.repeatCount;
         ++repeat)
    {
        for (size_t i = 0;
             i < chain.actions.size();
             ++i)
        {
            if (stopRequested)
                return;

            const auto& action =
                chain.actions[i];

            wait(action.waitBefore);

            execute(action);

            wait(action.waitAfter);
        }
    }
}
```

---

# 32. Execute Action

```cpp
void ActionRunner::execute(
    const Action& action
)
{
    switch (action.type)
    {
        case ActionType::MouseClick:
            executeMouseClick(action);
            break;

        case ActionType::MouseDrag:
            executeMouseDrag(action);
            break;

        case ActionType::MouseHold:
            executeMouseHold(action);
            break;

        case ActionType::TypeText:
            executeTypeText(action);
            break;

        case ActionType::Hotkey:
            executeHotkey(action);
            break;

        case ActionType::KeyPress:
            executeKeyPress(action);
            break;

        case ActionType::Scroll:
            executeScroll(action);
            break;
    }
}
```

---

# 33. Input Controller

Tạo class:

```cpp
class InputController
{
public:

    void moveMouse(int x, int y);

    void mouseDown(MouseButton button);

    void mouseUp(MouseButton button);

    void click(MouseButton button);

    void keyDown(int key);

    void keyUp(int key);

    void pressKey(int key);

    void hotkey(
        const std::vector<int>& modifiers,
        int key
    );

    void typeText(
        const std::string& text
    );

    void scroll(
        int amount
    );
};
```

Class này là lớp duy nhất giao tiếp với Windows Input API.

---

# 34. Mouse Click bằng Win32

Có thể sử dụng:

```cpp
SetCursorPos(x, y);
```

Sau đó:

```cpp
SendInput(...)
```

Ví dụ logic:

```text
SetCursorPos
     |
     v
Mouse Down
     |
     v
Mouse Up
```

Không nên phụ thuộc vào tọa độ UI của Qt.

---

# 35. Drag

Thuật toán:

```text
Move to Start

MouseDown

for each step:
    calculate position
    MoveMouse(position)
    Sleep(stepDuration)

MouseUp
```

Ví dụ:

```cpp
int steps = 50;

for (int i = 0; i <= steps; ++i)
{
    double t =
        static_cast<double>(i) / steps;

    int x =
        startX +
        static_cast<int>(
            (endX - startX) * t
        );

    int y =
        startY +
        static_cast<int>(
            (endY - startY) * t
        );

    moveMouse(x, y);

    std::this_thread::sleep_for(
        stepDuration
    );
}
```

---

# 36. Stop Button

Runtime phải hỗ trợ Stop.

UI:

```text
[ RUN ] [ STOP ]
```

Khi bấm Stop:

```cpp
std::atomic_bool stopRequested;
```

Runner kiểm tra liên tục:

```cpp
if (stopRequested)
    return;
```

---

# 37. Không block UI Thread

Đây là yêu cầu cực kỳ quan trọng.

Không được:

```cpp
// GUI thread
runActionChain();
```

vì:

```text
Sleep()
SendInput()
Drag()
Wait()
```

sẽ làm UI treo.

Phải chạy Runner trên:

```text
QThread
```

hoặc:

```text
std::jthread
```

Khuyến nghị:

```text
Qt UI
  |
  v
QThread
  |
  v
ActionRunner
```

---

# 38. Runtime Overlay

Khi chạy chain, phải có một box nhỏ hiển thị trạng thái.

Ví dụ:

```text
+--------------------------------------+
| Auto Click                           |
|                                      |
| Action: Type Text                    |
|                                      |
| Progress: 4 / 10                     |
|                                      |
| Next action: Press Enter             |
|                                      |
| Remaining: 1.25 s                    |
+--------------------------------------+
```

---

# 39. Overlay không được che vùng thao tác

Overlay phải tự tránh khu vực đang thao tác.

Ví dụ:

```text
Screen

+-------------------------------------------------------+
|                                                       |
|   +------------------------+                          |
|   | Browser               |                          |
|   |                        |                          |
|   |          X             |                          |
|   |          ^             |                          |
|   |          |             |                          |
|   +------------------------+                          |
|                                                       |
|                              +---------------------+  |
|                              | Auto Click          |  |
|                              | Action 4 / 10      |  |
|                              +---------------------+  |
+-------------------------------------------------------+
```

Nếu action target nằm bên phải:

```text
Overlay -> chuyển sang trái
```

Nếu target nằm bên trái:

```text
Overlay -> chuyển sang phải
```

---

# 40. Overlay Position Algorithm

Mỗi lần chuẩn bị chạy action:

```text
Target Position
       |
       v
Calculate candidate positions
       |
       +--> Top Left
       +--> Top Right
       +--> Bottom Left
       +--> Bottom Right
       |
       v
Check collision
       |
       v
Select position
```

Overlay không được nằm trên:

```text
Target Point
Target Rectangle
```

Trong version đầu:

```text
Target Point + khoảng cách an toàn
```

là đủ.

---

# 41. Countdown

Overlay cần hiển thị thời gian còn lại.

Ví dụ:

```text
Action 4 / 10

Current:
Click

Next:
Type Text

Waiting:
1.52 s
```

Nếu:

```text
Wait Before = 5000 ms
```

hiển thị:

```text
5.0
4.9
4.8
...
0.1
```

Sau đó thực hiện action.

---

# 42. Action Status

Runtime gửi event về UI:

```cpp
struct ActionRuntimeState
{
    int chainIndex;
    int actionIndex;
    int totalActions;

    ActionType currentAction;
    ActionType nextAction;

    std::chrono::milliseconds remaining;
};
```

UI nhận:

```text
ActionRunner
     |
     | signal
     v
Overlay
```

---

# 43. Signal/Slot

Qt:

```cpp
signals:

    void actionStarted(
        int index,
        int total
    );

    void actionFinished(
        int index
    );

    void countdownChanged(
        qint64 milliseconds
    );

    void chainFinished();

    void executionStopped();

    void errorOccurred(
        QString message
    );
```

---

# 44. Lưu cấu hình

Không lưu dữ liệu bằng file riêng cho từng Action.

Sử dụng JSON.

Ví dụ:

```text
profiles/
    login.json
    download.json
    test.json
```

---

# 45. JSON Structure

Ví dụ:

```json
{
    "version": "1.0",
    "chains": [
        {
            "id": "login",
            "name": "Login",
            "description": "Login website",
            "enabled": true,
            "repeatCount": 1,
            "actions": [
                {
                    "type": "MouseClick",
                    "x": 500,
                    "y": 300,
                    "waitBefore": 0,
                    "waitAfter": 500,
                    "duration": 0,
                    "enabled": true
                },
                {
                    "type": "TypeText",
                    "text": "admin",
                    "waitBefore": 100,
                    "waitAfter": 500,
                    "duration": 500,
                    "enabled": true
                },
                {
                    "type": "KeyPress",
                    "key": "TAB",
                    "waitBefore": 0,
                    "waitAfter": 300,
                    "duration": 0,
                    "enabled": true
                },
                {
                    "type": "TypeText",
                    "text": "password",
                    "waitBefore": 0,
                    "waitAfter": 300,
                    "duration": 500,
                    "enabled": true
                },
                {
                    "type": "KeyPress",
                    "key": "ENTER",
                    "waitBefore": 0,
                    "waitAfter": 2000,
                    "duration": 0,
                    "enabled": true
                }
            ]
        }
    ]
}
```

---

# 46. Import / Export

Về sau nên hỗ trợ:

```text
Export Chain
Import Chain
```

Ví dụ:

```text
Login.chain.json
```

Người dùng có thể chuyển chain sang máy khác.

---

# 47. Autosave

Khi thay đổi:

```text
Add Action
Delete Action
Edit Action
Move Action
Rename Chain
```

có thể tự động lưu.

Tuy nhiên nên có:

```text
Ctrl + S
```

và:

```text
Save
```

để người dùng chủ động lưu.

---

# 48. Undo / Redo

Nên thiết kế từ đầu để sau này hỗ trợ:

```text
Ctrl + Z
Ctrl + Y
```

Các thao tác:

```text
Add Action
Delete Action
Edit Action
Move Action
Clone Action
```

đều có thể trở thành command.

Có thể dùng Command Pattern:

```text
Command
  |
  +-- AddActionCommand
  +-- DeleteActionCommand
  +-- MoveActionCommand
  +-- EditActionCommand
  +-- CloneActionCommand
```

Version đầu có thể chưa triển khai nhưng data model phải không cản trở việc bổ sung.

---

# 49. Project Directory

Đề xuất cấu trúc project:

```text
OneForAll/
│
├── CMakeLists.txt
├── README.md
├── LICENSE
│
├── docs/
│   ├── architecture.md
│   ├── autoclick.md
│   └── roadmap.md
│
├── assets/
│   ├── icons/
│   │   ├── app.ico
│   │   ├── autoclick.svg
│   │   ├── connect.svg
│   │   ├── qr.svg
│   │   ├── disk.svg
│   │   ├── android.svg
│   │   ├── downloader.svg
│   │   ├── security.svg
│   │   ├── vpn.svg
│   │   └── wifi.svg
│   │
│   └── resources.qrc
│
├── src/
│   ├── main.cpp
│   │
│   ├── app/
│   │   ├── Application.h
│   │   └── Application.cpp
│   │
│   ├── core/
│   │   ├── Tool.h
│   │   ├── ToolManager.h
│   │   ├── SettingsManager.h
│   │   └── Logger.h
│   │
│   ├── ui/
│   │   ├── MainWindow.h
│   │   ├── MainWindow.cpp
│   │   └── MainWindow.ui
│   │
│   └── tools/
│       └── autoclick/
│           ├── AutoClickTool.h
│           ├── AutoClickTool.cpp
│           ├── AutoClickWindow.h
│           ├── AutoClickWindow.cpp
│           ├── AutoClickWindow.ui
│           │
│           ├── model/
│           │   ├── Action.h
│           │   ├── Action.cpp
│           │   ├── ActionChain.h
│           │   └── ActionChain.cpp
│           │
│           ├── engine/
│           │   ├── ActionRunner.h
│           │   ├── ActionRunner.cpp
│           │   ├── InputController.h
│           │   └── InputController.cpp
│           │
│           ├── capture/
│           │   ├── MouseCapture.h
│           │   ├── MouseCapture.cpp
│           │   ├── KeyboardCapture.h
│           │   └── KeyboardCapture.cpp
│           │
│           ├── overlay/
│           │   ├── RuntimeOverlay.h
│           │   └── RuntimeOverlay.cpp
│           │
│           ├── storage/
│           │   ├── ActionSerializer.h
│           │   └── ActionSerializer.cpp
│           │
│           └── widgets/
│               ├── ActionListWidget.h
│               ├── ActionListWidget.cpp
│               ├── ActionEditorWidget.h
│               └── ActionEditorWidget.cpp
│
├── tests/
│   ├── test_action.cpp
│   ├── test_serializer.cpp
│   └── test_runner.cpp
│
└── profiles/
```

---

# 50. Module Architecture

Mỗi tool sau này phải implement interface:

```cpp
class ITool
{
public:

    virtual ~ITool() = default;

    virtual QString id() const = 0;

    virtual QString name() const = 0;

    virtual QIcon icon() const = 0;

    virtual QWidget* createWindow() = 0;
};
```

Auto Click:

```cpp
class AutoClickTool :
    public ITool
{
public:

    QString id() const override
    {
        return "autoclick";
    }

    QString name() const override
    {
        return "Auto Click";
    }

    QIcon icon() const override;

    QWidget* createWindow() override;
};
```

Sau này:

```text
AutoClickTool
ConnectTogetherTool
QRTool
DiskCleanupTool
AndroidTool
DownloaderTool
SecurityGatewayTool
VPNLocationTool
WifiTool
```

---

# 51. Tool Manager

```cpp
class ToolManager
{
public:

    void registerTool(
        std::unique_ptr<ITool> tool
    );

    ITool* getTool(
        const QString& id
    );

private:

    std::vector<
        std::unique_ptr<ITool>
    > tools;
};
```

Khi startup:

```cpp
toolManager.registerTool(
    std::make_unique<AutoClickTool>()
);
```

Sau này:

```cpp
toolManager.registerTool(
    std::make_unique<ConnectTogetherTool>()
);

toolManager.registerTool(
    std::make_unique<QRTool>()
);
```

---

# 52. Main Window

Main window chỉ cần quản lý Tool.

Ví dụ:

```text
+------------------------------------------------+
| One for ALL                                    |
+----------------------+-------------------------+
|                      |                         |
| Tools                |                         |
|                      |                         |
| [icon] Auto Click    |                         |
| [icon] Connect       |                         |
| [icon] QR Tools      |                         |
| [icon] Disk Cleanup  |                         |
| [icon] Android       |                         |
| [icon] Downloader    |                         |
| [icon] Security      |                         |
| [icon] VPN           |                         |
| [icon] WiFi          |                         |
|                      |                         |
+----------------------+-------------------------+
```

Click:

```text
Auto Click
```

sẽ mở:

```text
AutoClickWindow
```

---

# 53. Cửa sổ Tool độc lập

Mỗi tool có thể có:

```text
Qt::Window
```

hoặc:

```cpp
window->show();
```

Không nên nhét toàn bộ UI của tất cả tool vào một widget duy nhất.

Mục tiêu:

```text
MainWindow
     |
     +--> AutoClickWindow
     |
     +--> QRWindow
     |
     +--> DownloaderWindow
```

---

# 54. Auto Click UI chi tiết

Nên chia Auto Click thành:

```text
AutoClickWindow
│
├── ChainListWidget
│
├── ActionListWidget
│
├── ActionEditorWidget
│
├── Toolbar
│
└── RuntimeOverlay
```

---

# 55. Toolbar

Toolbar:

```text
+-------------------------------------------------------+
| New Chain | Clone | Delete | Add Action | Save       |
+-------------------------------------------------------+
```

Khi chọn action:

```text
+-------------------------------------------------------+
| Edit | Clone | Before | After | Up | Down | Delete   |
+-------------------------------------------------------+
```

---

# 56. Add Action Dialog

Khi nhấn:

```text
Add Action
```

hiển thị:

```text
+------------------------------------+
| Add Action                         |
+------------------------------------+
|                                    |
| Action Type                        |
| [ Mouse Click              v ]     |
|                                    |
| [ Continue ] [ Cancel ]            |
+------------------------------------+
```

Danh sách:

```text
Mouse Click
Mouse Drag
Mouse Hold
Type Text
Hotkey
Key Press
Scroll
```

---

# 57. Mouse Click Editor

```text
+--------------------------------------+
| Mouse Click                          |
+--------------------------------------+
| Button: [ Left v ]                   |
|                                      |
| Position                             |
| X: [ 500 ]                           |
| Y: [ 300 ]                           |
|                                      |
| [ Capture Position ]                 |
|                                      |
| Wait Before: [ 500 ] [ ms v ]        |
| Wait After:  [ 1000 ] [ ms v ]       |
| Duration:    [ 0 ] [ ms v ]          |
|                                      |
| [ Apply ] [ Cancel ]                 |
+--------------------------------------+
```

---

# 58. Drag Editor

```text
+--------------------------------------+
| Mouse Drag                           |
+--------------------------------------+
| Start                                |
| X: [100]   Y: [200]                  |
| [Capture]                            |
|                                      |
| End                                  |
| X: [800]   Y: [600]                  |
| [Capture]                            |
|                                      |
| Duration: [1000] [ms]                |
|                                      |
| Wait Before: [0] [ms]                |
| Wait After:  [0] [ms]                |
+--------------------------------------+
```

---

# 59. Type Text Editor

```text
+--------------------------------------+
| Type Text                            |
+--------------------------------------+
| Text:                                |
| +----------------------------------+ |
| | username                         | |
| +----------------------------------+ |
|                                      |
| Duration: [1000] [ms]                |
|                                      |
| Mode:                                |
| ( ) Instant                          |
| ( ) Character by Character           |
+--------------------------------------+
```

---

# 60. Hotkey Editor

```text
+--------------------------------------+
| Hotkey                              |
+--------------------------------------+
| Ctrl     [ ]                         |
| Alt      [ ]                         |
| Shift    [✓]                         |
| Win      [✓]                         |
|                                      |
| Key: [ S ]                           |
|                                      |
| Result: Win + Shift + S              |
+--------------------------------------+
```

---

# 61. Chain Execution

UI:

```text
Repeat:

[ 1 ]

[ Run ]
[ Stop ]
```

Có thể nhập:

```text
1
5
10
100
```

hoặc:

```text
Infinite
```

Nếu hỗ trợ infinite:

```cpp
repeatCount = -1;
```

---

# 62. Execution Flow

Ví dụ:

```text
Chain Login
Actions = 6
Repeat = 2
```

Runtime:

```text
Round 1
  |
  +-- Action 1
  +-- Action 2
  +-- Action 3
  +-- Action 4
  +-- Action 5
  +-- Action 6
  |
  v
Round 2
  |
  +-- Action 1
  +-- Action 2
  ...
```

Overlay:

```text
Round 1/2
Action 3/6
```

---

# 63. Error Handling

Nếu action lỗi:

```text
Action 4 failed
```

không được làm ứng dụng crash.

Có thể cấu hình:

```text
On Error:

( ) Stop
( ) Continue
( ) Retry
```

Version đầu nên dùng:

```text
Stop
```

---

# 64. Logging

Log:

```text
logs/
    app.log
    autoclick.log
```

Ví dụ:

```text
2026-09-21 14:20:10
[INFO]
Starting chain: Login

2026-09-21 14:20:11
[INFO]
Action 1/6: MouseClick

2026-09-21 14:20:12
[INFO]
Action 2/6: TypeText
```

---

# 65. Hotkey điều khiển Auto Click

Nên hỗ trợ global hotkey:

```text
F6 = Start
F7 = Stop
F8 = Pause
```

Version đầu:

```text
F6 Start
F7 Stop
```

Windows:

```cpp
RegisterHotKey(...)
```

---

# 66. Pause

Nên thiết kế Runner có trạng thái:

```cpp
enum class RunnerState
{
    Idle,
    Running,
    Paused,
    Stopping,
    Finished,
    Error
};
```

Sau này:

```text
Run
Pause
Resume
Stop
```

---

# 67. Thread Safety

Các biến:

```cpp
stopRequested
pauseRequested
```

nên dùng:

```cpp
std::atomic_bool
```

Không dùng biến bool thông thường giữa UI thread và Runner thread.

Ví dụ:

```cpp
std::atomic_bool stopRequested{false};
std::atomic_bool pauseRequested{false};
```

---

# 68. DPI Awareness

Đây là vấn đề rất quan trọng.

Nếu Windows đang scale:

```text
100%
125%
150%
200%
```

thì tọa độ có thể bị sai nếu ứng dụng không DPI aware.

Ứng dụng phải thiết lập:

```text
Per-Monitor DPI Awareness
```

để:

```text
Screen coordinate
```

khớp với:

```text
GetCursorPos()
```

và:

```text
SetCursorPos()
```

---

# 69. Multi Monitor

Phải hỗ trợ nhiều màn hình.

Ví dụ:

```text
Monitor 1
x = 0 ... 1920

Monitor 2
x = 1920 ... 3840
```

Có thể xuất hiện tọa độ âm:

```text
X = -500
Y = 300
```

Không được giả định:

```text
x >= 0
y >= 0
```

---

# 70. Coordinate System

Nên lưu:

```text
Screen Coordinate
```

thay vì:

```text
Window Coordinate
```

vì Auto Click hoạt động trên toàn desktop.

Ví dụ:

```json
{
    "x": 1920,
    "y": 500
}
```

---

# 71. Elevated Application

Một số ứng dụng chạy với quyền Administrator.

Nếu:

```text
OneForAll = normal user
Target = Administrator
```

thì Windows có thể giới hạn input.

Cần document rõ:

```text
Nếu target chạy elevated,
OneForAll có thể cần chạy cùng mức quyền.
```

Không nên tự động yêu cầu Administrator ngay từ đầu nếu không cần thiết.

---

# 72. Security

Không nên lưu:

```text
password
token
API key
```

dưới dạng plaintext nếu sau này tool có các chức năng liên quan.

Riêng Auto Click:

```text
Type Text
```

có thể chứa password.

Do đó UI có thể hỗ trợ:

```text
Sensitive Text
```

và hiển thị:

```text
********
```

nhưng đây là phần có thể triển khai sau.

---

# 73. Action Description

Để UI dễ đọc, mỗi Action nên có function:

```cpp
QString Action::description() const;
```

Ví dụ:

```text
Click Left at (500,300)

Type "admin"

Press Enter

Hotkey Win + Shift + S

Scroll Down 5

Drag (100,100) -> (800,600)
```

---

# 74. Action List UI

Nên hiển thị:

```text
# | Action | Description | Before | After | Duration
```

Ví dụ:

```text
1 | Click | Left (500,300) | 0ms | 500ms | -
2 | Type  | "admin"       | 0ms | 500ms | 500ms
3 | Key   | Enter         | 0ms | 2s    | -
```

---

# 75. Drag & Drop Action

Nên cho phép người dùng kéo action bằng chuột:

```text
Action 1
Action 2
Action 3
Action 4
```

Kéo:

```text
Action 4
```

lên:

```text
Action 2
```

thành:

```text
Action 1
Action 4
Action 2
Action 3
```

Đây là UX tốt hơn chỉ có Up/Down.

---

# 76. Clone Action

Ví dụ:

```text
1 Click
2 Type username
3 Enter
```

Clone action 2:

```text
1 Click
2 Type username
3 Type username
4 Enter
```

---

# 77. Insert Before

Nếu chọn:

```text
Action 3
```

và:

```text
Insert Before
```

action mới trở thành:

```text
Action 1
Action 2
New Action
Action 3
```

---

# 78. Insert After

Tương tự:

```text
Action 1
Action 2
Action 3
New Action
Action 4
```

---

# 79. Chain Clone

Ví dụ:

```text
Login
```

Clone:

```text
Login
Login Copy
```

Tất cả actions được deep-copy.

Không được để hai chain dùng chung vector/action object theo kiểu reference không kiểm soát.

---

# 80. Data Flow hoàn chỉnh

```text
USER
 |
 | Click Add Action
 v
UI
 |
 | Select Mouse Click
 v
Capture Controller
 |
 | Global Mouse Hook
 v
Windows Desktop
 |
 | User Click
 v
Capture Position
 |
 | X,Y
 v
Action Model
 |
 v
Action List
 |
 | Save
 v
JSON
 |
 | Run
 v
Action Runner
 |
 v
Input Controller
 |
 v
Win32 SendInput
 |
 v
TARGET APPLICATION
```

---

# 81. Trạng thái Auto Click

```cpp
enum class AutoClickState
{
    Idle,
    Capturing,
    Running,
    Paused,
    Stopping,
    Finished,
    Error
};
```

---

# 82. State Machine

```text
Idle
 |
 | Add Action
 v
Capturing
 |
 | Capture Complete
 v
Idle
 |
 | Run
 v
Running
 |
 +---- Pause ----> Paused
 |                   |
 |                   +---- Resume ----> Running
 |
 +---- Stop -------> Stopping
 |
 +---- Finished ---> Finished
 |
 +---- Error ------> Error
```

---

# 83. Roadmap triển khai

Không nên code tất cả một lần.

Chia thành các Phase.

## Phase 1 – Project Foundation

Mục tiêu:

```text
C++20
Qt6
CMake
Windows
```

Hoàn thành:

```text
[ ] Tạo project
[ ] MainWindow
[ ] Tool Manager
[ ] Auto Click Tool
[ ] Icon
```

## Phase 2 – Auto Click Model

Hoàn thành:

```text
[ ] Action
[ ] ActionChain
[ ] ActionType
[ ] JSON serialization
[ ] JSON deserialization
```

## Phase 3 – Auto Click UI

Hoàn thành:

```text
[ ] Chain list
[ ] Action list
[ ] Setting panel
[ ] Add action
[ ] Edit action
[ ] Delete action
[ ] Clone action
[ ] Insert before
[ ] Insert after
[ ] Move
```

## Phase 4 – Mouse Capture

Hoàn thành:

```text
[ ] Get mouse position
[ ] Global mouse hook
[ ] Capture click
[ ] Capture drag start
[ ] Capture drag end
```

## Phase 5 – Keyboard Capture

Hoàn thành:

```text
[ ] Key capture
[ ] Hotkey capture
[ ] Key press
```

## Phase 6 – Runtime

Hoàn thành:

```text
[ ] ActionRunner
[ ] Background thread
[ ] Wait Before
[ ] Wait After
[ ] Duration
[ ] Repeat
[ ] Stop
```

## Phase 7 – Overlay

Hoàn thành:

```text
[ ] Current Action
[ ] X/Y progress
[ ] Countdown
[ ] Next Action
[ ] Auto reposition
```

## Phase 8 – Quality

Hoàn thành:

```text
[ ] Undo
[ ] Redo
[ ] Global Hotkey
[ ] Multi monitor
[ ] DPI
[ ] Error handling
[ ] Logging
[ ] Import
[ ] Export
```

---

# 84. MVP Version

Version đầu tiên chỉ cần:

```text
Auto Click
│
├── Chain
│   ├── Add
│   ├── Delete
│   ├── Clone
│   └── Run
│
├── Action
│   ├── Mouse Click
│   ├── Mouse Drag
│   ├── Mouse Hold
│   ├── Type Text
│   ├── Hotkey
│   ├── Key Press
│   └── Scroll
│
├── Timing
│   ├── Wait Before
│   ├── Wait After
│   └── Duration
│
├── Capture
│   ├── Mouse Position
│   └── Keyboard
│
├── Runtime
│   ├── Run
│   ├── Stop
│   └── Repeat
│
└── Overlay
    ├── Current Action
    ├── Progress
    └── Countdown
```

---

# 85. Thứ tự code thực tế

Nên code theo đúng thứ tự sau:

```text
01. Tạo CMake project
02. Cấu hình Qt6
03. Tạo MainWindow
04. Tạo Tool interface
05. Tạo ToolManager
06. Tạo AutoClickTool
07. Tạo AutoClickWindow
08. Tạo Action model
09. Tạo ActionChain model
10. Làm Chain List
11. Làm Action List
12. Làm Action Editor
13. Làm Add Action
14. Làm Delete Action
15. Làm Clone Action
16. Làm Move Action
17. Làm JSON
18. Làm Mouse Capture
19. Làm Keyboard Capture
20. Làm InputController
21. Làm ActionRunner
22. Làm Thread
23. Làm Stop
24. Làm Repeat
25. Làm Runtime Overlay
26. Làm Countdown
27. Làm Auto Position Overlay
28. Làm Global Hotkey
29. Làm Logging
30. Test
```

---

# 86. Test Case cơ bản

## TC01 – Mouse Click

```text
Action:
Mouse Click

X = 500
Y = 500
```

Expected:

```text
Mouse di chuyển tới 500,500
Click Left
```

## TC02 – Wait Before

```text
Wait Before = 2000 ms
```

Expected:

```text
Không click trong 2 giây đầu.
Sau 2 giây mới click.
```

## TC03 – Wait After

```text
Click
Wait After = 3000 ms
```

Expected:

```text
Action tiếp theo chỉ bắt đầu sau 3 giây.
```

## TC04 – Drag

```text
Start = 100,100
End = 500,500
Duration = 2000 ms
```

Expected:

```text
MouseDown
Move trong 2 giây
MouseUp
```

## TC05 – Type Text

```text
Text = Hello
```

Expected:

```text
Hello
```

## TC06 – Hotkey

```text
Win + Shift + S
```

Expected:

```text
Windows Screenshot UI xuất hiện.
```

## TC07 – Repeat

```text
Repeat = 5
```

Expected:

```text
Toàn bộ chain chạy 5 lần.
```

## TC08 – Stop

```text
Chain đang chạy
```

Click:

```text
Stop
```

Expected:

```text
Runner dừng.
Không thực hiện action tiếp theo.
```

---

# 87. Quy tắc thiết kế quan trọng

## Rule 1 – UI không trực tiếp điều khiển chuột

Sai:

```text
UI -> SendInput
```

Đúng:

```text
UI
 |
 v
ActionRunner
 |
 v
InputController
 |
 v
Win32
```

## Rule 2 – Action không phụ thuộc UI

Không được để:

```cpp
Action
```

biết:

```text
QWidget
QListWidget
QDialog
```

Model phải độc lập.

## Rule 3 – Win32 chỉ nằm ở Input Layer

Ví dụ:

```text
autoclick/
    engine/
        InputController.cpp
```

Không gọi:

```cpp
SendInput()
```

lung tung trong UI.

## Rule 4 – Tất cả thời gian dùng std::chrono

Không dùng integer rải rác kiểu:

```cpp
int wait = 500;
```

mà nên:

```cpp
std::chrono::milliseconds
```

## Rule 5 – Runner không chạy trên GUI thread

---

# 88. Công nghệ đề xuất

| Thành phần | Công nghệ |
|---|---|
| Language | C++20 |
| GUI | Qt 6 |
| Build | CMake |
| OS | Windows 10/11 |
| Mouse Input | Win32 SendInput |
| Keyboard Input | Win32 SendInput |
| Global Mouse Hook | WH_MOUSE_LL |
| Global Keyboard Hook | WH_KEYBOARD_LL |
| Global Hotkey | RegisterHotKey |
| Config | JSON |
| Logging | Qt logging / spdlog |
| Thread | QThread / std::jthread |
| Testing | Qt Test / Catch2 |
| Icons | SVG/ICO |

---

# 89. Dependency tối thiểu

Không nên kéo quá nhiều dependency ở version đầu.

Có thể bắt đầu:

```text
Qt6
CMake
MSVC
Windows SDK
```

Sau này nếu cần:

```text
spdlog
nlohmann/json
```

Nhưng Qt đã có:

```cpp
QJsonObject
QJsonArray
QJsonDocument
```

nên version đầu có thể dùng luôn Qt JSON.

---

# 90. Compiler

Khuyến nghị:

```text
Visual Studio 2022
```

với:

```text
MSVC
C++20
Windows SDK
Qt 6.x
```

Không nên bắt đầu bằng MinGW nếu mục tiêu chính là Windows + Win32 API + deployment ổn định.

---

# 91. Build Configuration

Có:

```text
Debug
Release
```

Debug:

```text
OneForAll.exe
```

Release:

```text
OneForAll.exe
Qt DLL
platforms/
plugins/
assets/
```

Sau này có thể dùng:

```text
windeployqt
```

để đóng gói Qt.

---

# 92. Versioning

Nên có:

```cpp
APP_VERSION_MAJOR
APP_VERSION_MINOR
APP_VERSION_PATCH
```

Ví dụ:

```text
1.0.0
```

Auto Click profile cũng có:

```json
{
    "version": "1.0"
}
```

để sau này thay đổi format JSON vẫn migrate được.

---

# 93. Roadmap toàn bộ One for ALL

Sau khi Auto Click hoàn thành:

```text
V1
 |
 +-- Auto Click
 |
 v
V2
 |
 +-- Connect Together
 |
 +-- QR Tools
 |
 v
V3
 |
 +-- Disk Cleanup
 |
 +-- Android Phone Control
 |
 v
V4
 |
 +-- Downloader
 |
 +-- Security Gateway
 |
 v
V5
 |
 +-- VPN & Location
 |
 +-- WiFi Connection
```

Mỗi module vẫn tuân thủ:

```text
ITool
  |
  +-- UI
  +-- Core
  +-- Service
  +-- Storage
```

---

# 94. Kết quả mong muốn của Auto Click V1

Sau khi hoàn thành V1, người dùng có thể mở:

```text
One for ALL
    |
    +--- Auto Click
```

Sau đó tạo:

```text
Chain:
"Test Website"
```

và thêm:

```text
1. Click tại (500,300)
2. Chờ 1 giây
3. Type "username"
4. Press Tab
5. Type "password"
6. Press Enter
7. Chờ 2 giây
8. Hotkey Win + Shift + S
9. Scroll Down
10. Drag chuột
```

Sau đó:

```text
Repeat = 5
```

nhấn:

```text
RUN
```

và hệ thống:

```text
+----------------------------------+
| Auto Click                       |
|                                  |
| Action: Type Text                |
|                                  |
| Progress: 3 / 10                 |
|                                  |
| Waiting: 0.53 s                  |
|                                  |
| Next: Press Tab                  |
+----------------------------------+
```

tự động di chuyển box ra khỏi vùng đang thao tác.

---

# 95. Kiến trúc cuối cùng của Auto Click

```text
                    ONE FOR ALL
                         |
                         v
                +------------------+
                | AutoClickWindow  |
                +--------+---------+
                         |
          +--------------+--------------+
          |              |              |
          v              v              v
   Chain Manager    Action List    Action Editor
          |              |              |
          +--------------+--------------+
                         |
                         v
                   Action Model
                         |
                         v
                  Action Runner
                         |
              +----------+----------+
              |                     |
              v                     v
       Timing Controller      Input Controller
                                    |
                                    v
                               Win32 API
                                    |
                                    v
                              Windows Desktop


Runtime:

Action Runner
     |
     +----> Runtime Overlay
     |
     +----> UI Signals
```

---

# 96. Kết luận

Điểm quan trọng nhất của dự án này là **không xây Auto Click thành một file/code monolithic**.

Ngay từ version đầu nên chia thành:

```text
OneForAll
│
├── Core
│
├── UI
│
└── Tools
    │
    └── AutoClick
        │
        ├── Model
        ├── Engine
        ├── Capture
        ├── Overlay
        ├── Storage
        └── Widgets
```

Trong đó:

```text
Model
```

quản lý dữ liệu.

```text
Capture
```

lấy thao tác từ người dùng.

```text
Engine
```

thực thi chuỗi.

```text
InputController
```

giao tiếp với Windows.

```text
Overlay
```

hiển thị trạng thái runtime.

```text
Storage
```

lưu/đọc JSON.

```text
Widgets
```

chỉ phụ trách giao diện.

Thiết kế này cho phép sau khi hoàn thành Auto Click, các module:

```text
Connect Together
QR Tools
Disk Cleanup
Android Phone Control
Downloader
Security Gateway
VPN & Location
WiFi Connection
```

được thêm vào mà không phải viết lại kiến trúc của ứng dụng.
