/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/BitManip.h"
#include "utils/WinUtil.h"
#include "utils/Dpi.h"
#include "utils/WinDynCalls.h"

#include "wingui/UIModels.h"

#include "wingui/Layout.h"
#include "wingui/WinGui.h"

#include "utils/Log.h"

#include "webview2.h"

Kind kindWnd = "wnd";

static LONG gSubclassId = 0;

UINT_PTR NextSubclassId() {
    LONG res = InterlockedIncrement(&gSubclassId);
    return (UINT_PTR)res;
}

// TODO:
// - if layout is set, do layout on WM_SIZE using LayoutToSize

// window_map.h / window_map.cpp
struct WindowToHwnd {
    Wnd* window = nullptr;
    HWND hwnd = nullptr;
};

Vec<WindowToHwnd> gWindowToHwndMap;

static Wnd* WindowMapGetWindow(HWND hwnd) {
    for (auto& el : gWindowToHwndMap) {
        if (el.hwnd == hwnd) {
            return el.window;
        }
    }
    return nullptr;
}

static void WindowMapAdd(HWND hwnd, Wnd* w) {
    if (!hwnd || (WindowMapGetWindow(hwnd) != nullptr)) {
        return;
    }
    WindowToHwnd el = {w, hwnd};
    gWindowToHwndMap.Append(el);
}

/*
static bool WindowMapRemove(HWND hwnd) {
    int n = gWindowToHwndMap.isize();
    for (int i = 0; i < n; i++) {
        auto&& el = gWindowToHwndMap[i];
        if (el.hwnd == hwnd) {
            gWindowToHwndMap.RemoveAtFast(i);
            return true;
        }
    }
    return false;
}
*/

static bool WindowMapRemove(Wnd* w) {
    int n = gWindowToHwndMap.isize();
    for (int i = 0; i < n; i++) {
        auto&& el = gWindowToHwndMap[i];
        if (el.window == w) {
            gWindowToHwndMap.RemoveAtFast(i);
            return true;
        }
    }
    return false;
}

//- Taskbar.cpp

const DWORD WM_TASKBARCALLBACK = WM_APP + 0x15;
const DWORD WM_TASKBARCREATED = ::RegisterWindowMessage(L"TaskbarCreated");
const DWORD WM_TASKBARBUTTONCREATED = ::RegisterWindowMessage(L"TaskbarButtonCreated");

//- Window.h / Window.cpp

const WCHAR* kDefaultClassName = L"SumatraWgDefaultWinClass";

static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Wnd* window = WindowMapGetWindow(hwnd);

    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)(lparam);
        CrashIf(window);
        window = (Wnd*)(cs->lpCreateParams);
        window->hwnd = hwnd;
        WindowMapAdd(hwnd, window);
    }

    if (window) {
        return window->WndProc(hwnd, msg, wparam, lparam);
    } else {
        return ::DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

static LRESULT CALLBACK StaticWindowProcSubclassed(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId,
                                                   DWORD_PTR data) {
    return StaticWindowProc(hwnd, msg, wp, lp);
}

Wnd::Wnd() {
    // instance = GetModuleHandleW(nullptr);
    kind = kindWnd;
}

Wnd::~Wnd() {
    Destroy();
    delete layout;
    DeleteBrush(backgroundColorBrush);
}

Kind Wnd::GetKind() {
    return kind;
}

void Wnd::SetText(const WCHAR* s) {
    CrashIf(!hwnd);
    HwndSetText(hwnd, s);
    HwndInvalidate(hwnd); // TODO: move inside HwndSetText()?
}

void Wnd::SetText(const char* s) {
    if (!s) {
        s = "";
    }
    HwndSetText(hwnd, s);
    HwndInvalidate(hwnd); // TODO: move inside HwndSetText()?
}

TempStr Wnd::GetTextTemp() {
    char* s = HwndGetTextTemp(hwnd);
    return s;
}

void Wnd::SetVisibility(Visibility newVisibility) {
    CrashIf(!hwnd);
    visibility = newVisibility;
    bool isVisible = IsVisible();
    // TODO: a different way to determine if is top level vs. child window?
    if (GetParent(hwnd) == nullptr) {
        ::ShowWindow(hwnd, isVisible ? SW_SHOW : SW_HIDE);
    } else {
        BOOL bIsVisible = toBOOL(isVisible);
        SetWindowStyle(hwnd, WS_VISIBLE, bIsVisible);
    }
}

Visibility Wnd::GetVisibility() {
    return visibility;
#if 0
    if (GetParent(hwnd) == nullptr) {
        // TODO: what to do for top-level window?
        CrashMe();
        return true;
    }
    bool isVisible = IsWindowStyleSet(hwnd, WS_VISIBLE);
    return isVisible;
#endif
}

void Wnd::SetIsVisible(bool isVisible) {
    SetVisibility(isVisible ? Visibility::Visible : Visibility::Collapse);
}

bool Wnd::IsVisible() const {
    return visibility == Visibility::Visible;
}

void Wnd::Destroy() {
    HwndDestroyWindowSafe(&hwnd);
    UnSubclass();
    Cleanup();
}

LRESULT Wnd::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    return WndProcDefault(hwnd, msg, wparam, lparam);
}

// This function is called when a window is attached to Wnd.
// Override it to automatically perform tasks when the window is attached.
// Note:  Window controls are attached.
void Wnd::OnAttach() {
}

// Override this to handle WM_COMMAND messages
bool Wnd::OnCommand(WPARAM wparam, LPARAM lparam) {
    //  UINT id = LOWORD(wparam);
    //  switch (id)
    //  {
    //  case IDM_FILE_NEW:
    //      OnFileNew();
    //      return true;   // return TRUE for handled commands
    //  }

    // return false for unhandled commands

    return false;
}

// Called during window creation. Override this functions to perform tasks
// such as creating child windows.
int Wnd::OnCreate(CREATESTRUCT*) {
    // This function is called when a WM_CREATE message is received
    // Override it to automatically perform tasks during window creation.
    // Return 0 to continue creating the window.

    // Note: Window controls don't call OnCreate. They are sublcassed (attached)
    //  after their window is created.

    /*
    LOGFONT logfont;
    ::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(logfont), &logfont);
    font = ::CreateFontIndirect(&logfont);
    ::SendMessage(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    */

    return 0;
}

// This function is called when a window is destroyed.
// Override it to do additional tasks, such as ending the application
//  with PostQuitMessage.
void Wnd::OnDestroy() {
}

// Called when the background of the window's client area needs to be erased.
// Override this function in your derived class to perform drawing tasks.
// Return Value: Return FALSE to also permit default erasure of the background
//               Return TRUE to prevent default erasure of the background
bool Wnd::OnEraseBkgnd(HDC) {
    return false;
}

// Called in response to WM_CLOSE, before the window is destroyed.
// Override this function to suppress destroying the window.
// WM_CLOSE is sent by SendMessage(WM_CLOSE, 0, 0) or by clicking X
//  in the top right corner.
// Child windows don't receive WM_CLOSE unless they are closed using
//  the Close function.
void Wnd::OnClose() {
    Destroy();
}

void Wnd::OnContextMenu(Point ptScreen) {
    if (!onContextMenu) {
        return;
    }

    // https://docs.microsoft.com/en-us/windows/win32/menurc/wm-contextmenu
    ContextMenuEvent ev;
    ev.w = this;
    ev.mouseScreen = ptScreen;

    POINT ptW{ptScreen.x, ptScreen.y};
    if (ptScreen.x != -1) {
        MapWindowPoints(HWND_DESKTOP, hwnd, &ptW, 1);
    }
    ev.mouseWindow.x = ptW.x;
    ev.mouseWindow.y = ptW.y;
    onContextMenu(&ev);
}

void Wnd::OnDropFiles(HDROP drop_info) {
}

void Wnd::OnGetMinMaxInfo(MINMAXINFO* mmi) {
}

LRESULT Wnd::OnMouseEvent(UINT msg, WPARAM wparam, LPARAM lparam) {
    return -1;
}

void Wnd::OnMove(LPPOINTS pts) {
}

// Processes notification (WM_NOTIFY) messages from a child window.
LRESULT Wnd::OnNotify(int controlId, NMHDR* nmh) {
    // You can use either OnNotifyReflect or OnNotify to handle notifications
    // Override OnNotifyReflect to handle notifications in the CWnd class that
    //   generated the notification.   OR
    // Override OnNotify to handle notifications in the PARENT of the CWnd class
    //   that generated the notification.

    // Your overriding function should look like this ...

    // LPNMHDR pHeader = reinterpret_cast<LPNMHDR>(lparam);
    // switch (pHeader->code)
    // {
    //      Handle your notifications from the CHILD window here
    //      Return the value recommended by the Windows API documentation.
    //      For many notifications, the return value doesn't matter, but for some it does.
    // }

    // return 0 for unhandled notifications
    // The framework will call SetWindowLongPtr(DWLP_MSGRESULT, result) for dialogs.
    return 0;
}

// Processes the notification (WM_NOTIFY) messages in the child window that originated them.
LRESULT Wnd::OnNotifyReflect(WPARAM, LPARAM) {
    // Override OnNotifyReflect to handle notifications in the CWnd class that
    //   generated the notification.

    // Your overriding function should look like this ...

    // LPNMHDR pHeader = reinterpret_cast<LPNMHDR>(lparam);
    // switch (pHeader->code)
    // {
    //      Handle your notifications from this window here
    //      Return the value recommended by the Windows API documentation.
    // }

    // Return 0 for unhandled notifications.
    // The framework will call SetWindowLongPtr(DWLP_MSGRESULT, result) for dialogs.
    return 0;
}

void Wnd::OnPaint(HDC hdc, PAINTSTRUCT* ps) {
    auto bgBrush = backgroundColorBrush;
    if (bgBrush != nullptr) {
        FillRect(hdc, &ps->rcPaint, bgBrush);
    }
}

void Wnd::OnSize(UINT msg, UINT type, SIZE size) {
}

void Wnd::OnTaskbarCallback(UINT msg, LPARAM lparam) {
}

void Wnd::OnTimer(UINT_PTR event_id) {
}

void Wnd::OnWindowPosChanging(WINDOWPOS* window_pos) {
}

// This function processes those special messages sent by some older controls,
// and reflects them back to the originating CWnd object.
// Override this function in your derived class to handle these special messages:
// WM_COMMAND, WM_CTLCOLORBTN, WM_CTLCOLOREDIT, WM_CTLCOLORDLG, WM_CTLCOLORLISTBOX,
// WM_CTLCOLORSCROLLBAR, WM_CTLCOLORSTATIC, WM_CHARTOITEM,  WM_VKEYTOITEM,
// WM_HSCROLL, WM_VSCROLL, WM_DRAWITEM, WM_MEASUREITEM, WM_DELETEITEM,
// WM_COMPAREITEM, WM_PARENTNOTIFY.
LRESULT Wnd::OnMessageReflect(UINT, WPARAM, LPARAM) {
    // This function processes those special messages (see above) sent
    // by some older controls, and reflects them back to the originating CWnd object.
    // Override this function in your derived class to handle these special messages.

    // Your overriding function should look like this ...

    // switch (msg)
    // {
    //      Handle your reflected messages here
    // }

    // return 0 for unhandled messages
    return 0;
}

Size Wnd::GetIdealSize() {
    return {};
}

Size Wnd::Layout(const Constraints bc) {
    dbglayoutf("WindowBase::Layout() %s ", GetKind());
    LogConstraints(bc, "\n");

    auto hinset = insets.left + insets.right;
    auto vinset = insets.top + insets.bottom;
    auto innerConstraints = bc.Inset(hinset, vinset);

    int dx = MinIntrinsicWidth(0);
    int dy = MinIntrinsicHeight(0);
    childSize = innerConstraints.Constrain(Size{dx, dy});
    auto res = Size{
        childSize.dx + hinset,
        childSize.dy + vinset,
    };
    return res;
}

int Wnd::MinIntrinsicHeight(int) {
#if 0
    auto vinset = insets.top + insets.bottom;
    Size s = GetIdealSize();
    return s.dy + vinset;
#else
    Size s = GetIdealSize();
    return s.dy;
#endif
}

int Wnd::MinIntrinsicWidth(int) {
#if 0
    auto hinset = insets.left + insets.right;
    Size s = GetIdealSize();
    return s.dx + hinset;
#else
    Size s = GetIdealSize();
    return s.dx;
#endif
}

