// SimpleDraw.cpp : 定义应用程序的入口点，实现简单画板功能
//

#include "framework.h"
#include "SimpleDraw.h"
#include <vector>
#include <string>
#include <commdlg.h>   // 文件对话框
#include <fstream>     // BMP文件保存
#include <algorithm>   // std::sort
#include <cmath>       // sqrt

#ifndef MAX_LOADSTRING
#define MAX_LOADSTRING 100
#endif

// 自定义消息
#define WM_GET_SHAPE_COUNT (WM_USER + 1)   // SendMessage 获取图形数量
#define WM_SAVE_COMPLETE    (WM_USER + 2)   // PostMessage 保存完成通知

// 图形类型
enum class ShapeType { Line, Rectangle };

// 图形数据
struct Shape {
    ShapeType type;
    POINT start;
    POINT end;
};

// 操作类型
enum class OpType { Add, Delete };

// 操作记录 - 修复C26495：使用类内初始化
struct Operation {
    OpType type = OpType::Add;    // 初始化type成员
    std::vector<Shape> shapes;
    std::vector<int> indices;
};

// 全局变量
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

std::vector<Shape> g_shapes;
bool g_isDrawing = false;
ShapeType g_currentType = ShapeType::Line;
POINT g_startPoint;
POINT g_currentPoint;
bool g_eraseMode = false;
std::vector<Operation> g_undoStack;
HBITMAP g_hBackgroundBitmap = NULL;

// 保存线程相关
bool g_bSaving = false;
HANDLE g_hSaveThread = NULL;

// 按钮ID
#define ID_BTN_LINE      1001
#define ID_BTN_RECT      1002
#define ID_BTN_ERASE     1003
#define ID_BTN_UNDO      1004
#define ID_BTN_CLEAR     1005
#define ID_BTN_SAVE      1006

// 常量
const int BUTTON_BAR_HEIGHT = 60;
const int BUTTON_WIDTH = 70;
const int BUTTON_HEIGHT = 35;
const int BUTTON_SPACING = 10;
const int HIT_TOLERANCE = 5;
const int MIN_DRAW_DISTANCE = 3;

// 函数声明
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void                SaveCanvasAsBMP(HWND hWnd);
bool                HitTestShape(const Shape& shape, POINT pt);
void                GetCanvasRect(HWND hWnd, RECT* canvasRect);
bool                IsPointInCanvas(HWND hWnd, POINT pt);
void                AddOperation(OpType type, const std::vector<Shape>& shapes, const std::vector<int>& indices);
void                RepositionButtons(HWND hWnd);
void                DrawCanvas(HDC hdc, int width, int height, const std::vector<Shape>& shapes, HBITMAP hBackground);
bool                SaveBitmapToFile(HBITMAP hBitmap, LPCWSTR filename, HDC hdc);
DWORD WINAPI        SaveThreadProc(LPVOID lpParam);

// 入口函数
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_SIMPLEDRAW, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
        return FALSE;

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_SIMPLEDRAW));
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SIMPLEDRAW));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    HWND hWnd = CreateWindowW(szWindowClass, L"简单画板 - 左键画图 右键擦除",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 900, 700, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd)
        return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

void GetCanvasRect(HWND hWnd, RECT* canvasRect)
{
    GetClientRect(hWnd, canvasRect);
    canvasRect->bottom -= BUTTON_BAR_HEIGHT;
}

bool IsPointInCanvas(HWND hWnd, POINT pt)
{
    RECT canvasRect;
    GetCanvasRect(hWnd, &canvasRect);
    return PtInRect(&canvasRect, pt);
}

void RepositionButtons(HWND hWnd)
{
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    int clientWidth = clientRect.right - clientRect.left;
    int totalButtons = 6;
    int totalWidth = totalButtons * BUTTON_WIDTH + (totalButtons - 1) * BUTTON_SPACING;
    int startX = (clientWidth - totalWidth) / 2;
    int buttonY = clientRect.bottom - BUTTON_BAR_HEIGHT + (BUTTON_BAR_HEIGHT - BUTTON_HEIGHT) / 2;

    const int buttonIds[] = { ID_BTN_LINE, ID_BTN_RECT, ID_BTN_ERASE,
                              ID_BTN_UNDO, ID_BTN_CLEAR, ID_BTN_SAVE };

    int x = startX;
    for (int id : buttonIds) {
        HWND hBtn = GetDlgItem(hWnd, id);
        if (hBtn) {
            SetWindowPos(hBtn, NULL, x, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT,
                SWP_NOZORDER | SWP_NOREDRAW);
        }
        x += BUTTON_WIDTH + BUTTON_SPACING;
    }
}

