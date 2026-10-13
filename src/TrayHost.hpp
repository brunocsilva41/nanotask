#pragma once
#include "Foundation.hpp"
#include "UiLauncher.hpp"
#include "Settings.hpp"
#include "TempProviders.hpp"
#include "Blackbox.hpp"
#include "OverlayWidget.hpp"
#include "Dashboard.hpp"
#include "BugReport.hpp"
#include <shellapi.h>
#include <thread>
#include <stop_token>
#include <chrono>
#include <cstring>
#include <algorithm>

namespace nt {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT ID_TRAY = 1001;
constexpr wchar_t kClassName[] = L"NanoTaskGhost";

constexpr UINT IDM_WIDGET = 101;
constexpr UINT IDM_GHOST = 102;
constexpr UINT IDM_PINNED = 103;
constexpr UINT IDM_REPORT = 104;
constexpr UINT IDM_EXIT = 105;
constexpr UINT IDM_DASH = 106;

class TrayHost {
public:
    bool Init(HINSTANCE hInst) {
        m_hInst = hInst;
        WNDCLASSEXW wx{}; wx.cbSize = sizeof(wx);
        wx.lpfnWndProc = &TrayHost::WndProcStatic;
        wx.hInstance = hInst;
        wx.lpszClassName = kClassName;
        if (!::RegisterClassExW(&wx)) { LogWin32Error("RegisterClassEx"); return false; }
        m_hwnd = ::CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, hInst, this);
        if (!m_hwnd) { LogWin32Error("CreateWindowEx HWND_MESSAGE"); return false; }
        NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd; nid.uID = ID_TRAY;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_TRAY;
        nid.hIcon = MakeTempIcon(42.0f).release();
        m_lastIcon = nid.hIcon;
        ::swprintf_s(nid.szTip, L"NanoTask — 42°C  CPU —");
        if (!::Shell_NotifyIconW(NIM_ADD, &nid)) { LogWin32Error("Shell_NotifyIcon ADD"); return false; }
        m_telem.Start(); // PDH + top PID em background; jthread joina sozinho
        if (!::AddClipboardFormatListener(m_hwnd)) LogWin32Error("Clipboard:Listener");
        return true;
    }

    void Configure(const Settings& cfg) {
        m_mode = cfg.taskbarMode;
        dashCfg_.kwhPrice = cfg.kwhPrice;
        dashCfg_.tdpWatts = cfg.tdpWatts;
        dashCfg_.confirmKill = cfg.confirmKill;
        dashCfg_.protectCritical = cfg.protectCritical;
        if (cfg.widgetEnabled) m_widget.Create(m_hInst);
        else m_widget.Destroy();
    }

