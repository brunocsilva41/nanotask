#pragma once
#include "Foundation.hpp"
#include "UiLauncher.hpp"
#include "TempProviders.hpp"
#include <shellapi.h>
#include <thread>
#include <stop_token>
#include <chrono>

namespace nt {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT ID_TRAY = 1001;
constexpr wchar_t kClassName[] = L"NanoTaskGhost";

class TrayHost {
public:
    bool Init(HINSTANCE hInst) {
        m_hInst = hInst;
        WNDCLASSEXW wx{}; wx.cbSize = sizeof(wx);
        wx.lpfnWndProc = &TrayHost::WndProcStatic;
        wx.hInstance = hInst;
        wx.lpszClassName = kClassName;
        if (!::RegisterClassExW(&wx)) { LogWin32Error("RegisterClassEx"); return false; }
        m_hwnd = ::CreateWindowExW(0, kClassName, L"", 0,0,0,0,0,
            HWND_MESSAGE, nullptr, hInst, this);
        if (!m_hwnd) { LogWin32Error("CreateWindowEx HWND_MESSAGE"); return false; }
        NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd; nid.uID = ID_TRAY;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_TRAY;
        nid.hIcon = MakeTempIcon(42.0f).release();
        m_lastIcon = nid.hIcon;
        ::wcscpy_s(nid.szTip, L"NanoTask - 42C");
        if (!::Shell_NotifyIconW(NIM_ADD, &nid)) { LogWin32Error("Shell_NotifyIcon ADD"); return false; }
        return true;
    }

    void RunWorker(std::stop_token st) {
        MultiTempProvider temps; // NVML -> WMI; "none" = sweep sintético (VMs sem sensor)
        float fakeTemp = 45.0f;
        while (!st.stop_requested()) {
            if (auto t = temps.ReadCelsius()) {
                UpdateIcon(*t); // sensor real; fonte visível via temps.Source()
            } else {
                UpdateIcon(fakeTemp);
                fakeTemp += 1.0f; if (fakeTemp > 85.0f) fakeTemp = 45.0f;
            }
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
        NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd; nid.uID = ID_TRAY;
        ::Shell_NotifyIconW(NIM_DELETE, &nid);
        if (m_lastIcon) ::DestroyIcon(m_lastIcon);
    }

private:
    UniqueIcon MakeTempIcon(float temp) noexcept {
        HDC hdcScreen = ::GetDC(nullptr);
        HDC hdcMem = ::CreateCompatibleDC(hdcScreen);
        HBITMAP hbmp = ::CreateCompatibleBitmap(hdcScreen, 16, 16);
        HBITMAP hbmpMask = ::CreateBitmap(16, 16, 1, 1, nullptr);
        auto hOld = ::SelectObject(hdcMem, hbmp);
        ::SetBkColor(hdcMem, RGB(0,0,0));
        ::SetTextColor(hdcMem, TempToColor(temp));
        wchar_t buf[8]{};
        ::swprintf_s(buf, L"%d", static_cast<int>(temp));
        HFONT hFont = ::CreateFontW(11,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Consolas");
        auto hOldFont = ::SelectObject(hdcMem, hFont);
        ::TextOutW(hdcMem, 0, 1, buf, ::lstrlenW(buf));
        ICONINFO ii{}; ii.fIcon = TRUE; ii.hbmColor = hbmp; ii.hbmMask = hbmpMask;
        HICON hIcon = ::CreateIconIndirect(&ii);
        ::SelectObject(hdcMem, hOldFont); ::DeleteObject(hFont);
        ::SelectObject(hdcMem, hOld); ::DeleteObject(hbmp); ::DeleteObject(hbmpMask);
        ::DeleteDC(hdcMem); ::ReleaseDC(nullptr, hdcScreen);
        if (!hIcon) LogWin32Error("CreateIconIndirect");
        return UniqueIcon(hIcon);
    }

    void UpdateIcon(float temp) {
        auto icon = MakeTempIcon(temp);
        if (!icon) return;
        NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd; nid.uID = ID_TRAY;
        nid.uFlags = NIF_ICON | NIF_TIP;
        nid.hIcon = icon.get();
        ::swprintf_s(nid.szTip, L"NanoTask - %dC", static_cast<int>(temp));
        if (!::Shell_NotifyIconW(NIM_MODIFY, &nid)) { LogWin32Error("Shell_NotifyIcon MODIFY"); return; }
        if (m_lastIcon) ::DestroyIcon(m_lastIcon);
        m_lastIcon = icon.release();
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
        if (m == WM_TRAY && LOWORD(l) == WM_LBUTTONUP) {
            UiLauncher::Show(h);
            return 0;
        }
        if (m == WM_DESTROY) { ::PostQuitMessage(0); return 0; }
        return ::DefWindowProcW(h, m, w, l);
    }

    HINSTANCE m_hInst{nullptr};
    HWND m_hwnd{nullptr};
    HICON m_lastIcon{nullptr};
};

} // namespace nt
