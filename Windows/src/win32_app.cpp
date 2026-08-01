#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include "room_engine.hpp"
#include "room_protocol.hpp"
#include "util.hpp"
#include "qrcodegen.hpp"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace {

inline HMENU control_id(int id) noexcept {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

constexpr UINT WM_ENGINE_UPDATE = WM_APP + 41;
constexpr int ID_COMPOSE = 1001;
constexpr int ID_SEND = 1002;
constexpr int ID_CONNECT = 1003;
constexpr int ID_CREATE_ROOM = 1004;
constexpr int ID_JOIN_ROOM = 1005;
constexpr int ID_SYNC = 1006;
constexpr int ID_INVITE = 1007;
constexpr int ID_REPLICA = 1008;
constexpr int ID_SETTINGS = 1009;
constexpr int ID_DEMO = 1010;
constexpr int ID_DIAGNOSTICS = 1011;
constexpr int ID_PROMPT_EDIT = 1101;

constexpr COLORREF C_BG = RGB(19, 19, 21);          // VeilWindow
constexpr COLORREF C_RAIL = RGB(19, 19, 21);        // VeilWindow
constexpr COLORREF C_CHANNEL = RGB(28, 28, 31);     // VeilPanel
constexpr COLORREF C_PANEL = RGB(24, 24, 27);       // between window and panel
constexpr COLORREF C_INPUT = RGB(22, 22, 24);       // VeilEdit
constexpr COLORREF C_TEXT = RGB(235, 235, 238);     // VeilText
constexpr COLORREF C_MUTED = RGB(165, 165, 172);    // VeilMuted
constexpr COLORREF C_FAINT = RGB(112, 112, 120);
constexpr COLORREF C_ACCENT = RGB(239, 35, 60);     // VeilRed
constexpr COLORREF C_ACCENT_DARK = RGB(145, 18, 35);// VeilRedDark
constexpr COLORREF C_GREEN = RGB(85, 194, 113);     // VeilSuccess
constexpr COLORREF C_RED = RGB(255, 107, 107);
constexpr COLORREF C_WARNING = RGB(255, 200, 87);   // VeilWarning
constexpr COLORREF C_BORDER = RGB(65, 65, 71);      // VeilBorder
constexpr COLORREF C_HOVER = RGB(47, 47, 52);
constexpr int IDI_VEILKNIT = 101;


std::mutex g_diagnostic_mutex;
std::filesystem::path g_diagnostic_path;

std::string diagnostic_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &value);
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

void diagnostic_log(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_diagnostic_mutex);
    if (g_diagnostic_path.empty()) {
        wchar_t local_app_data[MAX_PATH]{};
        DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
        std::filesystem::path folder = size ? std::filesystem::path(local_app_data) / L"VeilKnit" / L"Rooms" / L"logs"
                                            : std::filesystem::current_path() / L"logs";
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        g_diagnostic_path = folder / L"rooms-running.log";
    }
    std::ofstream output(g_diagnostic_path, std::ios::app | std::ios::binary);
    if (output) {
        output << '[' << diagnostic_timestamp() << "] " << message << "\r\n";
        output.flush();
    }
}

void show_diagnostic_location(HWND owner) {
    diagnostic_log("User requested diagnostic log information");
    std::wstring message = L"Crash-safe logging is active and flushes every operation to:\n\n" +
                           g_diagnostic_path.wstring() +
                           L"\n\nThis file can be copied while Rooms is running and remains available after a freeze or crash.";
    MessageBoxW(owner, message.c_str(), L"VeilKnit Rooms diagnostics", MB_OK | MB_ICONINFORMATION);
}

const char* connection_state_name(vkrooms::ConnectionState state) {
    switch (state) {
        case vkrooms::ConnectionState::disconnected: return "disconnected";
        case vkrooms::ConnectionState::connecting: return "connecting";
        case vkrooms::ConnectionState::authorizing: return "authorizing";
        case vkrooms::ConnectionState::connected: return "connected";
        case vkrooms::ConnectionState::error: return "error";
    }
    return "unknown";
}

struct PromptState {
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND edit = nullptr;
    std::wstring title;
    std::wstring label;
    std::wstring initial;
    bool multiline = false;
    bool done = false;
    bool accepted = false;
    std::wstring result;
};

LRESULT CALLBACK PromptProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<PromptState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->window = hwnd;
    }
    if (!state) return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
        case WM_ERASEBKGND: {
            RECT rect{}; GetClientRect(hwnd, &rect);
            static HBRUSH brush = CreateSolidBrush(C_PANEL);
            FillRect(reinterpret_cast<HDC>(wparam), &rect, brush);
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            auto dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, C_TEXT); SetBkColor(dc, C_PANEL);
            static HBRUSH brush = CreateSolidBrush(C_PANEL);
            return reinterpret_cast<LRESULT>(brush);
        }
        case WM_CTLCOLOREDIT: {
            auto dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, C_TEXT); SetBkColor(dc, C_INPUT);
            static HBRUSH brush = CreateSolidBrush(C_INPUT);
            return reinterpret_cast<LRESULT>(brush);
        }
        case WM_CREATE: {
            BOOL dark = TRUE; DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
            CreateWindowExW(0, L"STATIC", state->label.c_str(), WS_CHILD | WS_VISIBLE,
                            18, 16, 484, 32, hwnd, nullptr, nullptr, nullptr);
            const DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
                (state->multiline ? (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN) : ES_AUTOHSCROLL);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->initial.c_str(), edit_style,
                                          18, 52, 484, state->multiline ? 176 : 30,
                                          hwnd, control_id(ID_PROMPT_EDIT), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                            334, state->multiline ? 240 : 96, 78, 30, hwnd, control_id(IDOK), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            424, state->multiline ? 240 : 96, 78, 30, hwnd, control_id(IDCANCEL), nullptr, nullptr);
            SetWindowTheme(state->edit, L"DarkMode_Explorer", nullptr);
            SendMessageW(state->edit, EM_SETSEL, 0, -1);
            SetFocus(state->edit);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) {
                state->accepted = LOWORD(wparam) == IDOK;
                if (state->accepted) {
                    const int length = GetWindowTextLengthW(state->edit);
                    state->result.resize(static_cast<std::size_t>(length) + 1);
                    GetWindowTextW(state->edit, state->result.data(), length + 1);
                    state->result.resize(static_cast<std::size_t>(length));
                }
                state->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

std::optional<std::wstring> prompt_text(HWND owner, const wchar_t* title, const wchar_t* label,
                                        const std::wstring& initial = {}, bool multiline = false) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = PromptProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(C_PANEL);
        wc.lpszClassName = L"VeilKnitRoomsPrompt";
        RegisterClassExW(&wc);
        registered = true;
    }
    PromptState state;
    state.owner = owner; state.title = title; state.label = label; state.initial = initial; state.multiline = multiline;
    EnableWindow(owner, FALSE);
    RECT owner_rect{}; GetWindowRect(owner, &owner_rect);
    const int width = 536; const int height = multiline ? 330 : 190;
    const int x = owner_rect.left + std::max(0L, (owner_rect.right - owner_rect.left - width) / 2);
    const int y = owner_rect.top + std::max(0L, (owner_rect.bottom - owner_rect.top - height) / 2);
    CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"VeilKnitRoomsPrompt", title,
                    WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
                    x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &state);
    MSG message{};
    while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(state.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return state.accepted ? std::optional<std::wstring>(state.result) : std::nullopt;
}