    void RunWorker(std::stop_token st) {
        MultiTempProvider temps;
        float fakeTemp = 45.0f;
        while (!st.stop_requested()) {
            float temp = fakeTemp;
            wchar_t src[8]{ L's', L'i', L'm', 0 };
            if (auto t = temps.ReadCelsius()) {
                temp = *t;
                // Source() é narrow ("nvml"|"wmi") -> converte p/ wide do widget
                char narrow[8]{};
                auto sv = temps.Source();
                ::strncpy_s(narrow, sv.data(), (std::min)(sv.size(), sizeof(narrow) - 1));
                size_t n = 0;
                ::mbstowcs_s(&n, src, narrow, _TRUNCATE);
            } else {
                fakeTemp += 1.0f; if (fakeTemp > 85.0f) fakeTemp = 45.0f;
            }
            float cpu = 0.f; uint64_t mem = 0;
            if (auto snap = m_box.Snapshot(); !snap.empty()) {
                cpu = snap.back().cpuPct;
                mem = snap.back().memUsedMB;
            }
            UpdateIcon(temp, cpu);
            WidgetData wd{};
            wd.tempC = temp; wd.cpuPct = cpu; wd.memMB = mem;
            wcsncpy_s(wd.src, src, _TRUNCATE);
            m_widget.PostUpdate(wd);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    int Pump() {
        MSG msg{};
        while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

    void Shutdown() {
        m_dash.Close();
        m_widget.Destroy();
        ::RemoveClipboardFormatListener(m_hwnd);
        NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd; nid.uID = ID_TRAY;
        ::Shell_NotifyIconW(NIM_DELETE, &nid);
        if (m_lastIcon) ::DestroyIcon(m_lastIcon);
        m_lastIcon = nullptr;
    }

    void OpenDashboard() {
        m_dash.Open(m_hInst, &m_box, dashCfg_, [this] {
            std::lock_guard lk(clipMtx_);
            return clip_;
        });
    }

    std::vector<std::wstring> Clipboard() {
        std::lock_guard lk(clipMtx_);
        return clip_;
    }

private:
    // CF_UNICODETEXT apenas; imagens/arquivos ignorados. Máx 20 itens, 4KB cada.
    void OnClipboard() {
        if (!::OpenClipboard(m_hwnd)) return;
        HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
        if (h) {
            const wchar_t* p = static_cast<const wchar_t*>(::GlobalLock(h));
            if (p) {
                std::wstring s(p);
                ::GlobalUnlock(h);
                if (s.size() > 4096) s.resize(4096);
                if (!s.empty()) {
                    std::lock_guard lk(clipMtx_);
                    clip_.erase(std::remove(clip_.begin(), clip_.end(), s), clip_.end());
                    clip_.insert(clip_.begin(), std::move(s));
                    if (clip_.size() > 20) clip_.resize(20);
                }
            }
        }
        ::CloseClipboard();
    }
    // Ícone 16x16 de máxima legibilidade: fundo sólido escuro + dígitos
    // grandes centralizados na cor da faixa. 2 caracteres no máximo.
    UniqueIcon MakeTempIcon(float temp) noexcept {
        HDC hdcScreen = ::GetDC(nullptr);
        HDC hdcMem = ::CreateCompatibleDC(hdcScreen);
        HBITMAP hbmp = ::CreateCompatibleBitmap(hdcScreen, 16, 16);
        HBITMAP hbmpMask = ::CreateBitmap(16, 16, 1, 1, nullptr);
        auto hOld = ::SelectObject(hdcMem, hbmp);

        HBRUSH bg = ::CreateSolidBrush(RGB(11, 15, 25));
        RECT rc{ 0, 0, 16, 16 };
        ::FillRect(hdcMem, &rc, bg);
        ::DeleteObject(bg);

        ::SetBkMode(hdcMem, TRANSPARENT);
        ::SetTextColor(hdcMem, TempToColor(temp));
        ::SetTextAlign(hdcMem, TA_CENTER | TA_TOP);
        wchar_t buf[8]{};
        int v = static_cast<int>(temp);
        if (v > 99) v = 99; if (v < 0) v = 0;
        ::swprintf_s(buf, L"%d", v);
        HFONT hFont = ::CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Consolas");
        auto hOldFont = ::SelectObject(hdcMem, hFont);
        ::TextOutW(hdcMem, 8, 1, buf, ::lstrlenW(buf));

        ICONINFO ii{}; ii.fIcon = TRUE; ii.hbmColor = hbmp; ii.hbmMask = hbmpMask;
        HICON hIcon = ::CreateIconIndirect(&ii);

        ::SelectObject(hdcMem, hOldFont); ::DeleteObject(hFont);
        ::SelectObject(hdcMem, hOld); ::DeleteObject(hbmp); ::DeleteObject(hbmpMask);
        ::DeleteDC(hdcMem); ::ReleaseDC(nullptr, hdcScreen);

        if (!hIcon) LogWin32Error("CreateIconIndirect");
        return UniqueIcon(hIcon);
    }

    void UpdateIcon(float temp, float cpu) {
        auto icon = MakeTempIcon(temp);
        if (!icon) return;
        NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd; nid.uID = ID_TRAY;
        nid.uFlags = NIF_ICON | NIF_TIP;
        nid.hIcon = icon.get();
        ::swprintf_s(nid.szTip, L"NanoTask — %.0f°C  CPU %.0f%%", temp, cpu);
        if (!::Shell_NotifyIconW(NIM_MODIFY, &nid)) { LogWin32Error("Shell_NotifyIcon MODIFY"); return; }
        if (m_lastIcon) ::DestroyIcon(m_lastIcon);
        m_lastIcon = icon.release();
    }

    void ShowMenu() {
        HMENU menu = ::CreatePopupMenu();
        if (!menu) { LogWin32Error("Menu:Create"); return; }
        ::AppendMenuW(menu, MF_STRING | MF_DEFAULT, IDM_DASH, L"Abrir painel");
        ::AppendMenuW(menu, MF_STRING | (m_widget.Visible() ? MF_CHECKED : MF_UNCHECKED), IDM_WIDGET, L"Widget flutuante");
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING | (m_mode == TaskbarMode::Ghost ? MF_CHECKED : 0), IDM_GHOST, L"Modo fantasma (só bandeja)");
        ::AppendMenuW(menu, MF_STRING | (m_mode == TaskbarMode::Pinned ? MF_CHECKED : 0), IDM_PINNED, L"Modo fixo (widget sempre visível)");
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, IDM_REPORT, L"Reportar problema…");
        ::AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Encerrar NanoTask");
        POINT pt{};
        ::GetCursorPos(&pt);
        ::SetForegroundWindow(m_hwnd); // exigido p/ o menu fechar sozinho
        ::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
        ::DestroyMenu(menu);
    }

    static LRESULT CALLBACK WndProcStatic(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
            ::SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto self = reinterpret_cast<TrayHost*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
        if (self) return self->WndProc(h, m, w, l);
        return ::DefWindowProcW(h, m, w, l);
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_TRAY) {
            if (LOWORD(l) == WM_LBUTTONUP) { OpenDashboard(); return 0; }
            if (LOWORD(l) == WM_RBUTTONUP) { ShowMenu(); return 0; }
        }
        if (m == WM_CLIPBOARDUPDATE) { OnClipboard(); return 0; }
        if (m == WM_COMMAND) {
            switch (LOWORD(w)) {
            case IDM_DASH: OpenDashboard(); break;
            case IDM_WIDGET:
                if (m_widget.Visible()) m_widget.SetVisible(false);
                else { m_widget.Create(m_hInst); m_widget.SetVisible(true); }
                break;
            case IDM_GHOST: m_mode = TaskbarMode::Ghost; break;
            case IDM_PINNED:
                m_mode = TaskbarMode::Pinned;
                m_widget.Create(m_hInst); m_widget.SetVisible(true);
                break;
            case IDM_REPORT:
                OpenBugReport(L"brunocsilva41/nanotask", L"1.0.0", m_mode);
                break;
            case IDM_EXIT:
                Shutdown();
                ::PostQuitMessage(0);
                break;
            }
            return 0;
        }
        if (m == WM_DESTROY) { ::PostQuitMessage(0); return 0; }
        return ::DefWindowProcW(h, m, w, l);
    }

    HINSTANCE m_hInst{ nullptr };
    HWND m_hwnd{ nullptr };
    HICON m_lastIcon{ nullptr };
    TaskbarMode m_mode{ TaskbarMode::Ghost };
    OverlayWidget m_widget{};
    Blackbox m_box{};
    TelemetryCollector m_telem{ m_box }; // depois de m_box: dtor joina o jthread antes
    Dashboard m_dash{};
    DashCfg dashCfg_{};
    std::vector<std::wstring> clip_{};
    std::mutex clipMtx_{};
};

} // namespace nt
