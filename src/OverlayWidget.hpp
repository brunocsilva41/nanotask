#pragma once
// OverlayWidget.hpp — widget flutuante estilo gadget: números grandes e claros.
// Janela WS_POPUP (240x104) sempre no topo, sem foco, cantos arredondados,
// redesenhada a cada 2s via PostMessage (nunca trava a Message Pump).
#include "Foundation.hpp"
#include <mutex>

namespace nt {

struct WidgetData {
    float tempC = 0.f;
    float cpuPct = 0.f;
    uint64_t memMB = 0;
    wchar_t src[8]{}; // "nvml"|"wmi"|"sim"
};

class OverlayWidget {
public:
    bool Create(HINSTANCE hInst) {
        if (hwnd_) return true;
        WNDCLASSEXW wx{}; wx.cbSize = sizeof(wx);
        wx.lpfnWndProc = &OverlayWidget::WndProcStatic;
        wx.hInstance = hInst;
        wx.lpszClassName = L"NanoTaskWidget";
        wx.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        if (!::RegisterClassExW(&wx) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            LogWin32Error("Widget:Register"); return false;
        }
        RECT work{}; ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        hwnd_ = ::CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
            L"NanoTaskWidget", L"NanoTask", WS_POPUP,
            work.right - kW - 12, work.bottom - kH - 12, kW, kH,
            nullptr, nullptr, hInst, this);
        if (!hwnd_) { LogWin32Error("Widget:Create"); return false; }
        ::SetLayeredWindowAttributes(hwnd_, 0, opacity_, LWA_ALPHA);
        HRGN rgn = ::CreateRoundRectRgn(0, 0, kW + 1, kH + 1, 16, 16);
        if (rgn) ::SetWindowRgn(hwnd_, rgn, TRUE); // OS assume o RGN
        ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        return true;
    }

    void Destroy() {
        if (hwnd_) { ::DestroyWindow(hwnd_); hwnd_ = nullptr; }
    }

    bool Visible() const { return hwnd_ && ::IsWindowVisible(hwnd_) ? true : false; }

    void SetVisible(bool v) {
        if (!hwnd_) return;
        ::ShowWindow(hwnd_, v ? SW_SHOWNOACTIVATE : SW_HIDE);
    }

    void SetOpacity(BYTE o) {
        opacity_ = o;
        if (hwnd_) ::SetLayeredWindowAttributes(hwnd_, 0, opacity_, LWA_ALPHA);
    }

    // Thread-safe: chamada do worker. Só posta mensagem, nunca desenha aqui.
    void PostUpdate(const WidgetData& d) {
        { std::lock_guard lk(m_); data_ = d; dirty_ = true; }
        if (hwnd_) ::PostMessageW(hwnd_, kMsgUpdate, 0, 0);
    }

private:
    static constexpr int kW = 248, kH = 104;
    static constexpr UINT kMsgUpdate = WM_APP + 10;

    static LRESULT CALLBACK WndProcStatic(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
            ::SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto self = reinterpret_cast<OverlayWidget*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
        if (self) return self->WndProc(h, m, w, l);
        return ::DefWindowProcW(h, m, w, l);
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case kMsgUpdate: {
            { std::lock_guard lk(m_); dirty_ = false; }
            ::InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_PAINT: Paint(h); return 0;
        case WM_NCHITTEST:
            // Arrastável pela área toda, a menos que travado
            return locked_ ? HTCLIENT : HTCAPTION;
        case WM_DISPLAYCHANGE: {
            RECT work{}; ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            ::SetWindowPos(h, nullptr, work.right - kW - 12, work.bottom - kH - 12,
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        }
        return ::DefWindowProcW(h, m, w, l);
    }

    void Paint(HWND h) {
        PAINTSTRUCT ps{};
        HDC hdc = ::BeginPaint(h, &ps);
        RECT rc{}; ::GetClientRect(h, &rc);
        HDC mem = ::CreateCompatibleDC(hdc);
        HBITMAP bmp = ::CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = static_cast<HBITMAP>(::SelectObject(mem, bmp));

        HBRUSH bg = ::CreateSolidBrush(RGB(11, 15, 25));
        ::FillRect(mem, &rc, bg);
        ::DeleteObject(bg);

        WidgetData d{};
        { std::lock_guard lk(m_); d = data_; }

        ::SetBkMode(mem, TRANSPARENT);

        // Temperatura gigante, cor por faixa
        HFONT big = ::CreateFontW(46, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        HFONT med = ::CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        HFONT oldF = static_cast<HFONT>(::SelectObject(mem, big));
        ::SetTextColor(mem, TempToColor(d.tempC));
        wchar_t t[16]{};
        ::swprintf_s(t, L"%.0f°", d.tempC);
        ::TextOutW(mem, 12, 2, t, ::lstrlenW(t));

        // Fonte do sensor no canto superior direito
        ::SelectObject(mem, med);
        ::SetTextColor(mem, RGB(100, 116, 139));
        ::TextOutW(mem, kW - 62, 10, d.src, ::lstrlenW(d.src));

        // CPU + RAM legíveis
        ::SetTextColor(mem, RGB(226, 232, 240));
        wchar_t s2[64]{};
        ::swprintf_s(s2, L"CPU %.0f%%   %.1f GB", d.cpuPct, d.memMB / 1024.0);
        ::TextOutW(mem, 12, kH - 30, s2, ::lstrlenW(s2));

        ::SelectObject(mem, oldF);
        ::DeleteObject(big); ::DeleteObject(med);

        ::BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        ::SelectObject(mem, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(mem);
        ::EndPaint(h, &ps);
    }

    HWND hwnd_ = nullptr;
    WidgetData data_{};
    bool dirty_ = false;
    bool locked_ = false;
    BYTE opacity_ = 235;
    std::mutex m_;
};

} // namespace nt