void set_clipboard_text(HWND owner, const std::wstring& text);


struct InviteQrState {
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND invite_edit = nullptr;
    std::wstring invite_text;
    std::vector<unsigned char> modules;
    int module_count = 0;
    bool done = false;
};

constexpr int ID_INVITE_QR_COPY = 1201;
constexpr int ID_INVITE_QR_CLOSE = 1202;
constexpr int ID_INVITE_QR_TEXT = 1203;

LRESULT CALLBACK InviteQrProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<InviteQrState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<InviteQrState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->window = hwnd;
    }
    if (!state) return DefWindowProcW(hwnd, message, wparam, lparam);

    switch (message) {
        case WM_CREATE: {
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
            CreateWindowExW(0, L"STATIC", L"Scan this code on another device, or copy the invitation below.",
                            WS_CHILD | WS_VISIBLE, 24, 20, 432, 24, hwnd, nullptr, nullptr, nullptr);
            state->invite_edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", state->invite_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                24, 430, 432, 82, hwnd, control_id(ID_INVITE_QR_TEXT), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Copy invite", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                            270, 526, 90, 32, hwnd, control_id(ID_INVITE_QR_COPY), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            370, 526, 86, 32, hwnd, control_id(ID_INVITE_QR_CLOSE), nullptr, nullptr);
            SetWindowTheme(state->invite_edit, L"DarkMode_Explorer", nullptr);
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rect{}; GetClientRect(hwnd, &rect);
            HBRUSH brush = CreateSolidBrush(C_PANEL);
            FillRect(reinterpret_cast<HDC>(wparam), &rect, brush);
            DeleteObject(brush);
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            auto dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, C_TEXT); SetBkColor(dc, C_PANEL);
            static HBRUSH brush = CreateSolidBrush(C_PANEL);
            return reinterpret_cast<LRESULT>(brush);
        }
        case WM_CTLCOLOREDIT: {
            auto dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, C_TEXT); SetBkColor(dc, C_INPUT);
            static HBRUSH brush = CreateSolidBrush(C_INPUT);
            return reinterpret_cast<LRESULT>(brush);
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT qr_area{56, 56, 424, 424};
            HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(dc, &qr_area, white);
            DeleteObject(white);
            if (state->module_count > 0) {
                constexpr int quiet_modules = 4;
                const int all_modules = state->module_count + quiet_modules * 2;
                const int qr_width = static_cast<int>(qr_area.right - qr_area.left);
                const int qr_height = static_cast<int>(qr_area.bottom - qr_area.top);
                const int pixel_size = std::max(1, std::min(qr_width, qr_height) / all_modules);
                const int rendered = pixel_size * all_modules;
                const int left = qr_area.left + ((qr_area.right - qr_area.left) - rendered) / 2;
                const int top = qr_area.top + ((qr_area.bottom - qr_area.top) - rendered) / 2;
                HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
                for (int y = 0; y < state->module_count; ++y) {
                    for (int x = 0; x < state->module_count; ++x) {
                        const auto index = static_cast<std::size_t>(y * state->module_count + x);
                        if (index >= state->modules.size() || state->modules[index] == 0) continue;
                        RECT module{
                            left + (x + quiet_modules) * pixel_size,
                            top + (y + quiet_modules) * pixel_size,
                            left + (x + quiet_modules + 1) * pixel_size,
                            top + (y + quiet_modules + 1) * pixel_size,
                        };
                        FillRect(dc, &module, black);
                    }
                }
                DeleteObject(black);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == ID_INVITE_QR_COPY) {
                set_clipboard_text(hwnd, state->invite_text);
                return 0;
            }
            if (LOWORD(wparam) == ID_INVITE_QR_CLOSE || LOWORD(wparam) == IDCANCEL) {
                state->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void show_invite_qr(HWND owner, const std::string& invite) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = InviteQrProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(C_PANEL);
        wc.lpszClassName = L"VeilKnitRoomsInviteQr";
        RegisterClassExW(&wc);
        registered = true;
    }

    InviteQrState state;
    state.owner = owner;
    state.invite_text = vkrooms::utf8_to_wide(invite);
    try {
        const auto qr = qrcodegen::QrCode::encodeText(invite.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        state.module_count = qr.getSize();
        state.modules.resize(static_cast<std::size_t>(state.module_count * state.module_count));
        for (int y = 0; y < state.module_count; ++y) {
            for (int x = 0; x < state.module_count; ++x) {
                state.modules[static_cast<std::size_t>(y * state.module_count + x)] = qr.getModule(x, y) ? 1 : 0;
            }
        }
    } catch (const std::exception& error) {
        MessageBoxW(owner, vkrooms::utf8_to_wide(std::string("Could not create the QR code: ") + error.what()).c_str(),
                    L"Invite QR unavailable", MB_ICONERROR);
        return;
    }

    EnableWindow(owner, FALSE);
    RECT owner_rect{}; GetWindowRect(owner, &owner_rect);
    constexpr int width = 496;
    constexpr int height = 610;
    const int x = owner_rect.left + std::max(0L, (owner_rect.right - owner_rect.left - width) / 2);
    const int y = owner_rect.top + std::max(0L, (owner_rect.bottom - owner_rect.top - height) / 2);
    CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"VeilKnitRoomsInviteQr", L"Share room invitation",
                    WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
                    x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &state);
    MSG message{};
    while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(state.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

void set_clipboard_text(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        std::memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
}

void fill_rect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void text_color(HDC dc, COLORREF color) { SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT); }