void Wnd::Close() {
    CrashIf(!::IsWindow(hwnd));
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

void Wnd::SetPos(RECT* r) {
    ::MoveWindow(hwnd, r);
}

void Wnd::SetBounds(Rect bounds) {
    dbglayoutf("WindowBaseLayout:SetBounds() %s %d,%d - %d, %d\n", GetKind(), bounds.x, bounds.y, bounds.dx, bounds.dy);

    lastBounds = bounds;

    bounds.x += insets.left;
    bounds.y += insets.top;
    bounds.dx -= (insets.right + insets.left);
    bounds.dy -= (insets.bottom + insets.top);

    auto r = RectToRECT(bounds);
    ::MoveWindow(hwnd, &r);
    // TODO: optimize if doesn't change position
    ::InvalidateRect(hwnd, nullptr, TRUE);
}

// A function used internally to call OnMessageReflect. Don't call or override this function.
LRESULT Wnd::MessageReflect(UINT msg, WPARAM wparam, LPARAM lparam) {
    HWND wnd = 0;
    switch (msg) {
        case WM_COMMAND:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSCROLLBAR:
        case WM_CTLCOLORSTATIC:
        case WM_CHARTOITEM:
        case WM_VKEYTOITEM:
        case WM_HSCROLL:
        case WM_VSCROLL:
            wnd = reinterpret_cast<HWND>(lparam);
            break;

        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_DELETEITEM:
        case WM_COMPAREITEM:
            wnd = GetDlgItem(hwnd, static_cast<int>(wparam));
            break;

        case WM_PARENTNOTIFY:
            switch (LOWORD(wparam)) {
                case WM_CREATE:
                case WM_DESTROY:
                    wnd = reinterpret_cast<HWND>(lparam);
                    break;
            }
    }
    if (!wnd) {
        return 0;
    }

    Wnd* pWnd = WindowMapGetWindow(wnd);
    if (pWnd != nullptr) {
        auto res = pWnd->OnMessageReflect(msg, wparam, lparam);
        return res;
    }

    return 0;
}

// for interop with windows not wrapped in Wnd, run this at the beginning of message loop
LRESULT TryReflectMessages(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // hwnd is a parent of control sending WM_NOTIFY message
    switch (msg) {
        case WM_COMMAND: {
            // Reflect this message if it's from a control.
            Wnd* pWnd = WindowMapGetWindow(reinterpret_cast<HWND>(lparam));
            bool didHandle = false;
            if (pWnd != nullptr) {
                didHandle = pWnd->OnCommand(wparam, lparam);
            }
            if (didHandle) {
                return 1;
            }
        } break; // Note: Some MDI commands require default processing.
        case WM_NOTIFY: {
            // Do notification reflection if message came from a child window.
            // Restricting OnNotifyReflect to child windows avoids double handling.
            NMHDR* hdr = reinterpret_cast<LPNMHDR>(lparam);
            HWND from = hdr->hwndFrom;
            Wnd* wndFrom = WindowMapGetWindow(from);
            if (!wndFrom) {
                return 0;
            }
            if (hwnd == GetParent(wndFrom->hwnd)) {
                return wndFrom->OnNotifyReflect(wparam, lparam);
            }
        }
        // A set of messages to be reflected back to the control that generated them.
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSCROLLBAR:
        case WM_CTLCOLORSTATIC:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_DELETEITEM:
        case WM_COMPAREITEM:
        case WM_CHARTOITEM:
        case WM_VKEYTOITEM:
        case WM_HSCROLL:
        case WM_VSCROLL:
        case WM_PARENTNOTIFY: {
            Wnd* pWnd = WindowMapGetWindow(reinterpret_cast<HWND>(lparam));
            LRESULT result = 0;
            if (pWnd != nullptr) {
                result = pWnd->MessageReflect(msg, wparam, lparam);
            }
            if (result != 0) {
                return result; // Message processed so return.
            }
        } break; // Do default processing when message not already processed.
    }
    return 0;
}

LRESULT Wnd::WndProcDefault(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    LRESULT result = 0;

    switch (msg) {
        case WM_CLOSE: {
            OnClose();
            return 0;
        }

        // windows don't support WM_GETFONT / WM_SETFONT
        // only controls do. not sure if we won't interfere
        // with control handling
        // TODO: maybe when font is nullptr, ask the original proc
        case WM_GETFONT: {
            return (LRESULT)font;
        }

        case WM_SETFONT: {
            font = (HFONT)wparam;
            return 0;
        }

        case WM_COMMAND: {
            // Reflect this message if it's from a control.
            Wnd* pWnd = WindowMapGetWindow(reinterpret_cast<HWND>(lparam));
            bool didHandle = false;
            if (pWnd != nullptr) {
                didHandle = pWnd->OnCommand(wparam, lparam);
            }

            // Handle user commands.
            if (!didHandle) {
                didHandle = OnCommand(wparam, lparam);
            }

            if (didHandle) {
                return 0;
            }
        } break; // Note: Some MDI commands require default processing.

        case WM_CREATE: {
            OnCreate((CREATESTRUCT*)lparam);
            break;
        }

        case WM_DESTROY: {
            OnDestroy();
            break; // Note: Some controls require default processing.
        }

        case WM_NOTIFY: {
            // Do notification reflection if message came from a child window.
            // Restricting OnNotifyReflect to child windows avoids double handling.
            NMHDR* hdr = reinterpret_cast<NMHDR*>(lparam);
            HWND from = hdr->hwndFrom;
            Wnd* wndFrom = WindowMapGetWindow(from);

            if (wndFrom != nullptr) {
                if (::GetParent(from) == this->hwnd) {
                    result = wndFrom->OnNotifyReflect(wparam, lparam);
                }
            }

            // Handle user notifications
            if (result == 0) {
                result = OnNotify((int)wparam, (NMHDR*)lparam);
            }
            if (result != 0) {
                return result;
            }
            break;
        }

        case WM_PAINT: {
            if (subclassId) {
                // Allow window controls to do their default drawing.
                return FinalWindowProc(msg, wparam, lparam);
            }

            if (::GetUpdateRect(hwnd, nullptr, FALSE)) {
                PAINTSTRUCT ps;
                HDC hdc = ::BeginPaint(hwnd, &ps);
                OnPaint(hdc, &ps);
                ::EndPaint(hwnd, &ps);
            } else {
                // TODO: for now those are the same because LabelWithCloseWnd::OnPaint
                // assumes ps is provided (and maybe others)
                PAINTSTRUCT ps;
                HDC hdc = ::BeginPaint(hwnd, &ps);
                OnPaint(hdc, &ps);
                ::EndPaint(hwnd, &ps);
#if 0
                HDC hdc = ::GetDC(hwnd);
                OnPaint(hdc, nullptr);
                ::ReleaseDC(hwnd, hdc);
#endif
            }
            // No more drawing required
            return 0;
        }

        case WM_ERASEBKGND: {
            HDC dc = (HDC)(wparam);
            BOOL preventErasure;

            preventErasure = OnEraseBkgnd(dc);
            if (preventErasure)
                return TRUE;
        } break;

        // A set of messages to be reflected back to the control that generated them.
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSCROLLBAR:
        case WM_CTLCOLORSTATIC:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_DELETEITEM:
        case WM_COMPAREITEM:
        case WM_CHARTOITEM:
        case WM_VKEYTOITEM:
        case WM_HSCROLL:
        case WM_VSCROLL:
        case WM_PARENTNOTIFY: {
            result = MessageReflect(msg, wparam, lparam);
            if (result != 0)
                return result; // Message processed so return.
        } break;               // Do default processing when message not already processed.

        case WM_DROPFILES: {
            OnDropFiles(reinterpret_cast<HDROP>(wparam));
            break;
        }

        case WM_ENTERSIZEMOVE:
        case WM_EXITSIZEMOVE: {
            SIZE size = {0};
            OnSize(msg, 0, size);
            break;
        }
        case WM_GETMINMAXINFO: {
            OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lparam));
            break;
        }
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MOUSEACTIVATE:
        case WM_MOUSEHOVER:
        case WM_MOUSEHWHEEL:
        case WM_MOUSELEAVE:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL: {
            LRESULT lResult = OnMouseEvent(msg, wparam, lparam);
            if (lResult != -1)
                return lResult;
            break;
        }
        case WM_MOVE: {
            POINTS pts = MAKEPOINTS(lparam);
            OnMove(&pts);
            break;
        }

        case WM_CONTEXTMENU: {
            Point ptScreen = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            // Note: HWND in wparam might be a child window
            OnContextMenu(ptScreen);
            break;
        }

        case WM_SIZE: {
            SIZE size = {LOWORD(lparam), HIWORD(lparam)};
            OnSize(msg, static_cast<UINT>(wparam), size);
            break;
        }
        case WM_TIMER: {
            OnTimer(static_cast<UINT>(wparam));
            break;
        }
        case WM_WINDOWPOSCHANGING: {
            OnWindowPosChanging(reinterpret_cast<LPWINDOWPOS>(lparam));
            break;
        }

        default: {
            if (msg == WM_TASKBARCREATED || msg == WM_TASKBARBUTTONCREATED || msg == WM_TASKBARCALLBACK) {
                OnTaskbarCallback(msg, lparam);
                return 0;
            }
            break;
        }
    }

    // Now hand all messages to the default procedure.
    return FinalWindowProc(msg, wparam, lparam);
}

LRESULT Wnd::FinalWindowProc(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (subclassId) {
        return ::DefSubclassProc(hwnd, msg, wparam, lparam);
    } else {
        // TODO: also DefSubclassProc?
        return ::DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

bool Wnd::PreTranslateMessage(MSG& msg) {
    return false;
}

void Wnd::Attach(HWND hwnd) {
    CrashIf(!IsWindow(hwnd));
    CrashIf(WindowMapGetWindow(hwnd));

    this->hwnd = hwnd;
    Subclass();
    OnAttach();
}

// Attaches a CWnd object to a dialog item.
void Wnd::AttachDlgItem(UINT id, HWND parent) {
    CrashIf(!::IsWindow(parent));
    HWND wnd = ::GetDlgItem(parent, id);
    Attach(wnd);
}

HWND Wnd::Detach() {
    UnSubclass();

    HWND wnd = hwnd;
    WindowMapRemove(this);
    hwnd = nullptr;
    return wnd;
}

void Wnd::Cleanup() {
    WindowMapRemove(this);
    hwnd = nullptr;
    subclassId = 0;
}

static void WndRegisterClass(const WCHAR* className) {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    HINSTANCE inst = GetInstance();
    BOOL ok = ::GetClassInfoExW(inst, className, &wc);
    if (ok) {
        return;
    }
    wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.hInstance = inst;
    wc.lpszClassName = className;
    wc.lpfnWndProc = StaticWindowProc;
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH));
    ATOM atom = ::RegisterClassExW(&wc);
    CrashIf(!atom);
}

HWND Wnd::CreateControl(const CreateControlArgs& args) {
    CrashIf(!args.className);
    // TODO: validate that className is one of the known controls?

    font = args.font;
    if (!font) {
        // TODO: need this?
        font = GetDefaultGuiFont();
    }

    DWORD style = args.style;
    if (args.parent) {
        style |= WS_CHILD;
    }
    if (args.visible) {
        style |= WS_VISIBLE;
    } else {
        style &= ~WS_VISIBLE;
    }
    DWORD exStyle = args.exStyle;
    const WCHAR* className = args.className;
    int x = args.pos.x;
    int y = args.pos.y;
    int dx = args.pos.dx;
    int dy = args.pos.dy;
    HWND parent = args.parent;
    HMENU id = args.ctrlId;
    HINSTANCE inst = GetInstance();
    void* createParams = this;
    hwnd = ::CreateWindowExW(exStyle, className, L"", style, x, y, dx, dy, parent, id, inst, createParams);
    HwndSetFont(hwnd, font);
    CrashIf(!hwnd);

    // TODO: validate that
    Subclass();
    OnAttach();

    // prevWindowProc(hwnd, WM_SETFONT, (WPARAM)f, 0);
    // HwndSetFont(hwnd, f);
    if (args.text) {
        SetText(args.text);
    }
    return hwnd;
}