void AddOperation(OpType type, const std::vector<Shape>& shapes, const std::vector<int>& indices)
{
    Operation op;
    op.type = type;
    op.shapes = shapes;
    op.indices = indices;
    g_undoStack.push_back(op);
}

// 绘制画板内容（不含预览和边框）
void DrawCanvas(HDC hdc, int width, int height, const std::vector<Shape>& shapes, HBITMAP hBackground)
{
    if (hBackground) {
        HDC hdcBmp = CreateCompatibleDC(hdc);
        SelectObject(hdcBmp, hBackground);
        BITMAP bm;
        GetObject(hBackground, sizeof(BITMAP), &bm);
        StretchBlt(hdc, 0, 0, width, height, hdcBmp, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        DeleteDC(hdcBmp);
    }
    else {
        RECT rect = { 0, 0, width, height };
        HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(hdc, &rect, whiteBrush);
        DeleteObject(whiteBrush);
    }

    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    SelectObject(hdc, hPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));

    for (const auto& shape : shapes) {
        if (shape.type == ShapeType::Line) {
            MoveToEx(hdc, shape.start.x, shape.start.y, NULL);
            LineTo(hdc, shape.end.x, shape.end.y);
        }
        else {
            Rectangle(hdc, shape.start.x, shape.start.y, shape.end.x, shape.end.y);
        }
    }
    DeleteObject(hPen);
}

// 将位图保存为BMP文件
bool SaveBitmapToFile(HBITMAP hBitmap, LPCWSTR filename, HDC hdc)
{
    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);
    BITMAPINFOHEADER bi = { 0 };
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmp.bmWidth;
    bi.biHeight = bmp.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = bmp.bmBitsPixel;
    bi.biCompression = BI_RGB;

    DWORD dwBmpSize = ((bmp.bmWidth * bmp.bmBitsPixel + 31) / 32) * 4 * bmp.bmHeight;
    BYTE* lpBitmap = new BYTE[dwBmpSize];
    if (GetDIBits(hdc, hBitmap, 0, bmp.bmHeight, lpBitmap, (BITMAPINFO*)&bi, DIB_RGB_COLORS) == 0) {
        delete[] lpBitmap;
        return false;
    }

    HANDLE hFile = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        delete[] lpBitmap;
        return false;
    }

    BITMAPFILEHEADER bf;
    bf.bfType = 0x4D42;
    bf.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwBmpSize;
    bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    DWORD dwWritten;
    WriteFile(hFile, &bf, sizeof(BITMAPFILEHEADER), &dwWritten, NULL);
    WriteFile(hFile, &bi, sizeof(BITMAPINFOHEADER), &dwWritten, NULL);
    WriteFile(hFile, lpBitmap, dwBmpSize, &dwWritten, NULL);
    CloseHandle(hFile);
    delete[] lpBitmap;
    return true;
}

// 保存线程参数 - 修复C26495：使用类内初始化
struct SaveThreadParams {
    HWND hWnd = nullptr;
    std::wstring fileName;
    std::vector<Shape> shapes;
    HBITMAP hBackground = nullptr;
    int width = 0;
    int height = 0;
};

// 工作线程：执行保存操作
DWORD WINAPI SaveThreadProc(LPVOID lpParam)
{
    SaveThreadParams* p = (SaveThreadParams*)lpParam;

    // 演示 SendMessage：同步获取当前图形数量
    int count = (int)SendMessage(p->hWnd, WM_GET_SHAPE_COUNT, 0, 0);
    // 可在此添加调试输出：count 即为图形数量

    // 创建内存DC和位图
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, p->width, p->height);
    SelectObject(hdcMem, hBitmap);

    // 绘制画板内容
    DrawCanvas(hdcMem, p->width, p->height, p->shapes, p->hBackground);

    // 保存为BMP文件
    bool success = SaveBitmapToFile(hBitmap, p->fileName.c_str(), hdcMem);

    // 清理
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    DeleteObject(hBitmap);

    // 通知主线程保存完成（PostMessage 异步）
    if (IsWindow(p->hWnd)) {
        PostMessage(p->hWnd, WM_SAVE_COMPLETE, success ? 1 : 0, 0);
    }

    delete p;
    return 0;
}

