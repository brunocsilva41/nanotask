#pragma once
// OverlayWidget.hpp — widget flutuante: números grandes e legíveis.
// Segoe UI (cobre acentos e símbolo de grau), layout com barras, sem overflow.
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
        HRGN rgn = ::CreateRoundRectRgn(0, 0, kW + 1, kH + 1, 18, 18);
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

    void PostUpdate(const WidgetData& d) {
        { std::lock_guard lk(m_); data_ = d; }
        if (hwnd_) ::PostMessageW(hwnd_, kMsgUpdate, 0, 0);
    }

private:
    static constexpr int kW = 300, kH = 148;
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
        case kMsgUpdate:
            ::InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_PAINT: Paint(h); return 0;
        case WM_NCHITTEST:
            return HTCLIENT; // fixo; mover pelo menu? travado p/ não atrapalhar
        case WM_DISPLAYCHANGE: {
            RECT work{}; ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            ::SetWindowPos(h, nullptr, work.right - kW - 12, work.bottom - kH - 12,
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        }
        (void)w; (void)l;
        return ::DefWindowProcW(h, m, w, l);
    }

    static void Bar(HDC dc, int x, int y, int w, int h, float frac, COLORREF fill) {
        if (frac < 0.f) frac = 0.f; if (frac > 1.f) frac = 1.f;
        HBRUSH track = ::CreateSolidBrush(RGB(30, 41, 59));
        RECT tr{ x, y, x + w, y + h };
        ::FillRect(dc, &tr, track);
        ::DeleteObject(track);
        RECT fr{ x, y, x + static_cast<int>(w * frac), y + h };
        if (fr.right > fr.left) {
            HBRUSH b = ::CreateSolidBrush(fill);
            ::FillRect(dc, &fr, b);
            ::DeleteObject(b);
        }
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

        HFONT big = ::CreateFontW(52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT med = ::CreateFontW(19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT sml = ::CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT oldF = static_cast<HFONT>(::SelectObject(mem, big));

        // Temperatura grande à esquerda
        ::SetTextColor(mem, TempToColor(d.tempC));
        wchar_t t[16]{};
        ::swprintf_s(t, L"%.0f\u00B0", d.tempC); // U+00B0 existe no Segoe UI
        RECT rt{ 12, 0, 150, 66 };
        ::DrawTextW(mem, t, -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Fonte do sensor + labels à direita
        ::SelectObject(mem, med);
        ::SetTextColor(mem, RGB(148, 163, 184));
        RECT rs{ 150, 6, kW - 10, 30 };
        ::DrawTextW(mem, d.src, -1, &rs, DT_RIGHT | DT_SINGLELINE);

        ::SelectObject(mem, sml);
        wchar_t l1[32]{}, l2[32]{};
        ::swprintf_s(l1, L"CPU %.0f%%", static_cast<double>(d.cpuPct));
        double gb = d.memMB / 1024.0;
        ::swprintf_s(l2, L"RAM %.1f GB", gb);
        RECT r1{ 150, 32, kW - 10, 54 }, r2{ 150, 54, kW - 10, 76 };
        ::SetTextColor(mem, RGB(226, 232, 240));
        ::DrawTextW(mem, l1, -1, &r1, DT_RIGHT | DT_SINGLELINE);
        ::DrawTextW(mem, l2, -1, &r2, DT_RIGHT | DT_SINGLELINE);

        // Barras
        Bar(mem, 12, 84, kW - 24, 12, d.cpuPct / 100.f, RGB(74, 222, 128));
        Bar(mem, 12, 102, kW - 24, 12, static_cast<float>(d.memMB) / 16384.f, RGB(96, 165, 250));

        // Legendas das barras
        ::SetTextColor(mem, RGB(100, 116, 139));
        RECT rl{ 12, 118, kW - 10, kH - 4 };
        ::DrawTextW(mem, L"uso de CPU e memória", -1, &rl, DT_LEFT | DT_SINGLELINE);

        ::SelectObject(mem, oldF);
        ::DeleteObject(big); ::DeleteObject(med); ::DeleteObject(sml);

        ::BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        ::SelectObject(mem, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(mem);
        ::EndPaint(h, &ps);
    }

    HWND hwnd_ = nullptr;
    WidgetData data_{};
    BYTE opacity_ = 235;
    std::mutex m_;
};

} // namespace nt