HWND Wnd::CreateCustom(const CreateCustomArgs& args) {
    font = args.font;

    const WCHAR* className = args.className;
    // TODO: validate className is not win32 control class
    if (className == nullptr) {
        className = kDefaultClassName;
    }
    WndRegisterClass(className);
    HWND parent = args.parent;

    DWORD style = args.style;
    if (style == 0) {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    }
    if (parent) {
        style |= WS_CHILD;
    } else {
        style &= ~WS_CHILD;
        style |= WS_CLIPCHILDREN;
    }
    if (args.visible) {
        style |= WS_VISIBLE;
    } else {
        style &= ~WS_VISIBLE;
    }

    int x = args.pos.x;
    int y = args.pos.y;
    int dx = args.pos.dx;
    int dy = args.pos.dy;
    if (!args.parent && args.pos.IsEmpty()) {
        x = CW_USEDEFAULT;
        y = CW_USEDEFAULT;
        dx = CW_USEDEFAULT;
        dy = CW_USEDEFAULT;
    }

    DWORD tmpStyle = style & ~WS_VISIBLE;
    DWORD exStyle = args.exStyle;
    CrashIf(args.menu && args.cmdId);
    HMENU m = args.menu;
    if (m == nullptr) {
        m = (HMENU)(INT_PTR)args.cmdId;
    }
    HINSTANCE inst = GetInstance();
    void* createParams = this;
    WCHAR* titleW = ToWstrTemp(args.title);

    HWND hwndTmp = ::CreateWindowExW(exStyle, className, titleW, style, x, y, dx, dy, parent, m, inst, createParams);

    CrashIf(!hwndTmp);
    // hwnd should be assigned in WM_CREATE
    CrashIf(hwndTmp != hwnd);
    CrashIf(this != WindowMapGetWindow(hwndTmp));
    if (!hwnd) {
        return nullptr;
    }

    // trigger creating a backgroundBrush
    SetBackgroundColor(args.bgColor);
    if (args.icon) {
        HwndSetIcon(hwnd, args.icon);
    }
    if (style & WS_VISIBLE) {
        if (style & WS_MAXIMIZE)
            ::ShowWindow(hwnd, SW_MAXIMIZE);
        else if (style & WS_MINIMIZE)
            ::ShowWindow(hwnd, SW_MINIMIZE);
        else
            ::ShowWindow(hwnd, SW_SHOWNORMAL);
    }
    return hwnd;
}

// if only top given, set them all to top
// if top, right given, set bottom to top and left to right
void Wnd::SetInsetsPt(int top, int right, int bottom, int left) {
    insets = DpiScaledInsets(hwnd, top, right, bottom, left);
}

void Wnd::Subclass() {
    CrashIf(!IsWindow(hwnd));
    CrashIf(subclassId); // don't subclass multiple times
    if (subclassId) {
        return;
    }
    WindowMapAdd(hwnd, this);

    subclassId = NextSubclassId();
    BOOL ok = SetWindowSubclass(hwnd, StaticWindowProcSubclassed, subclassId, (DWORD_PTR)this);
    CrashIf(!ok);
}

void Wnd::UnSubclass() {
    if (!subclassId) {
        return;
    }
    RemoveWindowSubclass(hwnd, StaticWindowProcSubclassed, subclassId);
    subclassId = 0;
}

HFONT Wnd::GetFont() {
    return font;
}

void Wnd::SetFont(HFONT fontIn) {
    font = fontIn;
    // TODO: for controls, send WM_SETFONT message to original wndproc function
}

void Wnd::SetIsEnabled(bool isEnabled) const {
    CrashIf(!hwnd);
    BOOL enabled = isEnabled ? TRUE : FALSE;
    ::EnableWindow(hwnd, enabled);
}

bool Wnd::IsEnabled() const {
    BOOL enabled = ::IsWindowEnabled(hwnd);
    return tobool(enabled);
}

void Wnd::SetFocus() const {
    ::SetFocus(hwnd);
}

bool Wnd::IsFocused() const {
    BOOL isFocused = ::IsFocused(hwnd);
    return tobool(isFocused);
}

void Wnd::SetRtl(bool isRtl) const {
    CrashIf(!hwnd);
    SetWindowExStyle(hwnd, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRtl);
}

void Wnd::SetBackgroundColor(COLORREF col) {
    if (col == ColorNoChange) {
        return;
    }
    backgroundColor = col;
    if (backgroundColorBrush != nullptr) {
        DeleteBrush(backgroundColorBrush);
        backgroundColorBrush = nullptr;
    }
    if (backgroundColor != ColorUnset) {
        backgroundColorBrush = CreateSolidBrush(backgroundColor);
    }
    // can be set before we create the window
    if (!hwnd) {
        return;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void Wnd::SuspendRedraw() const {
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
}

void Wnd::ResumeRedraw() const {
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
}

// application.cpp
bool PreTranslateMessage(MSG& msg) {
    bool shouldProcess = (WM_KEYFIRST <= msg.message && msg.message <= WM_KEYLAST) ||
                         (WM_MOUSEFIRST <= msg.message && msg.message <= WM_MOUSELAST);
    if (!shouldProcess) {
        return false;
    }
    for (HWND hwnd = msg.hwnd; hwnd != nullptr; hwnd = ::GetParent(hwnd)) {
        if (auto window = WindowMapGetWindow(hwnd)) {
            if (window->PreTranslateMessage(msg)) {
                return true;
            }
        }
    }
    return false;
}

static void SizeToIdealSize(Wnd* wnd) {
    if (!wnd || !wnd->hwnd) {
        return;
    }
    auto size = wnd->GetIdealSize();
    // TODO: don't change x,y, only dx/dy
    RECT r{0, 0, size.dx, size.dy};
    wnd->SetBounds(r);
}

//- Static

// https://docs.microsoft.com/en-us/windows/win32/controls/static-controls

Kind kindStatic = "static";

Static::Static() {
    kind = kindStatic;
}

HWND Static::Create(const StaticCreateArgs& args) {
    CreateControlArgs cargs;
    cargs.className = WC_STATICW;
    cargs.parent = args.parent;
    cargs.font = args.font;
    cargs.style = WS_CHILD | WS_VISIBLE | SS_NOTIFY;
    cargs.text = args.text;

    Wnd::CreateControl(cargs);
    SizeToIdealSize(this);

    return hwnd;
}

Size Static::GetIdealSize() {
    CrashIf(!hwnd);
    char* txt = HwndGetTextTemp(hwnd);
    HFONT hfont = GetWindowFont(hwnd);
    return HwndMeasureText(hwnd, txt, hfont);
}

bool Static::OnCommand(WPARAM wparam, LPARAM lparam) {
    auto code = HIWORD(wparam);
    if (code == STN_CLICKED && onClicked) {
        onClicked();
        return true;
    }
    return false;
}

#if 0
void Handle_WM_CTLCOLORSTATIC(void* user, WndEvent* ev) {
    auto w = (StaticCtrl*)user;
    uint msg = ev->msg;
    CrashIf(msg != WM_CTLCOLORSTATIC);
    HDC hdc = (HDC)ev->wp;
    if (w->textColor != ColorUnset) {
        SetTextColor(hdc, w->textColor);
    }
    // the brush we return is the background color for the whole
    // area of static control
    // SetBkColor() is just for the part where the text is
    // SetBkMode(hdc, TRANSPARENT) sets the part of the text to transparent
    // (but the whole background is still controlled by the bruhs
    auto bgBrush = w->backgroundColorBrush;
    if (bgBrush != nullptr) {
        SetBkColor(hdc, w->backgroundColor);
        ev->result = (LRESULT)bgBrush;
    } else {
        SetBkMode(hdc, TRANSPARENT);
    }
    ev->didHandle = true;
}
#endif

LRESULT Static::OnMessageReflect(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_CTLCOLORSTATIC) {
        // TODO: implement me
        return 0;
    }
    return 0;
}

//--- Button

// https://docs.microsoft.com/en-us/windows/win32/controls/buttons

Kind kindButton = "button";

Button::Button() {
    kind = kindButton;
}

bool Button::OnCommand(WPARAM wparam, LPARAM lparam) {
    auto code = HIWORD(wparam);
    if (code == BN_CLICKED && onClicked) {
        onClicked();
        return true;
    }
    return false;
}

LRESULT Button::OnMessageReflect(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_CTLCOLORBTN) {
        // TODO: implement me
        return 0;
    }
    return 0;
}

HWND Button::Create(const ButtonCreateArgs& args) {
    CreateControlArgs cargs;
    cargs.className = WC_BUTTONW;
    cargs.parent = args.parent;
    cargs.font = args.font;
    cargs.style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    if (isDefault) {
        cargs.style |= BS_DEFPUSHBUTTON;
    } else {
        cargs.style |= BS_PUSHBUTTON;
    }
    cargs.text = args.text;

    Wnd::CreateControl(cargs);
    SizeToIdealSize(this);

    return hwnd;
}

Size Button::GetIdealSize() {
    return ButtonGetIdealSize(hwnd);
}

#if 0
Size Button::SetTextAndResize(const WCHAR* s) {
    HwndSetText(this->hwnd, s);
    Size size = this->GetIdealSize();
    uint flags = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;
    SetWindowPos(this->hwnd, nullptr, 0, 0, size.dx, size.dy, flags);
    return size;
}
#endif

Button* CreateButton(HWND parent, const WCHAR* s, const ClickedHandler& onClicked) {
    ButtonCreateArgs args;
    args.parent = parent;
    args.text = ToUtf8Temp(s);

    auto b = new Button();
    b->onClicked = onClicked;
    b->Create(args);
    return b;
}

#define kButtonMargin 8

Button* CreateDefaultButton(HWND parent, const WCHAR* s) {
    ButtonCreateArgs args;
    args.parent = parent;
    args.text = ToUtf8Temp(s);

    auto* b = new Button();
    b->Create(args);

    RECT r = ClientRECT(parent);
    Size size = b->GetIdealSize();
    int margin = DpiScale(parent, kButtonMargin);
    int x = RectDx(r) - size.dx - margin;
    int y = RectDy(r) - size.dy - margin;
    r.left = x;
    r.right = x + size.dx;
    r.top = y;
    r.bottom = y + size.dy;
    b->SetPos(&r);
    return b;
}

Button* CreateDefaultButton(HWND parent, const char* s) {
    ButtonCreateArgs args;
    args.parent = parent;
    args.text = s;

    auto* b = new Button();
    b->Create(args);

    RECT r = ClientRECT(parent);
    Size size = b->GetIdealSize();
    int margin = DpiScale(parent, kButtonMargin);
    int x = RectDx(r) - size.dx - margin;
    int y = RectDy(r) - size.dy - margin;
    r.left = x;
    r.right = x + size.dx;
    r.top = y;
    r.bottom = y + size.dy;
    b->SetPos(&r);
    return b;
}

//--- Tooltip

// https://docs.microsoft.com/en-us/windows/win32/controls/tooltip-control-reference

Kind kindTooltip = "tooltip";

Tooltip::Tooltip() {
    kind = kindTooltip;
}

HWND Tooltip::Create(const TooltipCreateArgs& args) {
    CreateControlArgs cargs;
    cargs.className = TOOLTIPS_CLASS;
    cargs.font = args.font;
    cargs.style = WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP;
    cargs.exStyle = WS_EX_TOPMOST;

    parent = args.parent;

    Wnd::CreateControl(cargs);
    SetDelayTime(TTDT_AUTOPOP, 32767);
    return hwnd;
}
Size Tooltip::GetIdealSize() {
    return {100, 32}; // not used as this is top-level window
}

void Tooltip::SetMaxWidth(int dx) {
    SendMessageW(hwnd, TTM_SETMAXTIPWIDTH, 0, dx);
}

static const int MULTILINE_INFOTIP_WIDTH_PX = 500;

static void SetMaxWidthForText(HWND hwnd, const char* s, bool multiline) {
    int dx = -1;
    if (multiline || str::FindChar(s, '\n')) {
        // TODO: dpi scale
        dx = MULTILINE_INFOTIP_WIDTH_PX;
    }
    SendMessageW(hwnd, TTM_SETMAXTIPWIDTH, 0, dx);
}