// 保存画板内容（主线程调用，启动工作线程）
void SaveCanvasAsBMP(HWND hWnd)
{
    if (g_bSaving) {
        MessageBox(hWnd, L"正在保存中，请稍候...", L"提示", MB_OK);
        return;
    }

    RECT canvasRect;
    GetCanvasRect(hWnd, &canvasRect);
    int width = canvasRect.right - canvasRect.left;
    int height = canvasRect.bottom - canvasRect.top;
    if (width <= 0 || height <= 0) return;

    // 文件保存对话框
    OPENFILENAME ofn = { sizeof(ofn) };
    WCHAR szFile[260] = { 0 };
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Bitmap Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = 260;
    ofn.lpstrDefExt = L"bmp";
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (!GetSaveFileName(&ofn))
        return;

    // 复制当前图形（避免保存过程中被修改）
    std::vector<Shape> shapesCopy = g_shapes;

    SaveThreadParams* params = new SaveThreadParams;
    params->hWnd = hWnd;
    params->fileName = szFile;
    params->shapes = std::move(shapesCopy);
    params->hBackground = g_hBackgroundBitmap;
    params->width = width;
    params->height = height;

    g_bSaving = true;
    EnableWindow(GetDlgItem(hWnd, ID_BTN_SAVE), FALSE);

    DWORD dwThreadId;
    g_hSaveThread = CreateThread(NULL, 0, SaveThreadProc, params, 0, &dwThreadId);
    if (g_hSaveThread == NULL) {
        g_bSaving = false;
        EnableWindow(GetDlgItem(hWnd, ID_BTN_SAVE), TRUE);
        delete params;
        MessageBox(hWnd, L"无法创建保存线程！", L"错误", MB_OK | MB_ICONERROR);
    }
    // 线程句柄保留，等待 WM_SAVE_COMPLETE 或窗口销毁时关闭
}