std::wstring initials(const std::string& name) {
    std::wstring result;
    bool take = true;
    for (const wchar_t c : vkrooms::utf8_to_wide(name)) {
        if (take && iswalnum(c)) { result.push_back(static_cast<wchar_t>(towupper(c))); if (result.size() == 2) break; take = false; }
        if (iswspace(c) || c == L'-') take = true;
    }
    if (result.empty()) result = L"?";
    return result;
}

struct HitItem { RECT rect{}; int index = -1; std::string id; };

class MainWindow {
public:
    explicit MainWindow(HWND hwnd) : hwnd_(hwnd), engine_([hwnd] { PostMessageW(hwnd, WM_ENGINE_UPDATE, 0, 0); }, diagnostic_log) {
        diagnostic_log("VeilKnit Rooms started");
        logo_icon_ = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_VEILKNIT),
                                                  IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
        engine_.start();
        create_controls();
        create_fonts();
        engine_.connect_async();
    }
    ~MainWindow() { diagnostic_log("VeilKnit Rooms shutting down"); engine_.stop(); destroy_fonts(); if (logo_icon_) DestroyIcon(logo_icon_); if (input_brush_) DeleteObject(input_brush_); if (panel_brush_) DeleteObject(panel_brush_); }

    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case WM_SIZE: layout(); return 0;
            case WM_GETMINMAXINFO: { auto* info = reinterpret_cast<MINMAXINFO*>(lparam); info->ptMinTrackSize.x = 1050; info->ptMinTrackSize.y = 680; return 0; }
            case WM_CTLCOLOREDIT: { auto dc = reinterpret_cast<HDC>(wparam); SetTextColor(dc, C_TEXT); SetBkColor(dc, C_INPUT); return reinterpret_cast<LRESULT>(input_brush_); }
            case WM_CTLCOLORSTATIC: { auto dc = reinterpret_cast<HDC>(wparam); SetTextColor(dc, C_TEXT); SetBkMode(dc, TRANSPARENT); return reinterpret_cast<LRESULT>(panel_brush_); }
            case WM_PAINT: paint(); return 0;
            case WM_ERASEBKGND: return 1;
            case WM_COMMAND: {
                const int id = LOWORD(wparam);
                const int notification = HIWORD(wparam);
                const HWND source = reinterpret_cast<HWND>(lparam);

                // EDIT controls send WM_COMMAND notifications for focus and
                // every text change. Treating those notifications like button
                // clicks disabled the compose box for five seconds after each
                // keystroke, which made it appear impossible to type.
                if (id == ID_COMPOSE) return 0;
                if (source != nullptr && notification != BN_CLICKED) return 0;

                command(id);
                return 0;
            }
            case WM_TIMER: {
                HWND control = GetDlgItem(hwnd_, static_cast<int>(wparam));
                if (control) EnableWindow(control, TRUE);
                KillTimer(hwnd_, static_cast<UINT_PTR>(wparam));
                update_control_labels();
                return 0;
            }
            case WM_DRAWITEM: return draw_button(reinterpret_cast<DRAWITEMSTRUCT*>(lparam)) ? TRUE : FALSE;
            case WM_LBUTTONDOWN: click(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), false); return 0;
            case WM_RBUTTONDOWN: click(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), true); return 0;
            case WM_MOUSEWHEEL:
                chat_scroll_ = std::max(0, chat_scroll_ + (GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? 100 : -100));
                InvalidateRect(hwnd_, nullptr, FALSE); return 0;
            case WM_ENGINE_UPDATE: {
                const auto state = engine_.snapshot();
                std::size_t message_count = 0;
                std::size_t member_count = 0;
                std::string room_id;
                if (state.selected_room >= 0 && state.selected_room < static_cast<int>(state.rooms.size())) {
                    const auto& room = state.rooms[static_cast<std::size_t>(state.selected_room)];
                    message_count = room.messages.size();
                    member_count = room.members.size();
                    room_id = room.room_id.substr(0, std::min<std::size_t>(8, room.room_id.size()));
                }
                if (state.status != last_logged_status_ ||
                    state.connection != last_logged_connection_ ||
                    message_count != last_logged_message_count_ ||
                    member_count != last_logged_member_count_) {
                    diagnostic_log(
                        std::string("State: connection=") + connection_state_name(state.connection) +
                        " status=\"" + state.status + "\" room=" + (room_id.empty() ? "none" : room_id) +
                        " members=" + std::to_string(member_count) +
                        " messages=" + std::to_string(message_count));
                    last_logged_status_ = state.status;
                    last_logged_connection_ = state.connection;
                    last_logged_message_count_ = message_count;
                    last_logged_member_count_ = member_count;
                }
                chat_scroll_ = 0;
                update_control_labels();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            case WM_CLOSE: DestroyWindow(hwnd_); return 0;
            case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd_, message, wparam, lparam);
    }

    void send_compose() {
        const int length = GetWindowTextLengthW(compose_);
        if (length <= 0) return;
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(compose_, text.data(), length + 1);
        text.resize(static_cast<std::size_t>(length));
        SetWindowTextW(compose_, L"");
        SetFocus(compose_);
        diagnostic_log("Submitting chat text (content omitted)");
        engine_.submit_text(vkrooms::wide_to_utf8(text));
    }