void Tooltip::ShowOrUpdate(const char* s, const Rect& rc, bool multiline) {
    WCHAR* ws = ToWstrTemp(s);
    bool isShowing = IsShowing();
    if (!isShowing) {
        SetMaxWidthForText(hwnd, s, multiline);
        TOOLINFOW ti = {0};
        ti.cbSize = sizeof(ti);
        ti.hwnd = parent;
        ti.uFlags = TTF_SUBCLASS;
        ti.rect = ToRECT(rc);
        ti.lpszText = (WCHAR*)ws;
        SendMessageW(hwnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        return;
    }

    constexpr int bufSize = 512;
    WCHAR buf[bufSize] = {0};
    TOOLINFOW tiCurr = {0};
    tiCurr.cbSize = sizeof(tiCurr);
    tiCurr.hwnd = parent;
    tiCurr.lpszText = buf;
    SendMessageW(hwnd, TTM_GETTEXT, bufSize - 1, (LPARAM)&tiCurr);
    // TODO: should also compare ti.rect wit rc
    if (str::Eq(buf, ws)) {
        return;
    }

    SetMaxWidthForText(hwnd, s, multiline);
    tiCurr.lpszText = (WCHAR*)ws;
    tiCurr.uFlags = TTF_SUBCLASS;
    tiCurr.rect = ToRECT(rc);
    SendMessageW(hwnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&tiCurr);
    SendMessageW(hwnd, TTM_NEWTOOLRECT, 0, (LPARAM)&tiCurr);
}

int Tooltip::Count() {
    int n = (int)SendMessageW(hwnd, TTM_GETTOOLCOUNT, 0, 0);
    return n;
}

bool Tooltip::IsShowing() {
    return Count() > 0;
}

void Tooltip::Hide() {
    if (!IsShowing()) {
        return;
    }

    TOOLINFO ti{0};
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd;
    SendMessageW(hwnd, TTM_DELTOOL, 0, (LPARAM)&ti);
}

// https://docs.microsoft.com/en-us/windows/win32/controls/ttm-setdelaytime
// type is: TTDT_AUTOPOP, TTDT_INITIAL, TTDT_RESHOW, TTDT_AUTOMATIC
// timeInMs is max 32767 (~32 secs)
void Tooltip::SetDelayTime(int type, int timeInMs) {
    CrashIf(!IsValidDelayType(type));
    CrashIf(timeInMs < 0);
    CrashIf(timeInMs > 32767); // TODO: or is it 65535?
    SendMessageW(hwnd, TTM_SETDELAYTIME, type, (LPARAM)timeInMs);
}

//--- Edit

// https://docs.microsoft.com/en-us/windows/win32/controls/edit-controls

// TODO:
// - expose EN_UPDATE
// https://docs.microsoft.com/en-us/windows/win32/controls/en-update
// - add border and possibly other decorations by handling WM_NCCALCSIZE, WM_NCPAINT and
// WM_NCHITTEST
//   etc., http://www.catch22.net/tuts/insert-buttons-edit-control
// - include value we remember in WM_NCCALCSIZE in GetIdealSize()

Kind kindEdit = "edit";

static bool EditSetCueText(HWND hwnd, const char* s) {
    if (!hwnd) {
        return false;
    }
    WCHAR* ws = ToWstrTemp(s);
    bool ok = Edit_SetCueBannerText(hwnd, ws) == TRUE;
    return ok;
}

Edit::Edit() {
    kind = kindEdit;
}

Edit::~Edit() {
    // DeleteObject(bgBrush);
}

void Edit::SetSelection(int start, int end) {
    Edit_SetSel(hwnd, start, end);
}

HWND Edit::Create(const EditCreateArgs& editArgs) {
    // https://docs.microsoft.com/en-us/windows/win32/controls/edit-control-styles
    CreateControlArgs args;
    args.className = WC_EDITW;
    args.parent = editArgs.parent;
    args.font = editArgs.font;
    args.style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT;
    if (editArgs.withBorder) {
        args.exStyle = WS_EX_CLIENTEDGE;
        // Note: when using WS_BORDER, we would need to remember
        // we have border and use it in Edit::HasBorder
        // args.style |= WS_BORDER;
    }
    if (editArgs.isMultiLine) {
        args.style |= ES_MULTILINE | WS_VSCROLL | ES_WANTRETURN;
    } else {
        // ES_AUTOHSCROLL disable wrapping in multi-line setup
        args.style |= ES_AUTOHSCROLL;
    }
    idealSizeLines = editArgs.idealSizeLines;
    if (idealSizeLines < 1) {
        idealSizeLines = 1;
    }
    Wnd::CreateControl(args);
    if (!hwnd) {
        return nullptr;
    }
    SizeToIdealSize(this);

    if (editArgs.cueText) {
        EditSetCueText(hwnd, editArgs.cueText);
    }
    return hwnd;
}

bool Edit::HasBorder() {
    DWORD exStyle = GetWindowExStyle(hwnd);
    bool res = bit::IsMaskSet<DWORD>(exStyle, WS_EX_CLIENTEDGE);
    return res;
}

Size Edit::GetIdealSize() {
    HFONT hfont = HwndGetFont(hwnd);
    Size s1 = HwndMeasureText(hwnd, "Minimal", hfont);
    // logf("Edit::GetIdealSize: s1.dx=%d, s2.dy=%d\n", (int)s1.cx, (int)s1.cy);
    char* txt = HwndGetTextTemp(hwnd);
    Size s2 = HwndMeasureText(hwnd, txt, hfont);
    // logf("Edit::GetIdealSize: s2.dx=%d, s2.dy=%d\n", (int)s2.cx, (int)s2.cy);

    int dx = std::max(s1.dx, s2.dx);
    if (maxDx > 0 && dx > maxDx) {
        dx = maxDx;
    }
    // for multi-line text, this measures multiple line.
    // TODO: maybe figure out better protocol
    int dy = std::min(s1.dy, s2.dy);
    if (dy == 0) {
        dy = std::max(s1.dy, s2.dy);
    }
    dy = dy * idealSizeLines;
    // logf("Edit::GetIdealSize: dx=%d, dy=%d\n", (int)dx, (int)dy);

    LRESULT margins = SendMessageW(hwnd, EM_GETMARGINS, 0, 0);
    int lm = (int)LOWORD(margins);
    int rm = (int)HIWORD(margins);
    dx += lm + rm;

    if (HasBorder()) {
        dx += DpiScale(hwnd, 4);
        dy += DpiScale(hwnd, 8);
    }
    // logf("Edit::GetIdealSize(): dx=%d, dy=%d\n", int(res.cx), int(res.cy));
    return {dx, dy};
}

// https://docs.microsoft.com/en-us/windows/win32/controls/en-change
bool Edit::OnCommand(WPARAM wparam, LPARAM lparam) {
    auto code = HIWORD(wparam);
    if (code == EN_CHANGE && onTextChanged) {
        onTextChanged();
        return true;
    }
    return false;
}

#if 0
// https://docs.microsoft.com/en-us/windows/win32/controls/wm-ctlcoloredit
static void Handle_WM_CTLCOLOREDIT(void* user, WndEvent* ev) {
    auto w = (EditCtrl*)user;
    CrashIf(ev->msg != WM_CTLCOLOREDIT);
    HWND hwndCtrl = (HWND)ev->lp;
    CrashIf(hwndCtrl != w->hwnd);
    if (w->bgBrush == nullptr) {
        return;
    }
    HDC hdc = (HDC)ev->wp;
    // SetBkColor(hdc, w->bgCol);
    SetBkMode(hdc, TRANSPARENT);
    if (w->textColor != ColorUnset) {
        ::SetTextColor(hdc, w->textColor);
    }
    ev->didHandle = true;
    ev->result = (INT_PTR)w->bgBrush;
}
#endif

LRESULT Edit::OnMessageReflect(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_CTLCOLOREDIT) {
        // TOOD: return brush
        return 0;
    }
    return 0;
}

Kind kindListBox = "listbox";

ListBox::ListBox() {
    kind = kindListBox;
#if 0
    ctrlID = 0;
#endif
}

ListBox::~ListBox() {
    delete this->model;
}

HWND ListBox::Create(const ListBoxCreateArgs& args) {
    idealSizeLines = args.idealSizeLines;
    if (idealSizeLines < 0) {
        idealSizeLines = 0;
    }
    idealSize = {DpiScale(args.parent, 120), DpiScale(args.parent, 32)};

    CreateControlArgs cargs;
    cargs.className = L"LISTBOX";
    cargs.parent = args.parent;
    cargs.font = args.font;

    // https://docs.microsoft.com/en-us/windows/win32/controls/list-box-styles
    cargs.style = WS_CHILD | WS_TABSTOP | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL;
    cargs.style |= LBS_NOINTEGRALHEIGHT | LBS_NOTIFY;
    // args.style |= WS_BORDER;
    Wnd::CreateControl(cargs);
    SizeToIdealSize(this);

    if (hwnd) {
        if (model != nullptr) {
            FillWithItems(this->hwnd, model);
        }
    }

    return hwnd;
}

// https://docs.microsoft.com/en-us/windows/win32/controls/lb-getitemheight
int ListBox::GetItemHeight(int idx) {
    // idx only valid for LBS_OWNERDRAWVARIABLE, otherwise should be 0
    int res = (int)SendMessageW(hwnd, LB_GETITEMHEIGHT, idx, 0);
    if (res == LB_ERR) {
        // if failed for some reason, fallback to measuring text in default font
        // HFONT f = GetFont();
        HFONT f = GetDefaultGuiFont();
        Size sz = HwndMeasureText(hwnd, L"A", f);
        res = sz.dy;
    }
    return res;
}

Size ListBox::GetIdealSize() {
    Size res = idealSize;
    if (idealSizeLines > 0) {
        int dy = GetItemHeight(0) * idealSizeLines + DpiScale(hwnd, 2 + 2); // padding of 2 at top and bottom
        res.dy = dy;
    }
    return res;
}

int ListBox::GetCount() {
    LRESULT res = ListBox_GetCount(hwnd);
    return (int)res;
}

int ListBox::GetCurrentSelection() {
    LRESULT res = ListBox_GetCurSel(hwnd);
    return (int)res;
}

// -1 to clear selection
// returns false on error
bool ListBox::SetCurrentSelection(int n) {
    if (n < 0) {
        ListBox_SetCurSel(hwnd, -1);
        return true;
    }
    int nItems = model->ItemsCount();
    if (n >= nItems) {
        return false;
    }
    LRESULT res = ListBox_SetCurSel(hwnd, n);
    return res != LB_ERR;
}

// for efficiency you can re-use model:
// get the model, change data, call SetModel() again
void ListBox::SetModel(ListBoxModel* model) {
    if (this->model && (this->model != model)) {
        delete this->model;
    }
    this->model = model;
    if (model != nullptr) {
        FillWithItems(this->hwnd, model);
    }
    SetCurrentSelection(-1);
    // TODO: update ideal size based on the size of the model
}

bool ListBox::OnCommand(WPARAM wparam, LPARAM lparam) {
    auto code = HIWORD(wparam);
    // https://docs.microsoft.com/en-us/windows/win32/controls/lbn-selchange
    if (code == LBN_SELCHANGE && onSelectionChanged) {
        onSelectionChanged();
        return true;
    }
    // https://docs.microsoft.com/en-us/windows/win32/controls/lbn-dblclk
    if (code == LBN_DBLCLK && onDoubleClick) {
        onDoubleClick();
        return true;
    }
    return false;
}

LRESULT ListBox::OnMessageReflect(UINT msg, WPARAM wparam, LPARAM lparam) {
    // https://docs.microsoft.com/en-us/windows/win32/controls/wm-ctlcolorlistbox
    if (msg == WM_CTLCOLORLISTBOX) {
        // TOOD: implement me
        return 0;
    }
    return 0;
}

//- Checkbox

// https://docs.microsoft.com/en-us/windows/win32/controls/buttons

static Kind kindCheckbox = "checkbox";

static CheckState GetButtonCheckState(HWND hwnd) {
    auto res = Button_GetCheck(hwnd);
    return (CheckState)res;
}

static void SetButtonCheckState(HWND hwnd, CheckState newState) {
    CrashIf(!hwnd);
    Button_SetCheck(hwnd, newState);
}

Checkbox::Checkbox() {
    kind = kindCheckbox;
}

HWND Checkbox::Create(const CheckboxCreateArgs& args) {
    CreateControlArgs cargs;
    cargs.parent = args.parent;
    cargs.text = args.text;
    cargs.className = WC_BUTTONW;
    cargs.style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;

    Wnd::CreateControl(cargs);
    SetButtonCheckState(hwnd, args.initialState);
    SizeToIdealSize(this);
    return hwnd;
}

bool Checkbox::OnCommand(WPARAM wp, LPARAM) {
    auto code = HIWORD(wp);
    if (code == BN_CLICKED && onCheckStateChanged) {
        onCheckStateChanged();
        return true;
    }
    return false;
}

Size Checkbox::GetIdealSize() {
    return ButtonGetIdealSize(hwnd);
}

void Checkbox::SetCheckState(CheckState newState) {
    CrashIf(!hwnd);
    SetButtonCheckState(hwnd, newState);
}

CheckState Checkbox::GetCheckState() const {
    return GetButtonCheckState(hwnd);
}

void Checkbox::SetIsChecked(bool isChecked) {
    CrashIf(!hwnd);
    CheckState newState = isChecked ? CheckState::Checked : CheckState::Unchecked;
    SetButtonCheckState(hwnd, newState);
}

bool Checkbox::IsChecked() const {
    CrashIf(!hwnd);
    auto state = GetCheckState();
    return state == CheckState::Checked;
}

//- Progress

// https://docs.microsoft.com/en-us/windows/win32/controls/progress-bar-control-reference

Kind kindProgress = "progress";

Progress::Progress() {
    kind = kindProgress;
}

HWND Progress::Create(const ProgressCreateArgs& args) {
    CreateControlArgs cargs;
    cargs.parent = args.parent;
    cargs.style = WS_CHILD | WS_VISIBLE;
    cargs.className = PROGRESS_CLASSW;

    Wnd::CreateControl(cargs);
    SizeToIdealSize(this);
    if (hwnd && args.initialMax != 0) {
        SetMax(args.initialMax);
    }
    return hwnd;
}

Size Progress::GetIdealSize() {
    return {idealDx, idealDy};
}

void Progress::SetMax(int newMax) {
    int min = 0;
    SendMessageW(hwnd, PBM_SETRANGE32, min, newMax);
}

void Progress::SetCurrent(int newCurrent) {
    SendMessageW(hwnd, PBM_SETPOS, newCurrent, 0);
}