// 窗口过程
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        const wchar_t* buttonLabels[] = { L"线段", L"矩形", L"擦除", L"撤销", L"清除", L"保存" };
        const int buttonIds[] = { ID_BTN_LINE, ID_BTN_RECT, ID_BTN_ERASE,
                                  ID_BTN_UNDO, ID_BTN_CLEAR, ID_BTN_SAVE };

        for (int i = 0; i < 6; i++) {
            // 修复C4312：使用 reinterpret_cast 风格转换
            CreateWindow(L"BUTTON", buttonLabels[i],
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, BUTTON_WIDTH, BUTTON_HEIGHT,
                hWnd, (HMENU)(INT_PTR)buttonIds[i], hInst, nullptr);
        }
        RepositionButtons(hWnd);
    }
    break;

    case WM_SIZE:
        RepositionButtons(hWnd);
        InvalidateRect(hWnd, NULL, TRUE);
        break;

    case WM_CLOSE:
        if (!g_shapes.empty() || g_hBackgroundBitmap != NULL)
        {
            int result = MessageBox(hWnd,
                L"当前画板内容未保存，是否保存为新文件？",
                L"简单画板",
                MB_YESNOCANCEL | MB_ICONQUESTION);

            if (result == IDYES)
            {
                SaveCanvasAsBMP(hWnd);
                DestroyWindow(hWnd);
            }
            else if (result == IDNO)
            {
                DestroyWindow(hWnd);
            }
            else
            {
                return 0;
            }
        }
        else
        {
            DestroyWindow(hWnd);
        }
        break;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case ID_BTN_LINE:
            g_currentType = ShapeType::Line;
            g_eraseMode = false;
            SetWindowText(hWnd, L"简单画板 - 线段模式");
            break;

        case ID_BTN_RECT:
            g_currentType = ShapeType::Rectangle;
            g_eraseMode = false;
            SetWindowText(hWnd, L"简单画板 - 矩形模式");
            break;

        case ID_BTN_ERASE:
            g_eraseMode = true;
            SetWindowText(hWnd, L"简单画板 - 擦除模式");
            break;

        case ID_BTN_UNDO:
            if (!g_undoStack.empty())
            {
                Operation op = g_undoStack.back();
                g_undoStack.pop_back();

                if (op.type == OpType::Add)
                {
                    int idx = op.indices[0];
                    if (idx >= 0 && idx < (int)g_shapes.size())
                        g_shapes.erase(g_shapes.begin() + idx);
                }
                else // Delete
                {
                    std::vector<std::pair<int, Shape>> toInsert;
                    for (size_t i = 0; i < op.indices.size(); ++i)
                        toInsert.push_back({ op.indices[i], op.shapes[i] });

                    std::sort(toInsert.begin(), toInsert.end(),
                        [](const auto& a, const auto& b) { return a.first > b.first; });

                    for (const auto& item : toInsert)
                    {
                        if (item.first <= (int)g_shapes.size())
                            g_shapes.insert(g_shapes.begin() + item.first, item.second);
                        else
                            g_shapes.push_back(item.second);
                    }
                }
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;

        case ID_BTN_CLEAR:
            if (!g_shapes.empty())
            {
                std::vector<int> indices;
                std::vector<Shape> shapes;
                for (size_t i = 0; i < g_shapes.size(); ++i)
                {
                    indices.push_back((int)i);
                    shapes.push_back(g_shapes[i]);
                }
                AddOperation(OpType::Delete, shapes, indices);
                g_shapes.clear();
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;

        case ID_BTN_SAVE:
            SaveCanvasAsBMP(hWnd);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_LBUTTONDOWN:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (!IsPointInCanvas(hWnd, pt)) break;

        if (g_eraseMode)
        {
            std::vector<std::pair<int, Shape>> deleted;
            for (int i = (int)g_shapes.size() - 1; i >= 0; --i)
            {
                if (HitTestShape(g_shapes[i], pt))
                {
                    deleted.push_back({ i, g_shapes[i] });
                    g_shapes.erase(g_shapes.begin() + i);
                }
            }
            if (!deleted.empty())
            {
                std::sort(deleted.begin(), deleted.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });
                std::vector<int> indices;
                std::vector<Shape> shapes;
                for (const auto& item : deleted)
                {
                    indices.push_back(item.first);
                    shapes.push_back(item.second);
                }
                AddOperation(OpType::Delete, shapes, indices);
                InvalidateRect(hWnd, NULL, TRUE);
            }
        }
        else
        {
            g_isDrawing = true;
            g_startPoint = pt;
            g_currentPoint = pt;
            SetCapture(hWnd);
        }
    }
    break;

    case WM_RBUTTONDOWN:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (!IsPointInCanvas(hWnd, pt)) break;

        std::vector<std::pair<int, Shape>> deleted;
        for (int i = (int)g_shapes.size() - 1; i >= 0; --i)
        {
            if (HitTestShape(g_shapes[i], pt))
            {
                deleted.push_back({ i, g_shapes[i] });
                g_shapes.erase(g_shapes.begin() + i);
            }
        }
        if (!deleted.empty())
        {
            std::sort(deleted.begin(), deleted.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<int> indices;
            std::vector<Shape> shapes;
            for (const auto& item : deleted)
            {
                indices.push_back(item.first);
                shapes.push_back(item.second);
            }
            AddOperation(OpType::Delete, shapes, indices);
            InvalidateRect(hWnd, NULL, TRUE);
        }
    }
    break;

    case WM_MOUSEMOVE:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (g_isDrawing && (wParam & MK_LBUTTON))
        {
            RECT canvasRect;
            GetCanvasRect(hWnd, &canvasRect);
            g_currentPoint.x = max(canvasRect.left, min(canvasRect.right, pt.x));
            g_currentPoint.y = max(canvasRect.top, min(canvasRect.bottom, pt.y));
            InvalidateRect(hWnd, &canvasRect, TRUE);
        }
    }
    break;

    case WM_LBUTTONUP:
    {
        POINT endPoint = { LOWORD(lParam), HIWORD(lParam) };
        if (g_isDrawing)
        {
            RECT canvasRect;
            GetCanvasRect(hWnd, &canvasRect);
            endPoint.x = max(canvasRect.left, min(canvasRect.right, endPoint.x));
            endPoint.y = max(canvasRect.top, min(canvasRect.bottom, endPoint.y));

            if (abs(endPoint.x - g_startPoint.x) > MIN_DRAW_DISTANCE ||
                abs(endPoint.y - g_startPoint.y) > MIN_DRAW_DISTANCE)
            {
                Shape s;
                s.type = g_currentType;
                s.start = g_startPoint;
                s.end = endPoint;
                g_shapes.push_back(s);
                AddOperation(OpType::Add, { s }, { (int)g_shapes.size() - 1 });
            }

            g_isDrawing = false;
            ReleaseCapture();
            InvalidateRect(hWnd, &canvasRect, TRUE);
        }
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT canvasRect;
        GetCanvasRect(hWnd, &canvasRect);
        int width = canvasRect.right;
        int height = canvasRect.bottom;

        // 双缓冲
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height + BUTTON_BAR_HEIGHT);
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

        // 绘制画板区域
        DrawCanvas(hdcMem, width, height, g_shapes, g_hBackgroundBitmap);

        // 绘制画板边框
        HPEN hBorderPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
        SelectObject(hdcMem, hBorderPen);
        SelectObject(hdcMem, GetStockObject(NULL_BRUSH));
        Rectangle(hdcMem, 0, 0, width, height);

        // 绘制预览图形
        if (g_isDrawing)
        {
            HPEN hTempPen = CreatePen(PS_DOT, 1, RGB(255, 0, 0));
            SelectObject(hdcMem, hTempPen);
            if (g_currentType == ShapeType::Line)
            {
                MoveToEx(hdcMem, g_startPoint.x, g_startPoint.y, NULL);
                LineTo(hdcMem, g_currentPoint.x, g_currentPoint.y);
            }
            else
            {
                Rectangle(hdcMem, g_startPoint.x, g_startPoint.y,
                    g_currentPoint.x, g_currentPoint.y);
            }
            DeleteObject(hTempPen);
        }
        DeleteObject(hBorderPen);

        // 绘制按钮栏背景
        RECT btnBarRect = { 0, height, width, height + BUTTON_BAR_HEIGHT };
        HBRUSH barBrush = CreateSolidBrush(RGB(240, 240, 240));
        FillRect(hdcMem, &btnBarRect, barBrush);
        DeleteObject(barBrush);

        BitBlt(hdc, 0, 0, width, height + BUTTON_BAR_HEIGHT, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);
        EndPaint(hWnd, &ps);
    }
    break;

    case WM_ERASEBKGND:
        return 1;

        // 自定义消息：获取图形数量（SendMessage 演示）
    case WM_GET_SHAPE_COUNT:
        return (LRESULT)g_shapes.size();

        // 自定义消息：保存完成（PostMessage 演示）
    case WM_SAVE_COMPLETE:
    {
        BOOL success = (BOOL)wParam;
        EnableWindow(GetDlgItem(hWnd, ID_BTN_SAVE), TRUE);
        g_bSaving = false;
        if (g_hSaveThread != NULL) {
            CloseHandle(g_hSaveThread);
            g_hSaveThread = NULL;
        }
        MessageBox(hWnd, success ? L"保存成功！" : L"保存失败！",
            L"提示", success ? MB_OK : MB_OK | MB_ICONERROR);
    }
    break;

    case WM_DESTROY:
        // 等待保存线程结束（最多5秒）
        if (g_hSaveThread != NULL) {
            WaitForSingleObject(g_hSaveThread, 5000);
            CloseHandle(g_hSaveThread);
            g_hSaveThread = NULL;
        }
        if (g_hBackgroundBitmap)
            DeleteObject(g_hBackgroundBitmap);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

