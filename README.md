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

### 2.1 底部工具栏交互流转 (WM_COMMAND)

- **线段 (ID_BTN_LINE) / 矩形 (ID_BTN_RECT)：** 点击后修改全局状态变量 `g_currentType`，并同步更新窗口标题栏，为用户提供当前的绘图模式反馈。
  - **状态变量更新**
    - 若点击“线段”，将 `g_currentType` 设为 `ShapeType::Line`
    - 若点击“矩形”，将 `g_currentType` 设为 `ShapeType::Rectangle`
  - **界面反馈更新**
    - 调用 `SetWindowText` 更新窗口标题
    - 线段模式标题："简单画板 - 线段模式"
    - 矩形模式标题："简单画板 - 矩形模式"

- **撤销 (ID_BTN_UNDO)：** 触发命令回退逻辑。系统从 `g_undoStack` 弹出最后一次操作记录：
  - **前置检查**
    - 检查撤销栈 `g_undoStack` 是否为空
    - 若为空则直接返回，不执行任何操作
  - **操作类型判断**
    - **Add 类型（添加操作）**
      - 从操作记录中获取图形索引 `op.indices[0]`
      - 检查索引是否在有效范围内（0 到 `g_shapes.size() - 1`）
      - 调用 `g_shapes.erase(g_shapes.begin() + idx)` 删除对应图形
    - **Delete 类型（删除操作）**
      - **数据重组**
        - 遍历操作记录中的索引和图形，构建 `pair<int, Shape>` 数组
        - 每个 pair 包含原索引位置和被删除的图形对象
      - **排序处理**
        - 按索引**降序排序**（从大到小）
        - 排序目的：从后往前插入，避免先插入的图形改变后续索引位置
      - **图形恢复**
        - 遍历排序后的数组
        - 检查当前索引是否小于等于 `g_shapes.size()`
        - 若是：在指定索引位置插入 `g_shapes.insert(g_shapes.begin() + item.first, item.second)`
        - 若否：直接追加到末尾 `g_shapes.push_back(item.second)`
  - **状态更新**
    - 标记画板为未保存状态 `g_bSaved = false`
  - **界面刷新**
    - 调用 `GetCanvasRect` 获取画板区域矩形
    - 调用 `InvalidateRect(hWnd, &canvasRect, TRUE)` 仅重绘画板区域，避免按钮栏闪烁

- **清除 (ID_BTN_CLEAR)：** 将当前画布上所有的图形打包为一个 `OpType::Delete` 操作压入撤销栈，清空 `g_shapes` 容器，标记画板为未保存状态，并请求重绘。
  - **前置检查**
    - 检查 `g_shapes` 是否为空
    - 若为空则直接返回，不执行任何操作
  - **操作记录构建**
    - 遍历 `g_shapes` 中所有图形
    - 为每个图形记录其当前索引位置到 `indices` 数组
    - 为每个图形复制图形数据到 `shapes` 数组
    - 调用 `AddOperation(OpType::Delete, shapes, indices)` 将本次清除作为删除操作压入撤销栈
  - **数据清空**
    - 调用 `g_shapes.clear()` 清空所有图形数据
  - **状态更新**
    - 标记画板为未保存状态 `g_bSaved = false`
  - **界面刷新**
    - 获取画板区域矩形
    - 调用 `InvalidateRect` 仅重绘画板区域