int Progress::GetMax() {
    auto max = (int)SendMessageW(hwnd, PBM_GETRANGE, FALSE /* get high limit */, 0);
    return max;
}

int Progress::GetCurrent() {
    auto current = (int)SendMessageW(hwnd, PBM_GETPOS, 0, 0);
    return current;
}

//- DropDown

// https://docs.microsoft.com/en-us/windows/win32/controls/combo-boxes

Kind kindDropDown = "dropdown";

DropDown::DropDown() {
    kind = kindDropDown;
}

static void SetDropDownItems(HWND hwnd, StrVec& items) {
    ComboBox_ResetContent(hwnd);
    int n = items.Size();
    for (int i = 0; i < n; i++) {
        char* s = items[i];
        WCHAR* ws = ToWstrTemp(s);
        ComboBox_AddString(hwnd, ws);
    }
}

bool DropDown::OnCommand(WPARAM wp, LPARAM) {
    auto code = HIWORD(wp);
    if ((code == CBN_SELCHANGE) && onSelectionChanged) {
        onSelectionChanged();
        // must return false or else the drop-down list will not close
        return false;
    }
    return false;
}

HWND DropDown::Create(const DropDownCreateArgs& args) {
    CreateControlArgs cargs;
    cargs.parent = args.parent;
    cargs.style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST;
    cargs.className = WC_COMBOBOX;

    Wnd::CreateControl(cargs);
    if (!hwnd) {
        return nullptr;
    }

    // SetDropDownItems(hwnd, items);
    SetCurrentSelection(-1);

    SizeToIdealSize(this);
    return hwnd;
}

// -1 means no selection
int DropDown::GetCurrentSelection() {
    int res = (int)ComboBox_GetCurSel(hwnd);
    return res;
}

// -1 : no selection
void DropDown::SetCurrentSelection(int n) {
    if (n < 0) {
        ComboBox_SetCurSel(hwnd, -1);
        return;
    }
    int nItems = (int)items.size();
    CrashIf(n >= nItems);
    ComboBox_SetCurSel(hwnd, n);
}

void DropDown::SetCueBanner(const char* sv) {
    auto ws = ToWstrTemp(sv);
    ComboBox_SetCueBannerText(hwnd, ws);
}

void DropDown::SetItems(StrVec& newItems) {
    items.Reset();
    int n = newItems.Size();
    for (int i = 0; i < n; i++) {
        char* s = newItems[i];
        items.Append(s);
    }
    SetDropDownItems(hwnd, items);
    SetCurrentSelection(-1);
}

static void DropDownItemsFromStringArray(StrVec& items, const char* strings) {
    for (; strings; seqstrings::Next(strings)) {
        items.Append(strings);
    }
}

void DropDown::SetItemsSeqStrings(const char* items) {
    StrVec strings;
    DropDownItemsFromStringArray(strings, items);
    SetItems(strings);
}

Size DropDown::GetIdealSize() {
    HFONT hfont = GetWindowFont(hwnd);
    Size s1 = TextSizeInHwnd(hwnd, L"Minimal", hfont);

    int n = items.Size();
    for (int i = 0; i < n; i++) {
        char* s = items[i];
        WCHAR* ws = ToWstrTemp(s);
        Size s2 = TextSizeInHwnd(hwnd, ws, hfont);
        s1.dx = std::max(s1.dx, s2.dx);
        s1.dy = std::max(s1.dy, s2.dy);
    }
    // TODO: not sure if I want scrollbar. Only needed if a lot of items
    int dxPad = GetSystemMetrics(SM_CXVSCROLL);
    int dx = s1.dx + dxPad + DpiScale(hwnd, 8);
    // TODO: 5 is a guessed number.
    int dyPad = DpiScale(hwnd, 4);
    int dy = s1.dy + dyPad;
    Rect rc = WindowRect(hwnd);
    if (rc.dy > dy) {
        dy = rc.dy;
    }
    return {dx, dy};
}

//- Trackbar

// https://docs.microsoft.com/en-us/windows/win32/controls/trackbar-control-reference

Kind kindTrackbar = "trackbar";

Trackbar::Trackbar() {
    kind = kindTrackbar;
}

// https://docs.microsoft.com/en-us/windows/win32/controls/wm-vscroll--trackbar-
HWND Trackbar::Create(const TrackbarCreateArgs& args) {
    DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    dwStyle |= TBS_AUTOTICKS; // tick marks for each increment
    dwStyle |= TBS_TOOLTIPS;  // show current value when dragging in a tooltip
    if (args.isHorizontal) {
        dwStyle |= TBS_HORZ;
        idealSize.dx = 32;
        idealSize.dy = DpiScale(args.parent, 22);
    } else {
        dwStyle |= TBS_VERT;
        idealSize.dy = 32;
        idealSize.dx = DpiScale(args.parent, 22);
    }

    CreateControlArgs cargs;
    cargs.className = TRACKBAR_CLASS;
    cargs.parent = args.parent;

    // TODO: add initial size to CreateControlArgs
    // initialSize = idealSize;

    cargs.style = dwStyle;
    // args.style |= WS_BORDER;
    Wnd::CreateControl(cargs);
    SizeToIdealSize(this);

    if (hwnd) {
        SetRange(args.rangeMin, args.rangeMax);
        SetValue(args.rangeMin);
    }
    return hwnd;
}

// https://docs.microsoft.com/en-us/windows/win32/controls/wm-hscroll--trackbar-
// https://docs.microsoft.com/en-us/windows/win32/controls/wm-vscroll--trackbar-
LRESULT Trackbar::OnMessageReflect(UINT msg, WPARAM wp, LPARAM) {
    if (!onPosChanging) {
        return 0;
    }
    switch (msg) {
        case WM_VSCROLL:
        case WM_HSCROLL: {
            int pos = (int)HIWORD(wp);
            int code = (int)LOWORD(wp);
            switch (code) {
                case TB_THUMBPOSITION:
                case TB_THUMBTRACK:
                    // pos is HIWORD so do nothing
                    break;
                default:
                    pos = GetValue();
            }
            TrackbarPosChangingEvent a{};
            a.trackbar = this;
            a.pos = pos;
            onPosChanging(&a);
            // per https://docs.microsoft.com/en-us/windows/win32/controls/wm-vscroll--trackbar-
            // "if an application processes this message, it should return zero"
            return 0;
        }
    }

    return 0;
}

Size Trackbar::GetIdealSize() {
    return {idealSize.dx, idealSize.dy};
}

void Trackbar::SetRange(int min, int max) {
    WPARAM redraw = (WPARAM)TRUE;
    LPARAM range = (LPARAM)MAKELONG(min, max);
    SendMessageW(hwnd, TBM_SETRANGE, redraw, range);
}

int Trackbar::GetRangeMin() {
    int res = SendMessageW(hwnd, TBM_GETRANGEMIN, 0, 0);
    return res;
}

int Trackbar::getRangeMax() {
    int res = SendMessageW(hwnd, TBM_GETRANGEMAX, 0, 0);
    return res;
}

void Trackbar::SetValue(int pos) {
    WPARAM redraw = (WPARAM)TRUE;
    LPARAM p = (LPARAM)pos;
    SendMessageW(hwnd, TBM_SETPOS, redraw, p);
}

int Trackbar::GetValue() {
    int res = (int)SendMessageW(hwnd, TBM_GETPOS, 0, 0);
    return res;
}

//- Splitter

// the technique for drawing the splitter for non-live resize is described
// at http://www.catch22.net/tuts/splitter-windows

Kind kindSplitter = "splitter";

static void OnSplitterPaint(HWND hwnd, COLORREF bgCol) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    AutoDeleteBrush br = CreateSolidBrush(bgCol);
    FillRect(hdc, &ps.rcPaint, br);
    EndPaint(hwnd, &ps);
}

static void DrawXorBar(HDC hdc, HBRUSH br, int x1, int y1, int width, int height) {
    SetBrushOrgEx(hdc, x1, y1, nullptr);
    HBRUSH hbrushOld = (HBRUSH)SelectObject(hdc, br);
    PatBlt(hdc, x1, y1, width, height, PATINVERT);
    SelectObject(hdc, hbrushOld);
}

static HDC InitDraw(HWND hwnd, Rect& rc) {
    rc = ChildPosWithinParent(hwnd);
    HDC hdc = GetDC(GetParent(hwnd));
    SetROP2(hdc, R2_NOTXORPEN);
    return hdc;
}

static void DrawResizeLineV(HWND hwnd, HBRUSH br, int x) {
    Rect rc;
    HDC hdc = InitDraw(hwnd, rc);
    DrawXorBar(hdc, br, x, rc.y, 4, rc.dy);
    ReleaseDC(GetParent(hwnd), hdc);
}

static void DrawResizeLineH(HWND hwnd, HBRUSH br, int y) {
    Rect rc;
    HDC hdc = InitDraw(hwnd, rc);
    DrawXorBar(hdc, br, rc.x, y, rc.dx, 4);
    ReleaseDC(GetParent(hwnd), hdc);
}

static void DrawResizeLineVH(HWND hwnd, HBRUSH br, bool isVert, Point pos) {
    if (isVert) {
        DrawResizeLineV(hwnd, br, pos.x);
    } else {
        DrawResizeLineH(hwnd, br, pos.y);
    }
}

static void DrawResizeLine(HWND hwnd, HBRUSH br, SplitterType stype, bool erasePrev, bool drawCurr,
                           Point& prevResizeLinePos) {
    Point pos = HwndGetCursorPos(GetParent(hwnd));
    bool isVert = stype != SplitterType::Horiz;

    if (erasePrev) {
        DrawResizeLineVH(hwnd, br, isVert, prevResizeLinePos);
    }
    if (drawCurr) {
        DrawResizeLineVH(hwnd, br, isVert, pos);
    }
    prevResizeLinePos = pos;
}

static WORD dotPatternBmp[8] = {0x00aa, 0x0055, 0x00aa, 0x0055, 0x00aa, 0x0055, 0x00aa, 0x0055};

Splitter::Splitter() {
    kind = kindSplitter;
}

Splitter::~Splitter() {
    DeleteObject(brush);
    DeleteObject(bmp);
}

HWND Splitter::Create(const SplitterCreateArgs& args) {
    CrashIf(!args.parent);

    isLive = args.isLive;
    type = args.type;
    backgroundColor = args.backgroundColor;
    if (backgroundColor == ColorUnset) {
        backgroundColor = GetSysColor(COLOR_BTNFACE);
    }

    bmp = CreateBitmap(8, 8, 1, 1, dotPatternBmp);
    CrashIf(!bmp);
    brush = CreatePatternBrush(bmp);
    CrashIf(!brush);

    DWORD style = GetWindowLong(args.parent, GWL_STYLE);
    parentClipsChildren = bit::IsMaskSet<DWORD>(style, WS_CLIPCHILDREN);

    CreateCustomArgs cargs;
    // cargs.className = L"SplitterWndClass";
    cargs.parent = args.parent;
    cargs.style = WS_CHILDWINDOW;
    cargs.exStyle = 0;
    CreateCustom(cargs);

    return hwnd;
}

LRESULT Splitter::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (WM_ERASEBKGND == msg) {
        // TODO: should this be FALSE?
        return TRUE;
    }

    if (WM_LBUTTONDOWN == msg) {
        SetCapture(hwnd);
        if (!isLive) {
            if (parentClipsChildren) {
                SetWindowStyle(GetParent(hwnd), WS_CLIPCHILDREN, false);
            }
            DrawResizeLine(hwnd, brush, type, false, true, prevResizeLinePos);
        }
        return 1;
    }

    if (WM_LBUTTONUP == msg) {
        if (!isLive) {
            DrawResizeLine(hwnd, brush, type, true, false, prevResizeLinePos);
            if (parentClipsChildren) {
                SetWindowStyle(GetParent(hwnd), WS_CLIPCHILDREN, true);
            }
        }
        ReleaseCapture();
        SplitterMoveEvent arg;
        arg.w = this;
        arg.done = true;
        onSplitterMove(&arg);
        ScheduleRepaint(hwnd);
        return 0;
    }

    if (WM_MOUSEMOVE == msg) {
        LPWSTR curId = IDC_SIZENS;
        if (SplitterType::Vert == type) {
            curId = IDC_SIZEWE;
        }
        if (hwnd == GetCapture()) {
            SplitterMoveEvent arg;
            arg.w = this;
            arg.done = false;
            onSplitterMove(&arg);
            if (!arg.resizeAllowed) {
                curId = IDC_NO;
            } else if (!isLive) {
                DrawResizeLine(hwnd, brush, type, true, true, prevResizeLinePos);
            }
        }
        SetCursorCached(curId);
        return 0;
    }

    if (WM_PAINT == msg) {
        OnSplitterPaint(hwnd, backgroundColor);
        return 0;
    }

    return WndProcDefault(hwnd, msg, wparam, lparam);
}