bool HitTestShape(const Shape& shape, POINT pt)
{
    if (shape.type == ShapeType::Line)
    {
        int left = min(shape.start.x, shape.end.x) - HIT_TOLERANCE;
        int right = max(shape.start.x, shape.end.x) + HIT_TOLERANCE;
        int top = min(shape.start.y, shape.end.y) - HIT_TOLERANCE;
        int bottom = max(shape.start.y, shape.end.y) + HIT_TOLERANCE;

        if (pt.x < left || pt.x > right || pt.y < top || pt.y > bottom)
            return false;

        float dx = (float)(shape.end.x - shape.start.x);
        float dy = (float)(shape.end.y - shape.start.y);
        if (dx == 0 && dy == 0) return false;

        float t = ((pt.x - shape.start.x) * dx + (pt.y - shape.start.y) * dy) /
            (dx * dx + dy * dy);
        t = max(0.0f, min(1.0f, t));

        float projx = shape.start.x + t * dx;
        float projy = shape.start.y + t * dy;
        float dist = sqrt((pt.x - projx) * (pt.x - projx) +
            (pt.y - projy) * (pt.y - projy));
        return dist <= HIT_TOLERANCE;
    }
    else
    {
        RECT rect = {
            min(shape.start.x, shape.end.x) - HIT_TOLERANCE,
            min(shape.start.y, shape.end.y) - HIT_TOLERANCE,
            max(shape.start.x, shape.end.x) + HIT_TOLERANCE,
            max(shape.start.y, shape.end.y) + HIT_TOLERANCE
        };
        return PtInRect(&rect, pt);
    }
}