- **保存 (ID_BTN_SAVE)：** 触发异步文件 I/O 流水线。通过 `GetSaveFileName` 调出系统对话框获取路径，锁定 UI 保存状态，禁用保存按钮以防重入冲突，最后拉起后台工作线程执行保存逻辑。
  - **前置检查**
    - **保存状态检查**
      - 检查全局标志 `g_bSaving` 是否为 true
      - 若正在保存中：弹出提示框 "正在保存中，请稍候..." 并直接返回
    - **画板尺寸检查**
      - 调用 `GetCanvasRect` 获取画板区域
      - 计算宽度和高度
      - 若宽度或高度小于等于0，直接返回（无内容可保存）
  - **文件路径获取**
    - **初始化对话框结构**
      - 填充 `OPENFILENAME` 结构体
      - 设置 `hwndOwner` 为主窗口句柄
      - 设置文件过滤器为 "Bitmap Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0"
      - 设置默认扩展名为 "bmp"
      - 设置标志 `OFN_OVERWRITEPROMPT`（文件存在时提示覆盖）
    - **调用保存对话框**
      - 调用 `GetSaveFileName(&ofn)`
      - 若用户取消：直接返回，不执行任何保存操作
      - 若用户确认：获取选择的文件路径 `szFile`
  - **数据快照准备**
    - **复制图形数据**
      - 创建 `std::vector<Shape> shapesCopy = g_shapes`
      - 深拷贝当前所有图形数据，生成独立副本
      - 目的：工作线程操作副本，主线程可继续修改原数据
    - **创建线程参数**
      - 使用 `std::make_unique<SaveThreadParams>` 创建参数对象
      - 设置参数成员：
        - `hWnd`：主窗口句柄（用于发送消息）
        - `fileName`：用户选择的文件路径
        - `shapes`：通过 `std::move(shapesCopy)` 转移图形数据所有权
        - `width`：画板宽度
        - `height`：画板高度
  - **UI状态锁定**
    - 设置全局标志 `g_bSaving = true`（标记正在保存）
    - 调用 `EnableWindow(GetDlgItem(hWnd, ID_BTN_SAVE), FALSE)` 禁用保存按钮
    - 目的：防止用户在保存过程中重复点击，导致多个保存线程冲突
  - **工作线程创建**
    - **调用 CreateThread**
      - 传递线程函数 `SaveThreadProc`
      - 传递参数 `params.get()`（原始指针）
      - 获取线程ID（仅用于调试）
      - 保存线程句柄到 `g_hSaveThread`
    - **线程创建成功处理**
      - 调用 `params.release()` 释放所有权
      - 目的：智能指针不再管理内存，所有权转移给工作线程
      - 工作线程结束后会自动通过 `unique_ptr` 释放内存
    - **线程创建失败处理**
      - 恢复保存状态 `g_bSaving = false`
      - 重新启用保存按钮
      - `params` 超出作用域自动释放内存（RAII优势）
      - 弹出错误提示 "无法创建保存线程！"
  - **工作线程执行逻辑 (SaveThreadProc)**
    - **参数接管**
      - 使用 `std::unique_ptr<SaveThreadParams> p(static_cast<SaveThreadParams*>(lpParam))`
      - 自动接管传入的参数内存，确保函数退出时释放
    - **同步查询**
      - 调用 `SendMessage(p->hWnd, WM_GET_SHAPE_COUNT, 0, 0)`
      - 目的：向主线程同步查询当前图形数量（仅作日志或调试用途）
    - **GDI资源创建**
      - **获取屏幕DC**
        - `HDC hdcScreen = GetDC(NULL)` 获取整个屏幕的设备上下文
      - **创建内存DC**
        - `HDC hdcMem = CreateCompatibleDC(hdcScreen)`
        - 创建与屏幕兼容的内存设备上下文
      - **创建兼容位图**
        - `HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, p->width, p->height)`
        - 创建与屏幕兼容的位图，大小与画板一致
      - **选入内存DC**
        - `SelectObject(hdcMem, hBitmap)`
        - 将位图选入内存DC，后续绘制操作都在这个位图上进行
    - **图形绘制**
      - 调用 `DrawCanvas(hdcMem, p->width, p->height, p->shapes)`
      - 在内存DC上绘制所有图形（使用数据副本）
      - 绘制白色背景、所有线段和矩形
    - **BMP文件保存**
      - 调用 `SaveBitmapToFile(hBitmap, p->fileName.c_str(), hdcMem)`
      - **内部实现细节：**
        - 获取位图信息 `GetObject(hBitmap, sizeof(BITMAP), &bmp)`
        - 填充 `BITMAPINFOHEADER` 结构
        - 计算图像数据大小（按4字节对齐）
        - 创建 `std::vector<BYTE>` 存储像素数据
        - 调用 `GetDIBits` 获取位图像素数据
        - 创建文件 `CreateFile`
        - 写入 `BITMAPFILEHEADER` 文件头
        - 写入 `BITMAPINFOHEADER` 信息头
        - 写入像素数据
        - 关闭文件句柄
    - **GDI资源清理**
      - 删除内存DC：`DeleteDC(hdcMem)`
      - 释放屏幕DC：`ReleaseDC(NULL, hdcScreen)`
      - 删除位图对象：`DeleteObject(hBitmap)`
    - **异步通知主线程**
      - 检查主窗口是否仍有效：`IsWindow(p->hWnd)`
      - 调用 `PostMessage(p->hWnd, WM_SAVE_COMPLETE, success ? 1 : 0, 0)`
      - 传递保存结果（成功或失败）
      - 使用 PostMessage 异步通知，不等待主线程处理
    - **线程退出**
      - 函数返回 0，线程自动结束
      - `unique_ptr` 析构自动释放 `SaveThreadParams` 内存
  - **保存完成处理 (WM_SAVE_COMPLETE)**
    - **状态恢复**
      - 从 `wParam` 获取保存结果 `success`
      - 调用 `EnableWindow(GetDlgItem(hWnd, ID_BTN_SAVE), TRUE)` 重新启用保存按钮
      - 设置 `g_bSaving = false` 清除保存状态
    - **线程句柄清理**
      - 检查 `g_hSaveThread` 是否为 NULL
      - 调用 `CloseHandle(g_hSaveThread)` 关闭线程句柄
      - 设置 `g_hSaveThread = NULL` 防止重复关闭
    - **用户反馈**
      - 若保存成功：设置 `g_bSaved = true` 标记已保存
      - 弹出提示框 "保存成功！"
      - 若保存失败：弹出提示框 "保存失败！"（带错误图标）

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