private:
    bool draw_button(const DRAWITEMSTRUCT* item) {
        if (!item || item->CtlType != ODT_BUTTON) return false;
        const auto state = engine_.snapshot();
        COLORREF background = C_PANEL;
        COLORREF border = C_BORDER;
        COLORREF foreground = C_TEXT;
        if (item->CtlID == ID_SEND) {
            background = C_ACCENT;
            border = C_ACCENT_DARK;
        } else if (item->CtlID == ID_CONNECT && state.connection == vkrooms::ConnectionState::connected) {
            background = RGB(33, 92, 51);
            border = C_GREEN;
        }
        if ((item->itemState & ODS_SELECTED) != 0) background = C_ACCENT_DARK;
        if ((item->itemState & ODS_DISABLED) != 0) { background = C_INPUT; foreground = C_FAINT; }

        HBRUSH brush = CreateSolidBrush(background);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        auto old_brush = SelectObject(item->hDC, brush);
        auto old_pen = SelectObject(item->hDC, pen);
        RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right, item->rcItem.bottom, 7, 7);
        SelectObject(item->hDC, old_brush); SelectObject(item->hDC, old_pen);
        DeleteObject(brush); DeleteObject(pen);

        wchar_t text[128]{};
        GetWindowTextW(item->hwndItem, text, 128);
        SetTextColor(item->hDC, foreground); SetBkMode(item->hDC, TRANSPARENT);
        SelectObject(item->hDC, item->CtlID == ID_SEND ? font_bold_ : font_);
        RECT text_rect = item->rcItem;
        DrawTextW(item->hDC, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if ((item->itemState & ODS_FOCUS) != 0) {
            RECT focus = item->rcItem; InflateRect(&focus, -3, -3); DrawFocusRect(item->hDC, &focus);
        }
        return true;
    }

    void create_controls() {
        compose_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                   0,0,100,40, hwnd_, control_id(ID_COMPOSE), nullptr, nullptr);
        send_ = CreateWindowExW(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                0,0,80,40, hwnd_, control_id(ID_SEND), nullptr, nullptr);
        const struct { int id; const wchar_t* text; } buttons[] = {
            {ID_CONNECT,L"Connect"},{ID_CREATE_ROOM,L"+ Room"},{ID_JOIN_ROOM,L"Join"},{ID_SYNC,L"Sync"},
            {ID_INVITE,L"Share Invite"},{ID_REPLICA,L"Replica"},{ID_SETTINGS,L"Room Policy"},{ID_DIAGNOSTICS,L"Log"},{ID_DEMO,L"Demo"}
        };
        int x = 310;
        for (const auto& button : buttons) {
            HWND handle = CreateWindowExW(0, L"BUTTON", button.text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                           x, 10, button.id == ID_INVITE || button.id == ID_SETTINGS ? 100 : 74, 30,
                                           hwnd_, control_id(button.id), nullptr, nullptr);
            toolbar_.push_back(handle);
            x += button.id == ID_INVITE || button.id == ID_SETTINGS ? 108 : 82;
        }
        SetWindowLongPtrW(compose_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        old_edit_proc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(compose_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&MainWindow::ComposeProc)));
        input_brush_ = CreateSolidBrush(C_INPUT);
        panel_brush_ = CreateSolidBrush(C_PANEL);
        SetWindowTheme(compose_, L"DarkMode_Explorer", nullptr);
        SetWindowTheme(send_, L"DarkMode_Explorer", nullptr);
        for (auto button : toolbar_) SetWindowTheme(button, L"DarkMode_Explorer", nullptr);
    }

    static LRESULT CALLBACK ComposeProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && message == WM_KEYDOWN && wparam == VK_RETURN && (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
            self->send_compose(); return 0;
        }
        return CallWindowProcW(self ? self->old_edit_proc_ : DefWindowProcW, hwnd, message, wparam, lparam);
    }

    void create_fonts() {
        const int dpi = GetDpiForWindow(hwnd_);
        auto make = [dpi](int points, int weight) {
            return CreateFontW(-MulDiv(points, dpi, 72), 0,0,0, weight, FALSE,FALSE,FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        };
        font_ = make(10, FW_NORMAL); font_bold_ = make(10, FW_SEMIBOLD); font_title_ = make(14, FW_BOLD); font_small_ = make(8, FW_NORMAL);
        SendMessageW(compose_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(send_, WM_SETFONT, reinterpret_cast<WPARAM>(font_bold_), TRUE);
        for (auto button : toolbar_) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    void destroy_fonts() { DeleteObject(font_); DeleteObject(font_bold_); DeleteObject(font_title_); DeleteObject(font_small_); }

    void layout() {
        RECT client{}; GetClientRect(hwnd_, &client);
        const int rail = 72, channels = 230, members = 250, top = 86, bottom = 72;
        const int center_left = rail + channels;
        const int center_right = std::max<int>(center_left + 280, static_cast<int>(client.right) - members);
        SetWindowPos(compose_, nullptr, center_left + 16, client.bottom - bottom + 12,
                     std::max(100, center_right - center_left - 112), 46, SWP_NOZORDER);
        SetWindowPos(send_, nullptr, center_right - 88, client.bottom - bottom + 12, 72, 46, SWP_NOZORDER);
        int x = center_left + 8;
        for (auto button : toolbar_) {
            RECT r{}; GetWindowRect(button, &r); const int width = r.right-r.left;
            SetWindowPos(button, nullptr, x, 10, width, 31, SWP_NOZORDER); x += width + 7;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void update_control_labels() {
        const auto state = engine_.snapshot();
        SetWindowTextW(toolbar_[0], state.connection == vkrooms::ConnectionState::connected ? L"Connected" : L"Connect");
        SetWindowTextW(toolbar_[1], state.room_creation_in_progress ? L"Creating..." : L"+ Room");
        EnableWindow(toolbar_[1], state.room_creation_in_progress ? FALSE : TRUE);
        const bool has_room = state.selected_room >= 0 && state.selected_room < static_cast<int>(state.rooms.size());
        const bool room_active = has_room && !state.rooms[static_cast<std::size_t>(state.selected_room)].suspended;
        EnableWindow(compose_, room_active && !show_log_ ? TRUE : FALSE);
        EnableWindow(send_, room_active && !show_log_ ? TRUE : FALSE);
        for (std::size_t index = 3; index <= 6 && index < toolbar_.size(); ++index) {
            EnableWindow(toolbar_[index], room_active ? TRUE : FALSE);
        }
        if (state.selected_room >= 0 && state.selected_room < static_cast<int>(state.rooms.size())) {
            SetWindowTextW(toolbar_[5], state.rooms[static_cast<std::size_t>(state.selected_room)].local_replica ? L"Stop Replica" : L"Replica");
        }
    }

    void command(int id) {
        diagnostic_log("Command " + std::to_string(id));
        if (id != ID_SEND && id != ID_DIAGNOSTICS) { HWND control = GetDlgItem(hwnd_, id); if (control) { EnableWindow(control, FALSE); SetTimer(hwnd_, static_cast<UINT_PTR>(id), 5000, nullptr); } }
        switch (id) {
            case ID_SEND: send_compose(); break;
            case ID_CONNECT: engine_.connect_async(); break;
            case ID_DEMO: engine_.start_demo_mode(); break;
            case ID_CREATE_ROOM: {
                const auto state = engine_.snapshot();
                if (state.room_creation_in_progress) break;
                const auto value = prompt_text(hwnd_, L"Create Room", L"Room name:", L"Model Airplanes");
                if (value) engine_.create_room(vkrooms::wide_to_utf8(*value));
                break;
            }
            case ID_JOIN_ROOM: {
                const auto value = prompt_text(hwnd_, L"Join Room", L"Paste a VKROOM1 invite code:", {}, true);
                if (value) engine_.join_room(vkrooms::wide_to_utf8(*value));
                break;
            }
            case ID_SYNC: engine_.sync_selected_room(); break;
            case ID_INVITE: {
                const auto invite = engine_.selected_invite_code();
                if (invite.empty()) MessageBoxW(hwnd_, L"This room does not have a published owner store yet.", L"Invite unavailable", MB_ICONINFORMATION);
                else show_invite_qr(hwnd_, invite);
                break;
            }
            case ID_REPLICA: engine_.toggle_replica(); break;
            case ID_SETTINGS: edit_room_policy(); break;
            case ID_DIAGNOSTICS: show_diagnostic_location(hwnd_); break;
        }
    }

    void edit_room_policy() {
        const auto state = engine_.snapshot();
        if (state.selected_room < 0 || state.selected_room >= static_cast<int>(state.rooms.size())) return;
        std::wstring initial;
        for (const auto& phrase : state.rooms[static_cast<std::size_t>(state.selected_room)].banned_phrases) {
            initial += vkrooms::utf8_to_wide(phrase); initial += L"\r\n";
        }
        const auto value = prompt_text(hwnd_, L"Room Phrase Policy", L"Blocked phrases, one per line:", initial, true);
        if (!value) return;
        std::vector<std::string> phrases;
        std::wstringstream stream(*value); std::wstring line;
        while (std::getline(stream, line)) { auto text = vkrooms::trim(vkrooms::wide_to_utf8(line)); if (!text.empty()) phrases.push_back(text); }
        engine_.set_banned_phrases(std::move(phrases));
    }

    void paint() {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd_, &ps);
        RECT client{}; GetClientRect(hwnd_, &client);
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
        HGDIOBJ old_bitmap = SelectObject(memory, bitmap);
        paint_to(memory, client);
        BitBlt(dc, 0,0,client.right,client.bottom,memory,0,0,SRCCOPY);
        SelectObject(memory, old_bitmap); DeleteObject(bitmap); DeleteDC(memory); EndPaint(hwnd_, &ps);
    }

    void paint_to(HDC dc, const RECT& client) {
        const int rail = 72, channels = 230, members = 250, top = 86, bottom = 72;
        const int center_left = rail + channels;
        const int center_right = std::max<int>(center_left + 280, static_cast<int>(client.right) - members);
        fill_rect(dc, client, C_BG);
        fill_rect(dc, RECT{0,0,rail,client.bottom}, C_RAIL);
        fill_rect(dc, RECT{rail,0,center_left,client.bottom}, C_CHANNEL);
        fill_rect(dc, RECT{center_right,0,client.right,client.bottom}, C_PANEL);
        fill_rect(dc, RECT{center_left,0,center_right,top}, C_PANEL);
        fill_rect(dc, RECT{center_left,client.bottom-bottom,center_right,client.bottom}, C_CHANNEL);

        const auto state = engine_.snapshot();
        draw_rooms(dc, state, rail);
        draw_channels(dc, state, rail, center_left, client.bottom);
        draw_header(dc, state, center_left, center_right);
        draw_messages(dc, state, RECT{center_left,top,center_right,client.bottom-bottom});
        draw_members(dc, state, RECT{center_right,top,client.right,client.bottom});
    }

    void draw_rooms(HDC dc, const vkrooms::AppSnapshot& state, int rail) {
        room_hits_.clear();
        if (logo_icon_) DrawIconEx(dc, 12, 10, logo_icon_, 48, 48, 0, nullptr, DI_NORMAL);
        HPEN separator = CreatePen(PS_SOLID, 1, C_BORDER);
        auto old_separator = SelectObject(dc, separator);
        MoveToEx(dc, 12, 66, nullptr); LineTo(dc, rail - 12, 66);
        SelectObject(dc, old_separator); DeleteObject(separator);
        int y = 78;
        SelectObject(dc, font_bold_);
        for (std::size_t i = 0; i < state.rooms.size(); ++i) {
            const bool selected = static_cast<int>(i) == state.selected_room;
            const bool suspended = state.rooms[i].suspended;
            RECT marker{4,y+8,8,y+44}; if (selected) fill_rect(dc, marker, C_TEXT);
            RECT circle{14,y,58,y+44};
            HBRUSH brush = CreateSolidBrush(suspended ? RGB(91, 31, 37) : (selected ? C_ACCENT : C_HOVER));
            HBRUSH old = static_cast<HBRUSH>(SelectObject(dc, brush));
            HPEN pen = CreatePen(suspended ? PS_SOLID : PS_NULL, suspended ? 2 : 0, suspended ? C_RED : 0);
            HPEN old_pen = static_cast<HPEN>(SelectObject(dc,pen));
            RoundRect(dc,circle.left,circle.top,circle.right,circle.bottom,selected?16:44,selected?16:44);
            SelectObject(dc,old); SelectObject(dc,old_pen); DeleteObject(brush); DeleteObject(pen);
            text_color(dc,C_TEXT); DrawTextW(dc,initials(state.rooms[i].name).c_str(),-1,&circle,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if (suspended) {
                HPEN slash = CreatePen(PS_SOLID, 3, C_RED);
                auto old_slash = SelectObject(dc, slash);
                MoveToEx(dc, circle.left + 7, circle.bottom - 7, nullptr);
                LineTo(dc, circle.right - 7, circle.top + 7);
                SelectObject(dc, old_slash);
                DeleteObject(slash);
            }
            room_hits_.push_back({circle,static_cast<int>(i),{}}); y += 54;
        }
        (void)rail;
    }

    void draw_channels(HDC dc, const vkrooms::AppSnapshot& state, int rail, int right, int bottom) {
        RECT title{rail+16,14,right-8,44}; SelectObject(dc,font_title_); text_color(dc,C_TEXT);
        const std::wstring room_name = state.selected_room >= 0 && state.selected_room < static_cast<int>(state.rooms.size())
            ? vkrooms::utf8_to_wide(state.rooms[static_cast<std::size_t>(state.selected_room)].name) : L"VeilKnit Rooms";
        DrawTextW(dc,room_name.c_str(),-1,&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(dc,font_bold_); text_color(dc,C_FAINT);
        RECT group{rail+16,72,right-12,96}; DrawTextW(dc,L"TEXT CHANNELS",-1,&group,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT general{rail+10,98,right-10,134}; fill_rect(dc,general,show_log_?C_CHANNEL:C_HOVER); text_color(dc,show_log_?C_MUTED:C_TEXT);
        DrawTextW(dc,L"#  general",-1,&general,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT log{rail+10,136,right-10,172}; fill_rect(dc,log,show_log_?C_HOVER:C_CHANNEL); text_color(dc,show_log_?C_TEXT:C_MUTED);
        DrawTextW(dc,L"#  room-log",-1,&log,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        channel_general_ = general; channel_log_ = log;

        fill_rect(dc,RECT{rail,bottom-72,right,bottom},C_RAIL);
        SelectObject(dc,font_bold_); text_color(dc,C_TEXT);
        RECT user{rail+14,bottom-65,right-12,bottom-42}; DrawTextW(dc,vkrooms::utf8_to_wide(state.username.empty()?"Not connected":state.username).c_str(),-1,&user,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(dc,font_small_); text_color(dc,state.connection==vkrooms::ConnectionState::connected?C_GREEN:C_MUTED);
        RECT status{rail+14,bottom-40,right-12,bottom-10}; DrawTextW(dc,vkrooms::utf8_to_wide(state.status).c_str(),-1,&status,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
    }

    void draw_header(HDC dc, const vkrooms::AppSnapshot& state, int left, int right) {
        SelectObject(dc,font_title_); text_color(dc,C_TEXT);
        std::wstring channel_name = show_log_ ? L"# room-log" : L"# general";
        RECT title{left + 12, 46, right - 16, 74};
        DrawTextW(dc,channel_name.c_str(),-1,&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

        SelectObject(dc,font_small_);
        text_color(dc, state.connection == vkrooms::ConnectionState::connected ? C_GREEN : C_MUTED);
        std::wstring line = vkrooms::utf8_to_wide(state.status.empty() ? std::string("Starting up") : state.status);
        if (state.selected_room >= 0 && state.selected_room < static_cast<int>(state.rooms.size())) {
            const auto& room = state.rooms[static_cast<std::size_t>(state.selected_room)];
            line += L"  •  ";
            line += vkrooms::utf8_to_wide(std::to_string(room.members.size()) + " members");
            line += L"  •  ";
            line += room.suspended ? L"PAUSED — right-click room and Retry" : (room.local_replica ? L"replica active" : L"replica off");
        }
        RECT status{left + 150, 49, right - 16, 72};
        DrawTextW(dc,line.c_str(),-1,&status,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

        HPEN pen=CreatePen(PS_SOLID,1,C_BORDER); auto old=SelectObject(dc,pen); MoveToEx(dc,left,85,nullptr); LineTo(dc,right,85); SelectObject(dc,old); DeleteObject(pen);
    }

    void draw_messages(HDC dc, const vkrooms::AppSnapshot& state, RECT area) {
        message_hits_.clear();
        if (state.selected_room < 0 || state.selected_room >= static_cast<int>(state.rooms.size())) {
            SelectObject(dc,font_title_); text_color(dc,C_MUTED); DrawTextW(dc,L"Create or join a room to begin.",-1,&area,DT_CENTER|DT_VCENTER|DT_SINGLELINE); return;
        }
        const auto& room = state.rooms[static_cast<std::size_t>(state.selected_room)];
        int y = area.bottom - 14 + chat_scroll_;
        for (auto iterator = room.messages.rbegin(); iterator != room.messages.rend(); ++iterator) {
            const auto& message = *iterator;
            if (show_log_ != message.system) continue;
            const int index = static_cast<int>(std::distance(iterator, room.messages.rend())) - 1;
            const int width = area.right-area.left-94;
            RECT calc{0,0,width,0}; SelectObject(dc,font_);
            const std::wstring body = message.deleted ? L"[message deleted]" : vkrooms::utf8_to_wide(message.text);
            DrawTextW(dc,body.c_str(),-1,&calc,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
            const int body_h = std::max<int>(20, static_cast<int>(calc.bottom - calc.top));
            const int height = message.system ? body_h+18 : body_h+42;
            y -= height;
            if (y < area.bottom && y+height > area.top) {
                RECT row{area.left+8,y,area.right-8,y+height};
                if (!message.system && selected_message_ == message.event_id) fill_rect(dc,row,C_HOVER);
                if (message.system) {
                    SelectObject(dc,font_small_); text_color(dc,C_FAINT);
                    RECT text{area.left+18,y+6,area.right-18,y+height-4}; DrawTextW(dc,body.c_str(),-1,&text,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
                } else {
                    RECT avatar{area.left+18,y+8,area.left+54,y+44}; HBRUSH brush=CreateSolidBrush(message.sender_main_dht==state.main_dht?C_ACCENT:C_HOVER);
                    auto old=SelectObject(dc,brush); auto pen=CreatePen(PS_NULL,0,0); auto oldp=SelectObject(dc,pen); Ellipse(dc,avatar.left,avatar.top,avatar.right,avatar.bottom); SelectObject(dc,old);SelectObject(dc,oldp);DeleteObject(brush);DeleteObject(pen);
                    SelectObject(dc,font_bold_); text_color(dc,C_TEXT); DrawTextW(dc,initials(message.sender_name).c_str(),-1,&avatar,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                    RECT name{area.left+66,y+7,area.right-16,y+26}; text_color(dc,message.sender_main_dht==state.main_dht?RGB(147,157,255):C_TEXT);
                    DrawTextW(dc,vkrooms::utf8_to_wide(message.sender_name).c_str(),-1,&name,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
                    SelectObject(dc,font_small_); text_color(dc,C_FAINT); RECT time{name.right-92,y+8,name.right,y+25};
                    DrawTextW(dc,vkrooms::utf8_to_wide(vkrooms::format_time(message.created_at)+(message.recovery?" • recovery":"")).c_str(),-1,&time,DT_RIGHT|DT_SINGLELINE);
                    SelectObject(dc,font_); text_color(dc,message.deleted?C_FAINT:C_TEXT); RECT text{area.left+66,y+29,area.right-16,y+height-5};
                    DrawTextW(dc,body.c_str(),-1,&text,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
                    message_hits_.push_back({row,index,message.event_id});
                }
            }
            y -= 5;
            if (y > area.bottom + 10000 || y < area.top - 2000) {
                if (y < area.top - 2000) break;
            }
        }
    }

    void draw_members(HDC dc, const vkrooms::AppSnapshot& state, RECT area) {
        member_hits_.clear();
        if (state.selected_room < 0 || state.selected_room >= static_cast<int>(state.rooms.size())) return;
        const auto& room = state.rooms[static_cast<std::size_t>(state.selected_room)];
        int y=area.top+18;
        const vkrooms::Role groups[] = {vkrooms::Role::owner,vkrooms::Role::moderator,vkrooms::Role::helper,vkrooms::Role::member};
        for (const auto role : groups) {
            std::vector<const vkrooms::Member*> members;
            for (const auto& [_,member] : room.members) if (member.role==role) members.push_back(&member);
            if (members.empty()) continue;
            SelectObject(dc,font_bold_); text_color(dc,C_FAINT); RECT heading{area.left+16,y,area.right-10,y+22};
            std::wstring title=vkrooms::utf8_to_wide(std::string(vkrooms::role_name(role))+"S — "+std::to_string(members.size()));
            DrawTextW(dc,title.c_str(),-1,&heading,DT_LEFT|DT_SINGLELINE); y+=25;
            for (const auto* member : members) {
                RECT row{area.left+8,y,area.right-8,y+42}; if (selected_member_==member->main_dht) fill_rect(dc,row,C_HOVER);
                RECT dot{area.left+18,y+14,area.left+28,y+24}; HBRUSH brush=CreateSolidBrush(member->banned?C_RED:(member->online?C_GREEN:C_FAINT)); auto old=SelectObject(dc,brush); auto pen=CreatePen(PS_NULL,0,0); auto oldp=SelectObject(dc,pen); Ellipse(dc,dot.left,dot.top,dot.right,dot.bottom);SelectObject(dc,old);SelectObject(dc,oldp);DeleteObject(brush);DeleteObject(pen);
                SelectObject(dc,font_bold_); text_color(dc,member->banned?C_FAINT:C_MUTED); RECT name{area.left+38,y+5,area.right-10,y+24}; DrawTextW(dc,vkrooms::utf8_to_wide(member->display_name.empty()?vkrooms::short_identity(member->main_dht):member->display_name).c_str(),-1,&name,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
                SelectObject(dc,font_small_); text_color(dc,C_FAINT); RECT sub{area.left+38,y+23,area.right-10,y+40};
                std::string reputation;
                if (member->reputation_class.empty() || member->reputation_confidence == 0) reputation = "reputation pending";
                else reputation = member->reputation_class + " " + std::to_string(member->reputation_confidence) + "%";
                std::string details = (member->replica ? "replica • " : "") + reputation;
                DrawTextW(dc,vkrooms::utf8_to_wide(details).c_str(),-1,&sub,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
                member_hits_.push_back({row,0,member->main_dht}); y+=44;
            }
            y+=10;
        }
    }

    void click(int x, int y, bool right) {
        POINT point{x,y};
        for (const auto& hit : room_hits_) if (PtInRect(&hit.rect,point)) {
            engine_.select_room(hit.index);
            update_control_labels();
            if (right) room_menu(x, y, hit.index);
            return;
        }
        if (PtInRect(&channel_general_,point)) { show_log_=false; update_control_labels(); InvalidateRect(hwnd_,nullptr,FALSE); return; }
        if (PtInRect(&channel_log_,point)) { show_log_=true; update_control_labels(); InvalidateRect(hwnd_,nullptr,FALSE); return; }
        for (const auto& hit : member_hits_) if (PtInRect(&hit.rect,point)) {
            selected_member_=hit.id; engine_.refresh_member_reputation(hit.id); InvalidateRect(hwnd_,nullptr,FALSE);
            if (right) member_menu(x,y,hit.id); return;
        }
        for (const auto& hit : message_hits_) if (PtInRect(&hit.rect,point)) {
            selected_message_=hit.id; InvalidateRect(hwnd_,nullptr,FALSE); if (right) message_menu(x,y,hit.id); return;
        }
    }

    void room_menu(int x, int y, int index) {
        const auto state = engine_.snapshot();
        if (index < 0 || index >= static_cast<int>(state.rooms.size())) return;
        const auto& room = state.rooms[static_cast<std::size_t>(index)];
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Open Room");
        AppendMenuW(menu, MF_STRING | (room.suspended ? MF_GRAYED : 0), 2, L"Share Invite / QR");
        AppendMenuW(menu, MF_STRING | (room.suspended ? MF_GRAYED : 0), 3, L"Synchronize History");
        AppendMenuW(menu, MF_STRING | (room.suspended ? 0 : MF_GRAYED), 4, L"Retry Room");
        AppendMenuW(menu, MF_STRING | (room.suspended ? MF_GRAYED : 0), 5, room.local_replica ? L"Stop Replica" : L"Become Replica");
        AppendMenuW(menu, MF_STRING | (room.suspended ? MF_GRAYED : 0), 6, L"Room Policy");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 7, L"Remove Room from This Device");
        POINT screen{x,y}; ClientToScreen(hwnd_,&screen);
        const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
        if (choice == 1) return;
        if (choice == 2) {
            const auto invite = engine_.selected_invite_code();
            if (invite.empty()) MessageBoxW(hwnd_, L"This room does not have a published owner store yet.", L"Invite unavailable", MB_ICONINFORMATION);
            else show_invite_qr(hwnd_, invite);
        } else if (choice == 3) engine_.sync_selected_room();
        else if (choice == 4) engine_.retry_selected_room();
        else if (choice == 5) engine_.toggle_replica();
        else if (choice == 6) edit_room_policy();
        else if (choice == 7) {
            const std::wstring prompt = L"Remove ‘" + vkrooms::utf8_to_wide(room.name) +
                L"’ from this device?\n\nThis removes the local membership and history. Distributed copies are not deleted.";
            if (MessageBoxW(hwnd_, prompt.c_str(), L"Remove room", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
                engine_.remove_selected_room();
            }
        }
    }

    void member_menu(int x, int y, const std::string& id) {
        const auto state = engine_.snapshot();
        if (state.selected_room < 0 || state.selected_room >= static_cast<int>(state.rooms.size())) return;
        const auto& room = state.rooms[static_cast<std::size_t>(state.selected_room)];
        const auto local = room.members.find(state.main_dht);
        const auto target = room.members.find(id);
        const auto local_role = local == room.members.end() ? vkrooms::Role::member : local->second.role;
        const auto target_role = target == room.members.end() ? vkrooms::Role::member : target->second.role;
        const bool target_is_owner = id == room.owner_main_dht;
        HMENU menu=CreatePopupMenu();
        if (!target_is_owner && local_role == vkrooms::Role::owner) {
            AppendMenuW(menu,MF_STRING,1,L"Make Moderator");
            if (target_role != vkrooms::Role::member) AppendMenuW(menu,MF_STRING,3,L"Make Member");
        } else if (!target_is_owner && local_role == vkrooms::Role::moderator && target_role != vkrooms::Role::moderator) {
            AppendMenuW(menu,MF_STRING,2,L"Make Helper");
            if (target_role != vkrooms::Role::member) AppendMenuW(menu,MF_STRING,3,L"Make Member");
        }
        const bool can_ban = !target_is_owner && (local_role == vkrooms::Role::owner ||
            (local_role == vkrooms::Role::moderator && target_role != vkrooms::Role::moderator));
        if (can_ban) AppendMenuW(menu,MF_STRING,4,target != room.members.end() && target->second.banned ? L"Unban User" : L"Ban User");
        if (GetMenuItemCount(menu) > 0) AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,5,L"Copy DHT Address"); AppendMenuW(menu,MF_STRING,6,L"Refresh Reputation");
        POINT screen{x,y}; ClientToScreen(hwnd_,&screen);
        const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,hwnd_,nullptr); DestroyMenu(menu);
        if (command==1) engine_.change_member_role(id,vkrooms::Role::moderator);
        else if (command==2) engine_.change_member_role(id,vkrooms::Role::helper);
        else if (command==3) engine_.change_member_role(id,vkrooms::Role::member);
        else if (command==4) engine_.toggle_member_ban(id);
        else if (command==5) set_clipboard_text(hwnd_,vkrooms::utf8_to_wide(id));
        else if (command==6) engine_.refresh_member_reputation(id);
    }

    void message_menu(int x, int y, const std::string& id) {
        const auto state = engine_.snapshot();
        const vkrooms::Room* room = nullptr;
        const vkrooms::ChatMessage* message = nullptr;
        if (state.selected_room >= 0 && state.selected_room < static_cast<int>(state.rooms.size())) {
            room = &state.rooms[static_cast<std::size_t>(state.selected_room)];
            const auto iterator = std::find_if(room->messages.begin(), room->messages.end(), [&](const vkrooms::ChatMessage& item) { return item.event_id == id; });
            if (iterator != room->messages.end()) message = &*iterator;
        }
        HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,1,L"Delete Message"); AppendMenuW(menu,MF_STRING,2,L"Copy Message ID");
        if (room && message && message->sender_main_dht != state.main_dht) {
            const auto local = room->members.find(state.main_dht);
            const auto target = room->members.find(message->sender_main_dht);
            const auto local_role = local == room->members.end() ? vkrooms::Role::member : local->second.role;
            const auto target_role = target == room->members.end() ? vkrooms::Role::member : target->second.role;
            const bool target_is_owner = message->sender_main_dht == room->owner_main_dht;
            if (!target_is_owner && (local_role == vkrooms::Role::owner || local_role == vkrooms::Role::moderator)) {
                AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
                if (local_role == vkrooms::Role::owner) AppendMenuW(menu,MF_STRING,3,L"Make User Moderator");
                else if (target_role != vkrooms::Role::moderator) AppendMenuW(menu,MF_STRING,4,L"Make User Helper");
                if (local_role == vkrooms::Role::owner || target_role != vkrooms::Role::moderator)
                    AppendMenuW(menu,MF_STRING,5,target != room->members.end() && target->second.banned ? L"Unban User" : L"Ban User");
            }
        }
        POINT screen{x,y};ClientToScreen(hwnd_,&screen); const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,hwnd_,nullptr);DestroyMenu(menu);
        if(command==1)engine_.delete_message(id);
        else if(command==2)set_clipboard_text(hwnd_,vkrooms::utf8_to_wide(id));
        else if (message && command==3) engine_.change_member_role(message->sender_main_dht, vkrooms::Role::moderator);
        else if (message && command==4) engine_.change_member_role(message->sender_main_dht, vkrooms::Role::helper);
        else if (message && command==5) engine_.toggle_member_ban(message->sender_main_dht);
    }

    HWND hwnd_ = nullptr, compose_ = nullptr, send_ = nullptr;
    std::vector<HWND> toolbar_;
    WNDPROC old_edit_proc_ = nullptr;
    HFONT font_ = nullptr, font_bold_ = nullptr, font_title_ = nullptr, font_small_ = nullptr;
    HICON logo_icon_ = nullptr;
    HBRUSH input_brush_ = nullptr, panel_brush_ = nullptr;
    vkrooms::RoomEngine engine_;
    std::vector<HitItem> room_hits_, member_hits_, message_hits_;
    RECT channel_general_{}, channel_log_{};
    bool show_log_ = false;
    int chat_scroll_ = 0;
    std::string selected_member_, selected_message_;
    std::string last_logged_status_;
    vkrooms::ConnectionState last_logged_connection_ = vkrooms::ConnectionState::disconnected;
    std::size_t last_logged_message_count_ = static_cast<std::size_t>(-1);
    std::size_t last_logged_member_count_ = static_cast<std::size_t>(-1);
};

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* window = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_CREATE){SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(new MainWindow(hwnd)));return 0;}
    if(message==WM_NCDESTROY){SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);delete window;return DefWindowProcW(hwnd,message,wparam,lparam);}
    return window?window->handle(message,wparam,lparam):DefWindowProcW(hwnd,message,wparam,lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    if (auto awareness = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")))
        awareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSEXW wc{sizeof(wc)};wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=MainProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=CreateSolidBrush(C_BG);wc.lpszClassName=L"VeilKnitRoomsMain";wc.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_VEILKNIT),IMAGE_ICON,32,32,LR_DEFAULTCOLOR));wc.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_VEILKNIT),IMAGE_ICON,16,16,LR_DEFAULTCOLOR));
    if(!RegisterClassExW(&wc))return 1;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"VeilKnit Rooms",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1420,860,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 1;
    BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
    ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return static_cast<int>(message.wParam);
}

#else
int main() { return 0; }
#endif