#if 0
// Convert ASCII hex digit to a nibble (four bits, 0 - 15).
//
// Use unsigned to avoid signed overflow UB.
unsigned char hex2nibble(unsigned char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    } else if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return 0;
}

// Convert ASCII hex string (two characters) to byte.
//
// E.g., "0B" => 0x0B, "af" => 0xAF.
char hex2char(const char* p) {
    return hex2nibble(p[0]) * 16 + hex2nibble(p[1]);
}

std::string url_encode(const std::string s) {
    std::string encoded;
    for (unsigned int i = 0; i < s.length(); i++) {
        auto c = s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded = encoded + c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02x", c);
            encoded = encoded + hex;
        }
    }
    return encoded;
}

std::string url_decode(const std::string st) {
    std::string decoded;
    const char* s = st.c_str();
    size_t length = strlen(s);
    for (unsigned int i = 0; i < length; i++) {
        if (s[i] == '%') {
            decoded.push_back(hex2char(s + i + 1));
            i = i + 2;
        } else if (s[i] == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(s[i]);
        }
    }
    return decoded;
}

std::string html_from_uri(const std::string s) {
    if (s.substr(0, 15) == "data:text/html,") {
        return url_decode(s.substr(15));
    }
    return "";
}
#endif

//--- WebView

Kind kindWebView = "webView";

class webview2_com_handler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                             public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                             public ICoreWebView2WebMessageReceivedEventHandler,
                             public ICoreWebView2PermissionRequestedEventHandler {
    using webview2_com_handler_cb_t = std::function<void(ICoreWebView2Controller*)>;

  public:
    webview2_com_handler(HWND hwnd, WebViewMsgCb msgCb, webview2_com_handler_cb_t cb)
        : m_window(hwnd), msgCb(msgCb), m_cb(cb) {
    }
    ULONG STDMETHODCALLTYPE AddRef() {
        return 1;
    }

    ULONG STDMETHODCALLTYPE Release() {
        return 1;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* ppv) {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT res, ICoreWebView2Environment* env) {
        env->CreateCoreWebView2Controller(m_window, this);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT res, ICoreWebView2Controller* controller) {
        controller->AddRef();

        ICoreWebView2* webview;
        ::EventRegistrationToken token;
        controller->get_CoreWebView2(&webview);
        webview->add_WebMessageReceived(this, &token);
        webview->add_PermissionRequested(this, &token);

        m_cb(controller);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) {
        WCHAR* message = nullptr;
        args->TryGetWebMessageAsString(&message);
        if (!message) {
            return S_OK;
        }
        char* s = ToUtf8Temp(message);
        msgCb(s);
        sender->PostWebMessageAsString(message);
        CoTaskMemFree(message);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2PermissionRequestedEventArgs* args) {
        COREWEBVIEW2_PERMISSION_KIND kind;
        args->get_PermissionKind(&kind);
        if (kind == COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ) {
            args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
        }
        return S_OK;
    }

  private:
    HWND m_window;
    WebViewMsgCb msgCb;
    webview2_com_handler_cb_t m_cb;
};

Webview2Wnd::Webview2Wnd() {
    kind = kindWebView;
}

void Webview2Wnd::UpdateWebviewSize() {
    if (controller == nullptr) {
        return;
    }
    RECT bounds = ClientRECT(hwnd);
    controller->put_Bounds(bounds);
}

void Webview2Wnd::Eval(const char* js) {
    WCHAR* ws = ToWstrTemp(js);
    webview->ExecuteScript(ws, nullptr);
}

void Webview2Wnd::SetHtml(const char* html) {
#if 0
        std::string s = "data:text/html,";
        s += url_encode(html);
        WCHAR* html2 = ToWstrTemp(s.c_str());
        m_webview->Navigate(html2);
#else
    WCHAR* html2 = ToWstrTemp(html);
    webview->NavigateToString(html2);
#endif
}

void Webview2Wnd::Init(const char* js) {
    WCHAR* ws = ToWstrTemp(js);
    webview->AddScriptToExecuteOnDocumentCreated(ws, nullptr);
}

void Webview2Wnd::Navigate(const char* url) {
    WCHAR* ws = ToWstrTemp(url);
    webview->Navigate(ws);
}

/*
Settings:
put_IsWebMessageEnabled(BOOL isWebMessageEnabled)
put_AreDefaultScriptDialogsEnabled(BOOL areDefaultScriptDialogsEnabled)
put_IsStatusBarEnabled(BOOL isStatusBarEnabled)
put_AreDevToolsEnabled(BOOL areDevToolsEnabled)
put_AreDefaultContextMenusEnabled(BOOL enabled)
put_AreHostObjectsAllowed(BOOL allowed)
put_IsZoomControlEnabled(BOOL enabled)
put_IsBuiltInErrorPageEnabled(BOOL enabled)
*/

bool Webview2Wnd::Embed(WebViewMsgCb cb) {
    // TODO: not sure if flag needs to be atomic i.e. is CreateCoreWebView2EnvironmentWithOptions()
    // called on a different thread?
    LONG flag = 0;
    // InterlockedCompareExchange()
    WCHAR* userDataFolder = ToWstrTemp(dataDir);
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder, nullptr, new webview2_com_handler(hwnd, cb, [&](ICoreWebView2Controller* ctrl) {
            controller = ctrl;
            controller->get_CoreWebView2(&webview);
            webview->AddRef();
            InterlockedAdd(&flag, 1);
        }));
    if (hr != S_OK) {
        return false;
    }
    MSG msg = {};
    while ((InterlockedAdd(&flag, 0) == 0) && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // remove window frame and decorations
    auto style = GetWindowLong(hwnd, GWL_STYLE);
    style &= ~(WS_OVERLAPPEDWINDOW);
    SetWindowLong(hwnd, GWL_STYLE, style);

    ICoreWebView2Settings* settings = nullptr;
    hr = webview->get_Settings(&settings);
    if (hr == S_OK) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
    }

    Init("window.external={invoke:s=>window.chrome.webview.postMessage(s)}");
    return true;
}

void Webview2Wnd::OnBrowserMessage(const char* msg) {
    /*
    auto seq = json_parse(msg, "id", 0);
    auto name = json_parse(msg, "method", 0);
    auto args = json_parse(msg, "params", 0);
    if (bindings.find(name) == bindings.end()) {
      return;
    }
    auto fn = bindings[name];
    (*fn->first)(seq, args, fn->second);
    */
    log(msg);
}

HWND Webview2Wnd::Create(const CreateCustomArgs& args) {
    CrashIf(!dataDir);
    CreateCustom(args);
    if (!hwnd) {
        return nullptr;
    }

    auto cb = std::bind(&Webview2Wnd::OnBrowserMessage, this, std::placeholders::_1);
    Embed(cb);
    UpdateWebviewSize();
    return hwnd;
}

LRESULT Webview2Wnd::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_SIZE) {
        UpdateWebviewSize();
    }
    return WndProcDefault(hwnd, msg, wparam, lparam);
}

Webview2Wnd::~Webview2Wnd() {
    str::Free(dataDir);
}

//--- TreeView

/*
- https://docs.microsoft.com/en-us/windows/win32/controls/tree-view-control-reference

Tree view, checkboxes and other info:
- https://devblogs.microsoft.com/oldnewthing/20171127-00/?p=97465
- https://devblogs.microsoft.com/oldnewthing/20171128-00/?p=97475
- https://devblogs.microsoft.com/oldnewthing/20171129-00/?p=97485
- https://devblogs.microsoft.com/oldnewthing/20171130-00/?p=97495
- https://devblogs.microsoft.com/oldnewthing/20171201-00/?p=97505
- https://devblogs.microsoft.com/oldnewthing/20171204-00/?p=97515
- https://devblogs.microsoft.com/oldnewthing/20171205-00/?p=97525
-
https://stackoverflow.com/questions/34161879/how-to-remove-checkboxes-on-specific-tree-view-items-with-the-tvs-checkboxes-sty
*/

Kind kindTreeView = "treeView";

TreeView::TreeView() {
    kind = kindTreeView;
}

TreeView::~TreeView() {
}

HWND TreeView::Create(const TreeViewCreateArgs& argsIn) {
    idealSize = {48, 120}; // arbitrary
    fullRowSelect = argsIn.fullRowSelect;

    CreateControlArgs args;
    args.className = WC_TREEVIEWW;
    args.parent = argsIn.parent;
    args.font = argsIn.font;
    args.style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    args.style |= TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS;
    args.style |= TVS_TRACKSELECT | TVS_NOHSCROLL | TVS_INFOTIP;
    args.exStyle = argsIn.exStyle | TVS_EX_DOUBLEBUFFER;

    if (fullRowSelect) {
        args.exStyle |= TVS_FULLROWSELECT;
        args.exStyle &= ~TVS_HASLINES;
    }

    Wnd::CreateControl(args);

    if (IsWindowsVistaOrGreater()) {
        SendMessageW(hwnd, TVM_SETEXTENDEDSTYLE, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    }
    if (DynSetWindowTheme) {
        DynSetWindowTheme(hwnd, L"Explorer", nullptr);
    }

    TreeView_SetUnicodeFormat(hwnd, true);

    SetToolTipsDelayTime(TTDT_AUTOPOP, 32767);

    // TODO:
    // must be done at the end. Doing  SetWindowStyle() sends bogus (?)
    // TVN_ITEMCHANGED notification. As an alternative we could ignore TVN_ITEMCHANGED
    // if hItem doesn't point to an TreeItem
    // Subclass();

    return hwnd;
}

Size TreeView::GetIdealSize() {
    return {idealSize.dx, idealSize.dy};
}

void TreeView::SetToolTipsDelayTime(int type, int timeInMs) {
    CrashIf(!IsValidDelayType(type));
    CrashIf(timeInMs < 0);
    CrashIf(timeInMs > 32767); // TODO: or is it 65535?
    HWND hwndToolTips = GetToolTipsHwnd();
    SendMessageW(hwndToolTips, TTM_SETDELAYTIME, type, (LPARAM)timeInMs);
}

// https://docs.microsoft.com/en-us/windows/win32/controls/tvm-gettooltips
HWND TreeView::GetToolTipsHwnd() {
    return TreeView_GetToolTips(hwnd);
}

HTREEITEM TreeView::GetHandleByTreeItem(TreeItem item) {
    return treeModel->GetHandle(item);
}

// the result only valid until the next GetItem call
static TVITEMW* GetTVITEM(TreeView* tree, HTREEITEM hItem) {
    TVITEMW* ti = &tree->item;
    ZeroStruct(ti);
    ti->hItem = hItem;
    // https: // docs.microsoft.com/en-us/windows/win32/api/commctrl/ns-commctrl-tvitemexa
    ti->mask = TVIF_HANDLE | TVIF_PARAM | TVIF_STATE | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    ti->stateMask = TVIS_SELECTED | TVIS_CUT | TVIS_DROPHILITED | TVIS_BOLD | TVIS_EXPANDED | TVIS_STATEIMAGEMASK;
    BOOL ok = TreeView_GetItem(tree->hwnd, ti);
    if (!ok) {
        return nullptr;
    }
    return ti;
}

TVITEMW* GetTVITEM(TreeView* tree, TreeItem ti) {
    HTREEITEM hi = tree->GetHandleByTreeItem(ti);
    return GetTVITEM(tree, hi);
}

// expand if collapse, collapse if expanded
void TreeViewToggle(TreeView* tree, HTREEITEM hItem, bool recursive) {
    HWND hTree = tree->hwnd;
    HTREEITEM child = TreeView_GetChild(hTree, hItem);
    if (!child) {
        // only applies to nodes with children
        return;
    }

    TVITEMW* item = GetTVITEM(tree, hItem);
    if (!item) {
        return;
    }
    uint flag = TVE_EXPAND;
    bool isExpanded = bitmask::IsSet(item->state, TVIS_EXPANDED);
    if (isExpanded) {
        flag = TVE_COLLAPSE;
    }
    if (recursive) {
        TreeViewExpandRecursively(hTree, hItem, flag, false);
    } else {
        TreeView_Expand(hTree, hItem, flag);
    }
}

void SetTreeItemState(uint uState, TreeItemState& state) {
    state.isExpanded = bitmask::IsSet(uState, TVIS_EXPANDED);
    state.isSelected = bitmask::IsSet(uState, TVIS_SELECTED);
    uint n = (uState >> 12) - 1;
    state.isChecked = n != 0;
}

static bool HandleKey(TreeView* tree, WPARAM wp) {
    HWND hwnd = tree->hwnd;
    // consistently expand/collapse whole (sub)trees
    if (VK_MULTIPLY == wp) {
        if (IsShiftPressed()) {
            TreeViewExpandRecursively(hwnd, TreeView_GetRoot(hwnd), TVE_EXPAND, false);
        } else {
            TreeViewExpandRecursively(hwnd, TreeView_GetSelection(hwnd), TVE_EXPAND, true);
        }
    } else if (VK_DIVIDE == wp) {
        if (IsShiftPressed()) {
            HTREEITEM root = TreeView_GetRoot(hwnd);
            if (!TreeView_GetNextSibling(hwnd, root)) {
                root = TreeView_GetChild(hwnd, root);
            }
            TreeViewExpandRecursively(hwnd, root, TVE_COLLAPSE, false);
        } else {
            TreeViewExpandRecursively(hwnd, TreeView_GetSelection(hwnd), TVE_COLLAPSE, true);
        }
    } else if (wp == 13) {
        // this is Enter key
        bool recursive = IsShiftPressed();
        TreeViewToggle(tree, TreeView_GetSelection(hwnd), recursive);
    } else {
        return false;
    }
    TreeView_EnsureVisible(hwnd, TreeView_GetSelection(hwnd));
    return true;
}

LRESULT TreeView::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    LRESULT res;
    TreeView* w = this;

    if (WM_ERASEBKGND == msg) {
        return FALSE;
    }

    if (WM_RBUTTONDOWN == msg) {
        // this is needed to make right click trigger context menu
        // otherwise it gets turned into NM_CLICK and it somehow
        // blocks WM_RBUTTONUP, which is a trigger for WM_CONTEXTMENU
        res = DefWindowProcW(hwnd, msg, wparam, lparam);
        return res;
    }

    if (WM_KEYDOWN == msg) {
        if (HandleKey(w, wparam)) {
            return 0;
        }
    }

    res = WndProcDefault(hwnd, msg, wparam, lparam);
    return res;
}

