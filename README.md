<div align="center">

# SimpleDraw - Win32 GDI 图形绘制引擎架构与机制解析

[![Windows](https://img.shields.io/badge/Platform-Windows-blue.svg)](https://github.com/)
[![Language](https://img.shields.io/badge/Language-C%2B%2B-green.svg)](https://isocpp.org/)
[![API](https://img.shields.io/badge/API-Win32-orange.svg)](https://docs.microsoft.com/en-us/windows/win32/)

</div>

## 1. 系统概述

SimpleDraw 是一个基于纯 Win32 API 与 GDI 以及 C++ 现代标准库构建的轻量级 2D 矢量图形绘制应用程序，支持基础的线段和矩形的绘制、左键绘图右键擦除、撤销、清除和保存功能。本系统摒弃了高层 UI 框架的封装，采用经典的**事件驱动（Event-Driven）架构**，直接与 Windows 底层消息队列交互。

系统采用了双缓冲渲染技术实现无闪烁绘图，并使用工作线程处理 BMP 格式文件保存等耗时操作，确保在进行磁盘读写等耗时操作时，UI 线程保持绝对的流畅和响应。

---

## 2. 核心状态机与工作流 (事件路由)

系统的运行围绕主消息循环（Message Pump）展开，所有的用户交互与内部状态流转均由 `WndProc` (窗口过程函数) 统一路由分发。

### 2.1 顶部工具栏交互流转 (WM_COMMAND)

- **线段 (ID_BTN_LINE) / 矩形 (ID_BTN_RECT):** 点击后修改全局状态变量 `g_currentType`，并同步更新窗口标题栏，为用户提供当前的绘图模式反馈。
- **撤销 (ID_BTN_UNDO):** 触发命令回退逻辑。系统从 `g_undoStack` 弹出最后一次操作记录：
  - 若为 `Add`（添加）：根据记录的索引，从 `g_shapes` 中安全擦除。
  - 若为 `Delete`（删除）：为防止索引越界或错乱，将历史图形按原索引**降序排序**后重新插入 `g_shapes`。
  - 操作完成后，调用 `InvalidateRect` 发起局部重绘。
- **清除 (ID_BTN_CLEAR):** 将当前画布上所有的图形打包为一个 `OpType::Delete` 操作压入撤销栈，清空 `g_shapes` 容器，标记画板为未保存状态，并请求重绘。
- **保存 (ID_BTN_SAVE):** 触发异步文件 I/O 流水线。通过 `GetSaveFileName` 调出系统对话框获取路径，锁定 UI 保存状态 (`g_bSaving = true`)，禁用保存按钮以防重入冲突，最后拉起后台工作线程执行保存逻辑。

### 2.2 鼠标核心交互逻辑

鼠标事件构成了画板的底层交互核心，利用严格的边界检测和数学算法保证绘制与擦除的精准度。

- **WM_LBUTTONDOWN (左键按下 - 起笔):**
  调用 `IsPointInCanvas` 确保落笔点在合法绘图区。触发状态翻转 (`g_isDrawing = true`)，记录起点。随后调用 `SetCapture(hWnd)` 强制捕获鼠标，确保即使鼠标快速拖拽出窗口区域，系统依然能接收到后续的移动和抬起消息。
- **WM_MOUSEMOVE (鼠标移动 - 预览):**
  在 `g_isDrawing` 状态下，利用 `max` 和 `min` 函数将当前鼠标坐标钳制在画布的 `RECT` 边界内。调用 `InvalidateRect` 触发高频重绘，在 `WM_PAINT` 中以红色虚线 (`PS_DOT`) 实时渲染预览图形。
- **WM_LBUTTONUP (左键释放 - 落笔):**
  执行防抖动校验：计算起点与终点的曼哈顿距离是否大于 `MIN_DRAW_DISTANCE`，丢弃原地点击产生的微小无效图形。校验通过后，将新图形推入 `g_shapes`，封装为 `OpType::Add` 压入撤销栈，最后调用 `ReleaseCapture()` 释放鼠标控制权。
- **WM_RBUTTONDOWN (右键擦除 - 命中测试):**
  采取**逆序遍历**策略（从 `g_shapes` 尾部向前遍历），这符合 Z-Index 的视觉覆盖逻辑（优先擦除最上层图形）。
  - **矩形命中:** 使用 Win32 API `InflateRect` 结合 `HIT_TOLERANCE` 扩展点击容差，通过 `PtInRect` 判断。
  - **线段命中:** 利用向量点积计算点击点到线段的投影比例参数，将其钳制在 `[0,1]` 区间内得出投影点坐标，最后计算真实点与投影点的欧几里得距离，判定是否在容差范围内。命中的图形被剥离渲染树，并作为 Delete 操作打入撤销栈。

---

## 3. 跨线程消息通信机制：SendMessage vs PostMessage

在 C++ Win32 开发中，跨线程通信的机制选择直接决定了程序的稳定性和性能。本系统通过两个自定义消息完美演示了这两种机制的本质区别。

### 3.1 SendMessage：同步阻塞调度 (Synchronous)

- **应用场景:** 工作线程向主线程查询当前画板上的图形总数。
- **代码实现:** `SendMessage(p->hWnd, WM_GET_SHAPE_COUNT, 0, 0);`
- **底层机制:** 当工作线程调用 `SendMessage` 时，它会**直接绕过消息队列**，跨线程调用主窗口的 `WndProc` 函数。
  **关键点：** 此时工作线程会被**操作系统强行挂起（阻塞）**。它必须在原地死等，直到主线程的 `WndProc` 捕获到该消息，执行完毕并返回 `LRESULT` 后，工作线程才会被唤醒，拿到返回值并继续执行下一行代码。
- **业务价值:** 适用于工作线程的下一步逻辑**强依赖**主线程即时返回的数据的场景，保证了时序的绝对一致性。

### 3.2 PostMessage：异步投递队列 (Asynchronous)

- **应用场景:** 工作线程完成 BMP 文件的磁盘写入后，通知主线程恢复 UI 状态（重新启用保存按钮）。
- **代码实现:** `PostMessage(p->hWnd, WM_SAVE_COMPLETE, success ? 1 : 0, 0);`
- **底层机制:** 当工作线程调用 `PostMessage` 时，操作系统仅仅是将 `WM_SAVE_COMPLETE` 这个消息打包，**丢入主线程绑定的消息队列 (Message Queue) 的尾部，然后立刻返回 `TRUE`**。
  **关键点：** 工作线程**完全不关心也不等待**主线程何时处理这个消息。投递完成后，工作线程直接执行清理逻辑并优雅退出 (`return 0;`)。主线程会在其自身的 `GetMessage` 循环中，在未来的某个时间片取出并处理该消息。
- **业务价值:** 适用于纯粹的状态通知。它实现了线程间的解耦，确保工作线程能以最快速度释放其占用的内存和系统句柄，避免了潜在的死锁风险。

---

## 4. 多线程架构设计：UI 线程与工作线程的分工

如果在单线程中处理高分辨率图像的内存对齐和磁盘 I/O，势必会阻塞主循环，导致窗口出现“未响应”或“白屏”的假死现象。本系统采用了标准的 **Main-Worker** 并发架构。

### 4.1 主线程 (UI Thread)

- **核心职责:** 负责维持 `GetMessage` -> `DispatchMessage` 的高速流转。专门处理用户的鼠标/键盘输入，以及通过 `WM_PAINT` 维持 GDI 的双缓冲屏幕渲染。
- **设计铁律:** 绝对禁止在主线程中执行任何阻塞型的 API 调用（如 `Sleep`, `WaitForSingleObject`(无限期等待) 或繁重的文件读写 `WriteFile`）。

### 4.2 工作线程 (Worker Thread)

- **核心职责:** 负责承接重度计算与 I/O 任务。在本系统中，具体表现为 `SaveThreadProc` 函数：在后台开辟不可见的兼容内存 DC，遍历图形数据绘制像素，计算 BITMAPFILEHEADER 的 4 字节对齐，最后写入磁盘。
- **生命周期:** 由主线程响应保存动作时，通过 `CreateThread` 动态拉起。任务完成后，发送 `PostMessage` 通知主线程，随后线程函数返回，系统自动回收其内核对象栈。为了防止程序退出时强制关闭正在写入的线程导致文件损坏，主线程在 `WM_DESTROY` 阶段使用了 `WaitForSingleObject` 提供最多 5 秒的优雅退出宽限期。

### 4.3 线程安全的无锁化设计 (Lock-Free Snapshot)

为了避免引入耗性能的互斥锁（Mutex）导致主副线程互相等待，系统在触发保存时，利用 `std::vector` 的拷贝构造特性，为工作线程生成了一份**数据快照（Snapshot）**：

```cpp
std::vector<Shape> shapesCopy = g_shapes;
auto params = std::make_unique<SaveThreadParams>();
params->shapes = std::move(shapesCopy); // 移交数据所有权给工作线程