<pre>
std::vector<Shape> shapesCopy = g_shapes;
auto params = std::make_unique<SaveThreadParams>();
params->shapes = std::move(shapesCopy); // 移交数据所有权给工作线程
</pre>

通过这种内存隔离策略，工作线程在后台依据副本遍历渲染时，用户依然可以在主线程的前台继续流畅地使用鼠标绘制新图形，从物理层面彻底根除了读写竞争 (Race Condition)。

---

## 5. 架构评估与技术价值

### 5.1 架构特点
    
- 解耦性：UI 渲染与文件 I/O 彻底分离，两个模块可以独立演化和测试。
- 响应性：消息优先级机制确保鼠标操作优先响应，即使后台有任务，界面依然流畅。
- 可扩展性：新增图形类型只需扩展 ShapeType 枚举和 HitTestShape 函数，符合开闭原则。
- 可测试性：核心算法（命中测试的向量计算、撤销栈的索引处理）独立于 UI，可编写单元测试。

### 5.2 技术难点攻克

线程通信：
- 解决方案：SendMessage/PostMessage的差异化应用
- 技术价值：深入理解Windows消息本质，掌握同步与异步通信的适用场景

命中测试
- 解决方案：向量投影算法 + 欧几里得距离计算
- 技术价值：将高中数学（向量、投影、距离）应用于实际图形学问题

内存管理
- 解决方案：RAII 原则 + std::unique_ptr 智能指针
- 技术价值：现代 C++ 资源管理的最佳实践，避免内存泄漏

绘制闪烁
- 解决方案：双缓冲技术 + 区域无效重绘
- 技术价值：GDI性能优化的核心技巧，理解屏幕刷新机制

  
SimpleDraw 虽然功能简单，但其架构设计体现了 Windows 系统编程的核心思想：消息驱动、多线程分工、资源管理、无锁化数据共享。这些设计原则在企业级软件开发中具有普适性的参考价值。