bool TreeView::IsExpanded(TreeItem ti) {
    auto state = GetItemState(ti);
    return state.isExpanded;
}

// https://docs.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-treeview_getitemrect
bool TreeView::GetItemRect(TreeItem ti, bool justText, RECT& r) {
    HTREEITEM hi = GetHandleByTreeItem(ti);
    BOOL b = toBOOL(justText);
    BOOL ok = TreeView_GetItemRect(hwnd, hi, &r, b);
    return ok == TRUE;
}

TreeItem TreeView::GetSelection() {
    HTREEITEM hi = TreeView_GetSelection(hwnd);
    return GetTreeItemByHandle(hi);
}

bool TreeView::SelectItem(TreeItem ti) {
    HTREEITEM hi = nullptr;
    if (ti != TreeModel::kNullItem) {
        hi = GetHandleByTreeItem(ti);
    }
    BOOL ok = TreeView_SelectItem(hwnd, hi);
    return ok == TRUE;
}

void TreeView::SetBackgroundColor(COLORREF bgCol) {
    backgroundColor = bgCol;
    TreeView_SetBkColor(hwnd, bgCol);
}

void TreeView::SetTextColor(COLORREF col) {
#if 0 // TODO: do I need this?
    this->textColor = col;
#endif
    TreeView_SetTextColor(this->hwnd, col);
}

void TreeView::ExpandAll() {
    SuspendRedraw();
    auto root = TreeView_GetRoot(this->hwnd);
    TreeViewExpandRecursively(this->hwnd, root, TVE_EXPAND, false);
    ResumeRedraw();
}

void TreeView::CollapseAll() {
    SuspendRedraw();
    auto root = TreeView_GetRoot(this->hwnd);
    TreeViewExpandRecursively(this->hwnd, root, TVE_COLLAPSE, false);
    ResumeRedraw();
}

void TreeView::Clear() {
    treeModel = nullptr;

    HWND hwnd = this->hwnd;
    ::SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(hwnd);
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    uint flags = RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN;
    ::RedrawWindow(hwnd, nullptr, nullptr, flags);
}

char* TreeView::GetDefaultTooltipTemp(TreeItem ti) {
    auto hItem = GetHandleByTreeItem(ti);
    WCHAR buf[INFOTIPSIZE + 1]{}; // +1 just in case

    TVITEMW it{};
    it.hItem = hItem;
    it.mask = TVIF_TEXT;
    it.pszText = buf;
    it.cchTextMax = dimof(buf);
    TreeView_GetItem(hwnd, &it);

    return ToUtf8Temp(buf);
}

// get the item at a given (x,y) position in the window
TreeItem TreeView::GetItemAt(int x, int y) {
    TVHITTESTINFO ht{};
    ht.pt = {x, y};
    TreeView_HitTest(hwnd, &ht);
    return GetTreeItemByHandle(ht.hItem);
}

TreeItem TreeView::GetTreeItemByHandle(HTREEITEM item) {
    if (item == nullptr) {
        return TreeModel::kNullItem;
    }
    auto tvi = GetTVITEM(this, item);
    if (!tvi) {
        return TreeModel::kNullItem;
    }
    TreeItem res = (TreeItem)(tvi->lParam);
    return res;
}

static void FillTVITEM(TVITEMEXW* tvitem, TreeModel* tm, TreeItem ti) {
    uint mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
    tvitem->mask = mask;

    uint stateMask = TVIS_EXPANDED;
    uint state = 0;
    if (tm->IsExpanded(ti)) {
        state = TVIS_EXPANDED;
    }

    tvitem->state = state;
    tvitem->stateMask = stateMask;
    tvitem->lParam = static_cast<LPARAM>(ti);
    char* title = tm->Text(ti);
    tvitem->pszText = ToWstrTemp(title);
}

// inserting in front is faster:
// https://devblogs.microsoft.com/oldnewthing/20111125-00/?p=9033
HTREEITEM insertItemFront(TreeView* treeView, TreeItem ti, HTREEITEM parent) {
    TVINSERTSTRUCTW toInsert{};

    toInsert.hParent = parent;
    toInsert.hInsertAfter = TVI_FIRST;

    TVITEMEXW* tvitem = &toInsert.itemex;
    FillTVITEM(tvitem, treeView->treeModel, ti);
    HTREEITEM res = TreeView_InsertItem(treeView->hwnd, &toInsert);
    return res;
}

bool TreeView::UpdateItem(TreeItem ti) {
    HTREEITEM ht = GetHandleByTreeItem(ti);
    CrashIf(!ht);
    if (!ht) {
        return false;
    }

    TVITEMEXW tvitem;
    tvitem.hItem = ht;
    FillTVITEM(&tvitem, treeModel, ti);
    BOOL ok = TreeView_SetItem(hwnd, &tvitem);
    return ok != 0;
}

// complicated because it inserts items backwards, as described in
// https://devblogs.microsoft.com/oldnewthing/20111125-00/?p=9033
void PopulateTreeItem(TreeView* treeView, TreeItem item, HTREEITEM parent) {
    auto tm = treeView->treeModel;
    int n = tm->ChildCount(item);
    TreeItem tmp[256];
    TreeItem* a = &tmp[0];
    if (n > dimof(tmp)) {
        size_t nBytes = (size_t)n * sizeof(TreeItem);
        a = (TreeItem*)malloc(nBytes);
    }
    // ChildAt() is optimized for sequential access and we need to
    // insert backwards, so gather the items in v first
    for (int i = 0; i < n; i++) {
        auto ti = tm->ChildAt(item, i);
        CrashIf(ti == 0);
        a[n - 1 - i] = ti;
    }

    for (int i = 0; i < n; i++) {
        auto ti = a[i];
        HTREEITEM h = insertItemFront(treeView, ti, parent);
        tm->SetHandle(ti, h);
        // avoid recursing if not needed because we use a lot of stack space
        if (tm->ChildCount(ti) > 0) {
            PopulateTreeItem(treeView, ti, h);
        }
    }

    if (a != &tmp[0]) {
        free(a);
    }
}

static void PopulateTree(TreeView* treeView, TreeModel* tm) {
    TreeItem root = tm->Root();
    PopulateTreeItem(treeView, root, nullptr);
}

void TreeView::SetTreeModel(TreeModel* tm) {
    CrashIf(!tm);

    SuspendRedraw();

    TreeView_DeleteAllItems(hwnd);

    treeModel = tm;
    PopulateTree(this, tm);
    ResumeRedraw();

    uint flags = RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN;
    RedrawWindow(hwnd, nullptr, nullptr, flags);
}

void TreeView::SetCheckState(TreeItem item, bool enable) {
    HTREEITEM hi = GetHandleByTreeItem(item);
    CrashIf(!hi);
    TreeView_SetCheckState(hwnd, hi, enable);
}

bool TreeView::GetCheckState(TreeItem item) {
    HTREEITEM hi = GetHandleByTreeItem(item);
    CrashIf(!hi);
    auto res = TreeView_GetCheckState(hwnd, hi);
    return res != 0;
}

TreeItemState TreeView::GetItemState(TreeItem ti) {
    TreeItemState res;

    TVITEMW* it = GetTVITEM(this, ti);
    CrashIf(!it);
    if (!it) {
        return res;
    }
    SetTreeItemState(it->state, res);
    res.nChildren = it->cChildren;

    return res;
}

// if context menu invoked via keyboard, get selected item
// if via right-click, selects the item under the cursor
// in both cases can return null
// sets pt to screen position (for context menu coordinates)
TreeItem GetOrSelectTreeItemAtPos(ContextMenuEvent* args, POINT& pt) {
    TreeView* treeView = (TreeView*)args->w;
    TreeModel* tm = treeView->treeModel;
    HWND hwnd = treeView->hwnd;

    TreeItem ti;
    pt = {args->mouseWindow.x, args->mouseWindow.y};
    if (pt.x == -1 || pt.y == -1) {
        // no mouse position when launched via keyboard shortcut
        // use position of selected item to show menu
        ti = treeView->GetSelection();
        if (ti == TreeModel::kNullItem) {
            return TreeModel::kNullItem;
        }
        RECT rcItem;
        if (treeView->GetItemRect(ti, true, rcItem)) {
            // rcItem is local to window, map to global screen position
            MapWindowPoints(hwnd, HWND_DESKTOP, (POINT*)&rcItem, 2);
            pt.x = rcItem.left;
            pt.y = rcItem.bottom;
        }
    } else {
        ti = treeView->GetItemAt(pt.x, pt.y);
        if (ti == TreeModel::kNullItem) {
            // only show context menu if over a node in tree
            return TreeModel::kNullItem;
        }
        // context menu acts on this item so select it
        // for better visual feedback to the user
        treeView->SelectItem(ti);
        pt.x = args->mouseScreen.x;
        pt.y = args->mouseScreen.y;
    }
    return ti;
}

LRESULT TreeView::OnNotifyReflect(WPARAM wp, LPARAM lp) {
    TreeView* w = this;
    NMTREEVIEWW* nmtv = (NMTREEVIEWW*)(lp);
    LRESULT res;

    auto code = nmtv->hdr.code;
    // https://docs.microsoft.com/en-us/windows/win32/controls/tvn-getinfotip
    if (code == TVN_GETINFOTIP) {
        if (!onGetTooltip) {
            return 0;
        }
        TreeItemGetTooltipEvent ev;
        ev.treeView = w;
        ev.info = (NMTVGETINFOTIPW*)(nmtv);
        ev.treeItem = GetTreeItemByHandle(ev.info->hItem);
        onGetTooltip(&ev);
        return 0;
    }

    // https://docs.microsoft.com/en-us/windows/win32/controls/nm-customdraw-tree-view
    if (code == NM_CUSTOMDRAW) {
        if (!onTreeItemCustomDraw) {
            return CDRF_DODEFAULT;
        }
        TreeItemCustomDrawEvent ev;
        ev.treeView = w;
        ev.nm = (NMTVCUSTOMDRAW*)lp;
        HTREEITEM hItem = (HTREEITEM)ev.nm->nmcd.dwItemSpec;
        // it can be 0 in CDDS_PREPAINT state
        ev.treeItem = GetTreeItemByHandle(hItem);
        // TODO: seeing this in crash reports because GetTVITEM() returns nullptr
        // should log more info
        // SubmitBugReportIf(!a.treeItem);
        if (!ev.treeItem) {
            return CDRF_DODEFAULT;
        }
        res = onTreeItemCustomDraw(&ev);
        if (res < 0) {
            return CDRF_DODEFAULT;
        }
        return res;
    }

    // https://docs.microsoft.com/en-us/windows/win32/controls/tvn-selchanged
    if (code == TVN_SELCHANGED) {
        log("tv: TVN_SELCHANGED\n");
        if (!onTreeSelectionChanged) {
            return 0;
        }
        TreeSelectionChangedEvent ev;
        ev.treeView = w;
        ev.nmtv = nmtv;
        auto action = ev.nmtv->action;
        if (action == TVC_BYKEYBOARD) {
            ev.byKeyboard = true;
        } else if (action == TVC_BYMOUSE) {
            ev.byMouse = true;
        }
        ev.prevSelectedItem = w->GetTreeItemByHandle(nmtv->itemOld.hItem);
        ev.selectedItem = w->GetTreeItemByHandle(nmtv->itemNew.hItem);
        onTreeSelectionChanged(&ev);
        return 0;
    }

    // https://docs.microsoft.com/en-us/windows/win32/controls/nm-click-tree-view
    if (code == NM_CLICK || code == NM_DBLCLK) {
        log("tv: NM_CLICK\n");
        if (!onTreeClick) {
            return 0;
        }
        NMHDR* nmhdr = (NMHDR*)lp;
        TreeClickEvent ev{};
        ev.treeView = w;
        ev.isDblClick = (code == NM_DBLCLK);

        DWORD pos = GetMessagePos();
        ev.mouseScreen.x = GET_X_LPARAM(pos);
        ev.mouseScreen.y = GET_Y_LPARAM(pos);
        POINT pt{ev.mouseScreen.x, ev.mouseScreen.y};
        if (pt.x != -1) {
            MapWindowPoints(HWND_DESKTOP, nmhdr->hwndFrom, &pt, 1);
        }
        ev.mouseWindow.x = pt.x;
        ev.mouseWindow.y = pt.y;

        // determine which item has been clicked (if any)
        TVHITTESTINFO ht{};
        ht.pt.x = ev.mouseWindow.x;
        ht.pt.y = ev.mouseWindow.y;
        TreeView_HitTest(nmhdr->hwndFrom, &ht);
        if ((ht.flags & TVHT_ONITEM)) {
            ev.treeItem = GetTreeItemByHandle(ht.hItem);
        }
        res = onTreeClick(&ev);
        return 0;
    }

    // https://docs.microsoft.com/en-us/windows/win32/controls/tvn-keydown
    if (code == TVN_KEYDOWN) {
        if (!onTreeKeyDown) {
            return 0;
        }
        NMTVKEYDOWN* nmkd = (NMTVKEYDOWN*)nmtv;
        TreeKeyDownEvent ev{};
        ev.treeView = w;
        ev.nmkd = nmkd;
        ev.keyCode = nmkd->wVKey;
        ev.flags = nmkd->flags;
        onTreeKeyDown(&ev);
        return 0;
    }

    return 0;
}

//--- Tabs

Kind kindTabs = "tabs";

TabsCtrl::TabsCtrl() {
    kind = kindTabs;
}

TabsCtrl::~TabsCtrl() = default;

LRESULT TabsCtrl::OnNotifyReflect(WPARAM wp, LPARAM lp) {
    LPNMHDR hdr = (LPNMHDR)lp;
    if (hdr->code == TTN_GETDISPINFOA) {
        logf("TabsCtrl::OnNotifyReflec: TTN_GETDISPINFOA\n");
    } else if (hdr->code == TTN_GETDISPINFOW) {
        logf("TabsCtrl::OnNotifyReflec: TTN_GETDISPINFOW\n");
    }
    return 0;
}

HWND TabsCtrl::Create(TabsCreateArgs& argsIn) {
    createToolTipsHwnd = argsIn.createToolTipsHwnd;

    CreateControlArgs args;
    args.parent = argsIn.parent;
    args.font = argsIn.font;
    args.className = WC_TABCONTROLW;
    args.style = WS_CHILD | WS_CLIPSIBLINGS | TCS_FOCUSNEVER | TCS_FIXEDWIDTH | TCS_FORCELABELLEFT | WS_VISIBLE;
    if (createToolTipsHwnd) {
        args.style |= TCS_TOOLTIPS;
    }

    HWND hwnd = CreateControl(args);
    if (!hwnd) {
        return nullptr;
    }

    if (createToolTipsHwnd) {
        HWND ttHwnd = GetToolTipsHwnd();
        TOOLINFO ti{0};
        ti.cbSize = sizeof(ti);
        ti.hwnd = hwnd;
        ti.uId = 0;
        ti.uFlags = TTF_SUBCLASS;
        ti.lpszText = (WCHAR*)L"placeholder tooltip";
        SetRectEmpty(&ti.rect);
        RECT r = ti.rect;
        SendMessageW(ttHwnd, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }
    return hwnd;
}

Size TabsCtrl::GetIdealSize() {
    Size sz{32, 128};
    return sz;
}

int TabsCtrl::GetTabCount() {
    int n = TabCtrl_GetItemCount(hwnd);
    return n;
}

int TabsCtrl::InsertTab(int idx, const char* s) {
    CrashIf(idx < 0);

    TCITEMW item{0};
    item.mask = TCIF_TEXT;
    item.pszText = ToWstrTemp(s);
    int insertedIdx = TabCtrl_InsertItem(hwnd, idx, &item);
    tooltips.InsertAt(idx, "");
    return insertedIdx;
}

void TabsCtrl::RemoveTab(int idx) {
    CrashIf(idx < 0);
    CrashIf(idx >= GetTabCount());
    BOOL ok = TabCtrl_DeleteItem(hwnd, idx);
    CrashIf(!ok);
    tooltips.RemoveAt(idx);
}

void TabsCtrl::RemoveAllTabs() {
    TabCtrl_DeleteAllItems(hwnd);
    tooltips.Reset();
}

void TabsCtrl::SetTabText(int idx, const char* s) {
    CrashIf(idx < 0);
    CrashIf(idx >= GetTabCount());

    TCITEMW item{0};
    item.mask = TCIF_TEXT;
    item.pszText = ToWstrTemp(s);
    TabCtrl_SetItem(hwnd, idx, &item);
}

// result is valid until next call to GetTabText()
char* TabsCtrl::GetTabText(int idx) {
    CrashIf(idx < 0);
    CrashIf(idx >= GetTabCount());

    WCHAR buf[512]{};
    TCITEMW item{0};
    item.mask = TCIF_TEXT;
    item.pszText = buf;
    item.cchTextMax = dimof(buf) - 1; // -1 just in case
    TabCtrl_GetItem(hwnd, idx, &item);
    char* s = ToUtf8Temp(buf);
    lastTabText.Set(s);
    return lastTabText.Get();
}

int TabsCtrl::GetSelectedTabIndex() {
    int idx = TabCtrl_GetCurSel(hwnd);
    return idx;
}

int TabsCtrl::SetSelectedTabByIndex(int idx) {
    int prevSelectedIdx = TabCtrl_SetCurSel(hwnd, idx);
    return prevSelectedIdx;
}

void TabsCtrl::SetItemSize(Size sz) {
    TabCtrl_SetItemSize(hwnd, sz.dx, sz.dy);
}

void TabsCtrl::SetToolTipsHwnd(HWND hwndTooltip) {
    TabCtrl_SetToolTips(hwnd, hwndTooltip);
}

HWND TabsCtrl::GetToolTipsHwnd() {
    HWND res = TabCtrl_GetToolTips(hwnd);
    return res;
}

// TODO: this is a nasty implementation
// should probably TTM_ADDTOOL for each tab item
// we could re-calculate it in SetItemSize()
void TabsCtrl::MaybeUpdateTooltip() {
    // logf("MaybeUpdateTooltip() start\n");
    HWND ttHwnd = GetToolTipsHwnd();
    if (!ttHwnd) {
        return;
    }

    {
        TOOLINFO ti{0};
        ti.cbSize = sizeof(ti);
        ti.hwnd = hwnd;
        ti.uId = 0;
        SendMessage(ttHwnd, TTM_DELTOOL, 0, (LPARAM)&ti);
    }

    {
        TOOLINFO ti{0};
        ti.cbSize = sizeof(ti);
        ti.hwnd = hwnd;
        ti.uFlags = TTF_SUBCLASS;
        // ti.lpszText = LPSTR_TEXTCALLBACK;
        WCHAR* ws = ToWstrTemp(currTooltipText.Get());
        ti.lpszText = ws;
        ti.uId = 0;
        ti.rect = ClientRECT(hwnd);
        SendMessage(ttHwnd, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }
}

void TabsCtrl::MaybeUpdateTooltipText(int idx) {
    HWND ttHwnd = GetToolTipsHwnd();
    if (!ttHwnd) {
        return;
    }
    const char* tooltip = GetTooltip(idx);
    if (!tooltip) {
        // TODO: remove tooltip
        return;
    }
    currTooltipText.Set(tooltip);
#if 1
    MaybeUpdateTooltip();
#else
    // TODO: why this doesn't work?
    TOOLINFO ti{0};
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd;
    ti.uFlags = TTF_SUBCLASS;
    ti.lpszText = currTooltipText.Get();
    ti.uId = 0;
    ti.rect = ClientRECT(hwnd);
    SendMessage(ttHwnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
#endif
    // SendMessage(ttHwnd, TTM_UPDATE, 0, 0);

    SendMessage(ttHwnd, TTM_POP, 0, 0);
    SendMessage(ttHwnd, TTM_POPUP, 0, 0);
    // logf("MaybeUpdateTooltipText: %s\n", tooltip);
}

void TabsCtrl::SetTooltip(int idx, const char* s) {
    tooltips.SetAt(idx, s);
}

const char* TabsCtrl::GetTooltip(int idx) {
    if (idx >= tooltips.Size()) {
        return nullptr;
    }
    char* res = tooltips.at(idx);
    return res;
}

//--- misc code

int RunMessageLoop(HACCEL accelTable, HWND hwndDialog) {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (PreTranslateMessage(msg)) {
            continue;
        }
        if (TranslateAccelerator(msg.hwnd, accelTable, &msg)) {
            continue;
        }
        if (hwndDialog && IsDialogMessage(hwndDialog, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

#if 0
// TODO: support accelerator table?
// TODO: a better way to stop the loop e.g. via shared
// atomic int to signal termination and sending WM_IDLE
// to trigger processing of the loop
void RunModalWindow(HWND hwndDialog, HWND hwndParent) {
    if (hwndParent != nullptr) {
        EnableWindow(hwndParent, FALSE);
    }

    MSG msg;
    bool isFinished = false;
    while (!isFinished) {
        BOOL ok = WaitMessage();
        if (!ok) {
            DWORD err = GetLastError();
            LogLastError(err);
            isFinished = true;
            continue;
        }
        while (!isFinished && PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                isFinished = true;
                break;
            }
            if (!IsDialogMessage(hwndDialog, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    if (hwndParent != nullptr) {
        EnableWindow(hwndParent, TRUE);
    }
}
#endif

#if 0
// sets initial position of w within hwnd. Assumes w->initialSize is set.
void PositionCloseTo(Wnd* w, HWND hwnd) {
    CrashIf(!hwnd);
    Size is = w->initialSize;
    CrashIf(is.IsEmpty());
    RECT r{};
    BOOL ok = GetWindowRect(hwnd, &r);
    CrashIf(!ok);

    // position w in the the center of hwnd
    // if window is bigger than hwnd, let the system position
    // we don't want to hide it
    int offX = (RectDx(r) - is.dx) / 2;
    if (offX < 0) {
        return;
    }
    int offY = (RectDy(r) - is.dy) / 2;
    if (offY < 0) {
        return;
    }
    Point& ip = w->initialPos;
    ip.x = (int)r.left + (int)offX;
    ip.y = (int)r.top + (int)offY;
}
#endif

// http://www.guyswithtowels.com/blog/10-things-i-hate-about-win32.html#ModelessDialogs
// to implement a standard dialog navigation we need to call
// IsDialogMessage(hwnd) in message loop.
// hwnd has to be current top-level window that is modeless dialog
// we need to manually maintain this window
HWND g_currentModelessDialog = nullptr;

HWND GetCurrentModelessDialog() {
    return g_currentModelessDialog;
}

// set to nullptr to disable
void SetCurrentModelessDialog(HWND hwnd) {
    g_currentModelessDialog = hwnd;
}

// TODO: port from Window.cpp or figure out something better
#if 0
static LRESULT CALLBACK wndProcCustom(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // ...
    if (w->isDialog) {
        // TODO: should handle more messages as per
        // https://stackoverflow.com/questions/35688400/set-full-focus-on-a-button-setfocus-is-not-enough
        // and https://docs.microsoft.com/en-us/windows/win32/dlgbox/dlgbox-programming-considerations
        if (WM_ACTIVATE == msg) {
            if (wp == 0) {
                // becoming inactive
                SetCurrentModelessDialog(nullptr);
            } else {
                // becoming active
                SetCurrentModelessDialog(w->hwnd);
            }
        }
    }
}
#endif

void DeleteWnd(Static** wnd) {
    delete *wnd;
    *wnd = nullptr;
}

void DeleteWnd(Button** wnd) {
    delete *wnd;
    *wnd = nullptr;
}

void DeleteWnd(Edit** wnd) {
    delete *wnd;
    *wnd = nullptr;
}

void DeleteWnd(Checkbox** wnd) {
    delete *wnd;
    *wnd = nullptr;
}

void DeleteWnd(Progress** wnd) {
    delete *wnd;
    *wnd = nullptr;
}
