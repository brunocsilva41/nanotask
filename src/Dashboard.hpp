#pragma once
// Dashboard.hpp — painel funcional do NanoTask (Win32 nativo, criado sob demanda).
// Abre no clique da bandeja; no fechar, tudo é destruído e a RAM é devolvida.
// Abas: Processos | Portas | Sistema | Ações (Módulos 1–5 do MVP).
#include "Foundation.hpp"
#include "Settings.hpp"
#include "Safety.hpp"
#include "NativeSysCalls.hpp"
#include "TempProviders.hpp" // ComPtr? não — RAII próprio abaixo
#include <windows.h>
#include <commctrl.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace nt {

struct DashCfg {
    double kwhPrice = 0.85;
    int tdpWatts = 65;
    bool confirmKill = true;
    bool protectCritical = true;
};

// Janela de gráfico: desenha o histórico de CPU do Blackbox (últimos 5 min).
class NanoGraph {
public:
    static ATOM Register(HINSTANCE h) {
        WNDCLASSEXW wx{}; wx.cbSize = sizeof(wx);
        wx.lpfnWndProc = &NanoGraph::WndProcStatic;
        wx.hInstance = h;
        wx.lpszClassName = L"NanoGraph";
        wx.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        return ::RegisterClassExW(&wx);
    }
    static LRESULT CALLBACK WndProcStatic(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
            ::SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto box = reinterpret_cast<Blackbox*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_PAINT && box) { PaintGraph(h, *box); return 0; }
        if (m == WM_ERASEBKGND) return 1;
        return ::DefWindowProcW(h, m, w, l);
    }
    static void PaintGraph(HWND h, const Blackbox& box) {
        PAINTSTRUCT ps{};
        HDC hdc = ::BeginPaint(h, &ps);
        RECT rc{}; ::GetClientRect(h, &rc);
        HDC mem = ::CreateCompatibleDC(hdc);
        HBITMAP bmp = ::CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldB = static_cast<HBITMAP>(::SelectObject(mem, bmp));
        HBRUSH bg = ::CreateSolidBrush(RGB(11, 15, 25));
        ::FillRect(mem, &rc, bg); ::DeleteObject(bg);
        auto snap = box.Snapshot();
        if (!snap.empty()) {
            HPEN pen = ::CreatePen(PS_SOLID, 2, RGB(74, 222, 128));
            HPEN oldP = static_cast<HPEN>(::SelectObject(mem, pen));
            const int n = static_cast<int>(snap.size());
            int prevX = 0, prevY = rc.bottom - 8;
            for (int i = 0; i < n; ++i) {
                int x = (rc.right * i) / (n > 1 ? (n - 1) : 1);
                float v = snap[static_cast<size_t>(i)].cpuPct;
                if (v < 0.f) v = 0.f; if (v > 100.f) v = 100.f;
                int y = rc.bottom - 8 - static_cast<int>((rc.bottom - 16) * (v / 100.f));
                if (i) { ::MoveToEx(mem, prevX, prevY, nullptr); ::LineTo(mem, x, y); }
                prevX = x; prevY = y;
            }
            ::SelectObject(mem, oldP); ::DeleteObject(pen);
            HFONT f = ::CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            HFONT of = static_cast<HFONT>(::SelectObject(mem, f));
            ::SetBkMode(mem, TRANSPARENT); ::SetTextColor(mem, RGB(100, 116, 139));
            wchar_t cap[64]{};
            ::swprintf_s(cap, L"CPU agora: %.0f%% — últimos %d min",
                static_cast<double>(snap.back().cpuPct), (n + 59) / 60);
            ::TextOutW(mem, 8, 4, cap, ::lstrlenW(cap));
            ::SelectObject(mem, of); ::DeleteObject(f);
        }
        ::BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        ::SelectObject(mem, oldB); ::DeleteObject(bmp); ::DeleteDC(mem);
        ::EndPaint(h, &ps);
    }
};

class Dashboard {
public:
    bool IsOpen() const noexcept { return hwnd_ != nullptr; }

    bool Open(HINSTANCE hInst, Blackbox* box, DashCfg cfg,
              std::function<std::vector<std::wstring>()> getClip) {
        if (hwnd_) { ::ShowWindow(hwnd_, SW_RESTORE); ::SetForegroundWindow(hwnd_); return true; }
        box_ = box; cfg_ = cfg; getClip_ = std::move(getClip);

        static bool comInit = false;
        if (!comInit) {
            INITCOMMONCONTROLSEX ic{ sizeof(ic), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES };
            ::InitCommonControlsEx(&ic);
            NanoGraph::Register(hInst);
            comInit = true;
        }
        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        EnsureClass(hInst);

        hwnd_ = ::CreateWindowExW(0, L"NanoDash", L"NanoTask — Painel",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME,
            CW_USEDEFAULT, CW_USEDEFAULT, 800, 560,
            nullptr, nullptr, hInst, this);
        if (!hwnd_) { LogWin32Error("Dash:Create"); ::CoUninitialize(); return false; }
        // Centraliza
        RECT rw{}; ::GetWindowRect(hwnd_, &rw);
        int sw = ::GetSystemMetrics(SM_CXSCREEN), sh = ::GetSystemMetrics(SM_CYSCREEN);
        ::SetWindowPos(hwnd_, nullptr, (sw - (rw.right - rw.left)) / 2,
            (sh - (rw.bottom - rw.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        font_ = ::CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        brush_ = ::CreateSolidBrush(RGB(17, 24, 39));
        BuildTabs();
        BuildPages();
        ::ShowWindow(hwnd_, SW_SHOW);
        ::SetTimer(hwnd_, kTimer, 2000, nullptr);
        RefreshAll();
        return true;
    }

    void Close() {
        if (!hwnd_) return;
        ::KillTimer(hwnd_, kTimer);
        ::DestroyWindow(hwnd_); // filhos morrem juntos
        hwnd_ = nullptr;
        if (vol_) { vol_->Release(); vol_ = nullptr; }
        if (font_) { ::DeleteObject(font_); font_ = nullptr; }
        if (brush_) { ::DeleteObject(brush_); brush_ = nullptr; }
        ::CoUninitialize();
        // Devolve RAM ao SO imediatamente (Lazy destroy)
        ::SetProcessWorkingSetSize(::GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    }

    LRESULT Handle(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(w);
            ::SetBkColor(dc, RGB(17, 24, 39));
            ::SetTextColor(dc, RGB(226, 232, 240));
            return reinterpret_cast<LRESULT>(brush_);
        }
        case WM_NOTIFY:
            if (reinterpret_cast<LPNMHDR>(l)->code == TCN_SELCHANGE) { ShowPage(); return 0; }
            break;
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(l) == volSlider_) { ApplyVolume(); return 0; }
            break;
        case WM_COMMAND: return OnCommand(static_cast<int>(LOWORD(w)));
        case WM_TIMER: RefreshAll(); return 0;
        case WM_SIZE:
            if (tab_) {
                RECT rc{}; ::GetClientRect(h, &rc);
                ::SetWindowPos(tab_, nullptr, 0, 0, rc.right, rc.bottom, SWP_NOZORDER);
            }
            return 0;
        case WM_CLOSE: Close(); return 0;
        }
        return ::DefWindowProcW(h, m, w, l);
    }

    static LRESULT CALLBACK WndProcStatic(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
            ::SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            WNDCLASSEXW wx{}; wx.cbSize = sizeof(wx); // garante classe registrada
            (void)wx;
        }
        auto self = reinterpret_cast<Dashboard*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
        if (self) return self->Handle(h, m, w, l);
        return ::DefWindowProcW(h, m, w, l);
    }

    static void EnsureClass(HINSTANCE h) {
        WNDCLASSEXW wx{}; wx.cbSize = sizeof(wx);
        wx.lpfnWndProc = &Dashboard::WndProcStatic;
        wx.hInstance = h; wx.lpszClassName = L"NanoDash";
        wx.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wx.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        ::RegisterClassExW(&wx); // ERROR_CLASS_ALREADY_EXISTS ok
    }

private:
    enum : int {
        ID_TAB = 10,
        ID_PROC_LIST, ID_PROC_REFRESH, ID_PROC_KILL, ID_PROC_TREE, ID_PRIO_COMBO, ID_PRIO_APPLY,
        ID_PORT_LIST, ID_PORT_REFRESH, ID_PORT_FREE,
        ID_LBL_TEMP, ID_LBL_CPU, ID_LBL_RAM, ID_GRAPH, ID_WATTS, ID_LBL_ENERGY, ID_ENERGY_CALC,
        ID_B_FLUSH, ID_B_TEMP, ID_B_DNS, ID_B_AWAKE, ID_B_TOP, ID_B_PANIC,
        ID_VOL_SLIDER, ID_LBL_VOL, ID_CLIP_LIST, ID_CLIP_COPY
    };

    static HWND Ctl(HWND p, const wchar_t* cls, const wchar_t* txt, DWORD st,
                    int x, int y, int w, int h, int id, HFONT f) {
        HWND c = ::CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st,
            x, y, w, h, p, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            ::GetModuleHandleW(nullptr), nullptr);
        if (c && f) ::SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
        if (!c) LogWin32Error("Dash:Ctl");
        return c;
    }

    static HWND List(HWND p, int x, int y, int w, int h, int id, HFONT f,
                     const wchar_t* const* cols, const int* widths, int ncols) {
        HWND lv = Ctl(p, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL, x, y, w, h, id, f);
        if (!lv) return nullptr;
        ::SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
        ::SendMessageW(lv, LVM_SETTEXTCOLOR, 0, RGB(226, 232, 240));
        ::SendMessageW(lv, LVM_SETBKCOLOR, 0, RGB(17, 24, 39));
        ::SendMessageW(lv, LVM_SETTEXTBKCOLOR, 0, RGB(17, 24, 39));
        for (int i = 0; i < ncols; ++i) {
            LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            c.fmt = (i == 0) ? LVCFMT_LEFT : LVCFMT_LEFT;
            c.cx = widths[i]; c.pszText = const_cast<LPWSTR>(cols[i]);
            ::SendMessageW(lv, LVM_INSERTCOLUMNW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&c));
        }
        return lv;
    }

    static int AddRow(HWND lv, int row, DWORD pid, const wchar_t* c0, const wchar_t* c1, const wchar_t* c2) {
        LVITEMW it{}; it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = row; it.lParam = static_cast<LPARAM>(pid); it.pszText = const_cast<LPWSTR>(c0);
        int idx = static_cast<int>(::SendMessageW(lv, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&it)));
        if (idx < 0) return -1;
        LVITEMW sub{}; sub.mask = LVIF_TEXT; sub.iItem = idx;
        sub.iSubItem = 1; sub.pszText = const_cast<LPWSTR>(c1);
        ::SendMessageW(lv, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&sub));
        if (c2) {
            sub.iSubItem = 2; sub.pszText = const_cast<LPWSTR>(c2);
            ::SendMessageW(lv, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&sub));
        }
        return idx;
    }

    static DWORD SelectedPid(HWND lv) {
        int i = static_cast<int>(::SendMessageW(lv, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED));
        if (i < 0) return 0;
        LVITEMW it{}; it.mask = LVIF_PARAM; it.iItem = i;
        if (!::SendMessageW(lv, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&it))) return 0;
        return static_cast<DWORD>(it.lParam);
    }

    static std::wstring ExeNameOf(DWORD pid) {
        UniqueHandle snap{ ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if (!snap || snap.get() == INVALID_HANDLE_VALUE) return L"?";
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (!::Process32FirstW(snap.get(), &pe)) return L"?";
        do { if (pe.th32ProcessID == pid) return pe.szExeFile; }
        while (::Process32NextW(snap.get(), &pe));
        return L"?";
    }

    void BuildTabs() {
        tab_ = Ctl(hwnd_, WC_TABCONTROLW, L"", TCS_TABS, 4, 4, 772, 500, ID_TAB, font_);
        const wchar_t* names[] = { L"Processos", L"Portas", L"Sistema", L"Ações" };
        for (int i = 0; i < 4; ++i) {
            TCITEMW t{}; t.mask = TCIF_TEXT; t.pszText = const_cast<LPWSTR>(names[i]);
            ::SendMessageW(tab_, TCM_INSERTITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&t));
        }
    }

    void BuildPages() {
        HINSTANCE h = ::GetModuleHandleW(nullptr);
        for (int i = 0; i < 4; ++i) {
            pages_[i] = ::CreateWindowExW(0, L"STATIC", L"", WS_CHILD,
                12, 34, 756, 460, hwnd_, nullptr, h, nullptr);
            if (pages_[i] && font_)
                ::SendMessageW(pages_[i], WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        // --- Aba 0: Processos ---
        {
            const wchar_t* cols[] = { L"PID", L"Processo", L"RAM (MB)" };
            const int wd[] = { 80, 380, 120 };
            procList_ = List(pages_[0], 8, 8, 740, 330, ID_PROC_LIST, font_, cols, wd, 3);
            int y = 346;
            Ctl(pages_[0], L"BUTTON", L"Atualizar", BS_PUSHBUTTON, 8, y, 110, 30, ID_PROC_REFRESH, font_);
            Ctl(pages_[0], L"BUTTON", L"Finalizar", BS_PUSHBUTTON, 126, y, 110, 30, ID_PROC_KILL, font_);
            Ctl(pages_[0], L"BUTTON", L"Kill em árvore", BS_PUSHBUTTON, 244, y, 130, 30, ID_PROC_TREE, font_);
            prioCombo_ = Ctl(pages_[0], L"COMBOBOX", L"", CBS_DROPDOWNLIST, 382, y, 150, 200, ID_PRIO_COMBO, font_);
            const wchar_t* pr[] = { L"Ocioso", L"Abaixo do normal", L"Normal", L"Acima do normal", L"Alta" };
            for (auto p : pr) ::SendMessageW(prioCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p));
            ::SendMessageW(prioCombo_, CB_SETCURSEL, 2, 0);
            Ctl(pages_[0], L"BUTTON", L"Aplicar prioridade", BS_PUSHBUTTON, 540, y, 150, 30, ID_PRIO_APPLY, font_);
        }
        // --- Aba 1: Portas ---
        {
            const wchar_t* cols[] = { L"Porta", L"PID", L"Processo" };
            const int wd[] = { 100, 100, 480 };
            portList_ = List(pages_[1], 8, 8, 740, 330, ID_PORT_LIST, font_, cols, wd, 3);
            Ctl(pages_[1], L"BUTTON", L"Atualizar", BS_PUSHBUTTON, 8, 346, 110, 30, ID_PORT_REFRESH, font_);
            Ctl(pages_[1], L"BUTTON", L"Liberar porta (mata processo)", BS_PUSHBUTTON, 126, 346, 260, 30, ID_PORT_FREE, font_);
        }
        // --- Aba 2: Sistema ---
        {
            lblTemp_ = Ctl(pages_[2], L"STATIC", L"Temperatura: —", SS_LEFT, 8, 8, 360, 26, ID_LBL_TEMP, font_);
            lblCpu_ = Ctl(pages_[2], L"STATIC", L"CPU: —", SS_LEFT, 8, 38, 360, 26, ID_LBL_CPU, font_);
            lblRam_ = Ctl(pages_[2], L"STATIC", L"RAM: —", SS_LEFT, 380, 8, 360, 26, ID_LBL_RAM, font_);
            graph_ = ::CreateWindowExW(0, L"NanoGraph", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                8, 70, 740, 220, pages_[2], nullptr, h, box_);
            Ctl(pages_[2], L"STATIC", L"TDP (W):", SS_LEFT, 8, 300, 70, 26, -1, font_);
            wattsEdit_ = Ctl(pages_[2], L"EDIT", L"65", ES_NUMBER | WS_BORDER, 80, 300, 80, 26, ID_WATTS, font_);
            Ctl(pages_[2], L"BUTTON", L"Calcular energia", BS_PUSHBUTTON, 170, 300, 150, 28, ID_ENERGY_CALC, font_);
            lblEnergy_ = Ctl(pages_[2], L"STATIC", L"Custo/hora: —", SS_LEFT, 8, 332, 740, 60, ID_LBL_ENERGY, font_);
        }
        // --- Aba 3: Ações + QoL ---
        {
            int x = 8, y = 8;
            Ctl(pages_[3], L"BUTTON", L"RAM Flush (Standby)", BS_PUSHBUTTON, x, y, 220, 32, ID_B_FLUSH, font_);
            Ctl(pages_[3], L"BUTTON", L"Limpar pastas Temp", BS_PUSHBUTTON, x + 232, y, 220, 32, ID_B_TEMP, font_);
            Ctl(pages_[3], L"BUTTON", L"Flush DNS", BS_PUSHBUTTON, x + 464, y, 200, 32, ID_B_DNS, font_);
            y += 42;
            awakeBtn_ = Ctl(pages_[3], L"BUTTON", L"Keep-Awake: OFF", BS_PUSHBUTTON, x, y, 220, 32, ID_B_AWAKE, font_);
            Ctl(pages_[3], L"BUTTON", L"Fixar janela ativa no topo", BS_PUSHBUTTON, x + 232, y, 220, 32, ID_B_TOP, font_);
            Ctl(pages_[3], L"BUTTON", L"PANIC (reinicia Explorer)", BS_PUSHBUTTON, x + 464, y, 200, 32, ID_B_PANIC, font_);
            y += 42;
            Ctl(pages_[3], L"STATIC", L"Volume geral:", SS_LEFT, x, y + 4, 110, 26, -1, font_);
            volSlider_ = Ctl(pages_[3], L"msctls_trackbar32", L"", TBS_HORZ | TBS_AUTOTICKS, x + 120, y, 400, 32, ID_VOL_SLIDER, font_);
            ::SendMessageW(volSlider_, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
            lblVol_ = Ctl(pages_[3], L"STATIC", L"—", SS_LEFT, x + 530, y + 4, 120, 26, ID_LBL_VOL, font_);
            y += 42;
            Ctl(pages_[3], L"STATIC", L"Clipboard (texto, 20 últimos):", SS_LEFT, x, y, 300, 26, -1, font_);
            Ctl(pages_[3], L"BUTTON", L"Colar selecionado de volta", BS_PUSHBUTTON, x + 460, y - 2, 220, 30, ID_CLIP_COPY, font_);
            clipList_ = Ctl(pages_[3], L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY, x, y + 26, 660, 150, ID_CLIP_LIST, font_);
            InitVolume();
        }
        ShowPage();
    }

    void ShowPage() {
        int cur = static_cast<int>(::SendMessageW(tab_, TCM_GETCURSEL, 0, 0));
        for (int i = 0; i < 4; ++i)
            ::ShowWindow(pages_[i], (i == cur) ? SW_SHOW : SW_HIDE);
        RefreshAll();
    }

    // ---- refresh ----
    void RefreshAll() {
        if (!hwnd_) return;
        RefreshProcs(); RefreshPorts(); RefreshSys(); RefreshClip(); RefreshVolLabel();
        if (graph_) ::InvalidateRect(graph_, nullptr, FALSE);
    }

    void RefreshProcs() {
        if (!procList_ || !::IsWindowVisible(pages_[0])) return;
        ::SendMessageW(procList_, LVM_DELETEALLITEMS, 0, 0);
        UniqueHandle snap{ ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if (!snap || snap.get() == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (!::Process32FirstW(snap.get(), &pe)) return;
        int row = 0;
        wchar_t pid[16]{}, mem[32]{};
        do {
            HANDLE hp = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            SIZE_T ws = 0;
            if (hp) {
                PROCESS_MEMORY_COUNTERS pmc{};
                if (::GetProcessMemoryInfo(hp, &pmc, sizeof(pmc))) ws = pmc.WorkingSetSize;
                ::CloseHandle(hp);
            }
            ::swprintf_s(pid, L"%u", static_cast<unsigned>(pe.th32ProcessID));
            ::swprintf_s(mem, L"%llu", static_cast<unsigned long long>(ws >> 20));
            AddRow(procList_, row++, pe.th32ProcessID, pid, pe.szExeFile, mem);
            if (row > 400) break; // trava de sanidade
        } while (::Process32NextW(snap.get(), &pe));
    }

    void RefreshPorts() {
        if (!portList_ || !::IsWindowVisible(pages_[1])) return;
        ::SendMessageW(portList_, LVM_DELETEALLITEMS, 0, 0);
        DWORD sz = 0;
        ::GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        if (sz < 4096) sz = 4096;
        std::vector<BYTE> buf(static_cast<size_t>(sz));
        if (::GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) return;
        auto* t = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buf.data());
        struct Row { USHORT port; DWORD pid; std::wstring exe; };
        std::vector<Row> rows;
        for (DWORD i = 0; i < t->dwNumEntries && i < 500; ++i) {
            USHORT p = ::ntohs(static_cast<u_short>(t->table[i].dwLocalPort));
            rows.push_back({ p, t->table[i].dwOwningPid, ExeNameOf(t->table[i].dwOwningPid) });
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.port < b.port; });
        wchar_t ps[16]{}, id[16]{};
        int row = 0;
        for (auto& r : rows) {
            ::swprintf_s(ps, L"%u", static_cast<unsigned>(r.port));
            ::swprintf_s(id, L"%u", static_cast<unsigned>(r.pid));
            AddRow(portList_, row++, r.pid, ps, id, r.exe.c_str());
        }
    }

    void RefreshSys() {
        if (!lblTemp_) return;
        float cpu = 0.f; uint64_t mem = 0;
        if (box_) {
            if (auto s = box_->Snapshot(); !s.empty()) {
                cpu = s.back().cpuPct; mem = s.back().memUsedMB;
            }
        }
        wchar_t b[128]{};
        ::swprintf_s(b, L"CPU: %.0f%%", static_cast<double>(cpu));
        ::SetWindowTextW(lblCpu_, b);
        ::swprintf_s(b, L"RAM em uso: %llu MB", static_cast<unsigned long long>(mem));
        ::SetWindowTextW(lblRam_, b);
        // Temperatura: leitura rápida (NVML é barata; WMI pode custar ~100ms — ok no timer de 2s)
        static MultiTempProvider temps;
        if (auto t = temps.ReadCelsius())
            ::swprintf_s(b, L"Temperatura: %.0f C  (via %S)", static_cast<double>(*t), temps.Source().data());
        else
            ::swprintf_s(b, L"Temperatura: indisponível (sem sensor)");
        ::SetWindowTextW(lblTemp_, b);
    }

    void RefreshClip() {
        if (!clipList_ || !getClip_) return;
        auto items = getClip_();
        LRESULT count = ::SendMessageW(clipList_, LB_GETCOUNT, 0, 0);
        if (count == static_cast<LRESULT>(items.size())) return; // sem mudança
        ::SendMessageW(clipList_, LB_RESETCONTENT, 0, 0);
        for (auto& s : items) {
            std::wstring preview = s.substr(0, 80);
            for (auto& c : preview) if (c == L'\r' || c == L'\n') c = L' ';
            ::SendMessageW(clipList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preview.c_str()));
        }
    }

    // ---- comandos ----
    LRESULT OnCommand(int id) {
        switch (id) {
        case ID_PROC_REFRESH: RefreshProcs(); break;
        case ID_PROC_KILL: {
            DWORD pid = SelectedPid(procList_);
            if (!pid) { Msg(L"Selecione um processo."); break; }
            if (!Confirm(L"Finalizar o processo?")) break;
            if (safety::AuthorizeKill(pid, cfg_.protectCritical) && native::KillOne(pid))
                Msg(L"Processo finalizado.");
            else Msg(L"Bloqueado ou falha (ver log).");
            RefreshProcs();
            break;
        }
        case ID_PROC_TREE: {
            DWORD pid = SelectedPid(procList_);
            if (!pid) { Msg(L"Selecione um processo."); break; }
            if (!Confirm(L"Encerrar a ÁRVORE inteira (processo + filhos)?")) break;
            if (!safety::AuthorizeKill(pid, cfg_.protectCritical)) { Msg(L"PID protegido."); break; }
            int n = native::ProcessTreeKill(pid);
            wchar_t b[64]{}; ::swprintf_s(b, L"%d processo(s) encerrado(s).", n);
            Msg(b); RefreshProcs();
            break;
        }
        case ID_PRIO_APPLY: {
            DWORD pid = SelectedPid(procList_);
            if (!pid) { Msg(L"Selecione um processo."); break; }
            static const DWORD map[] = { IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS,
                NORMAL_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS };
            int sel = static_cast<int>(::SendMessageW(prioCombo_, CB_GETCURSEL, 0, 0));
            if (sel < 0 || sel > 4) sel = 2;
            if (!safety::AuthorizeKill(pid, cfg_.protectCritical)) { Msg(L"PID protegido."); break; }
            Msg(native::SetPriorityAndAffinity(pid, map[sel], 0) ? L"Prioridade aplicada." : L"Falha ao aplicar.");
            break;
        }
        case ID_PORT_REFRESH: RefreshPorts(); break;
        case ID_PORT_FREE: {
            DWORD pid = SelectedPid(portList_);
            if (!pid) { Msg(L"Selecione uma porta."); break; }
            if (!Confirm(L"Matar o processo dono da porta?")) break;
            if (safety::AuthorizeKill(pid, cfg_.protectCritical) && native::KillOne(pid))
                Msg(L"Porta liberada.");
            else Msg(L"Bloqueado ou falha.");
            RefreshPorts();
            break;
        }
        case ID_ENERGY_CALC: CalcEnergy(); break;
        case ID_B_FLUSH:
            Msg(native::PurgeStandbyList() ? L"Standby esvaziada." : L"Falha — rode como admin.");
            break;
        case ID_B_TEMP:
            native::CleanTempDirsAsync().detach();
            Msg(L"Limpeza iniciada em background.");
            break;
        case ID_B_DNS:
            Msg(native::FlushDns() ? L"DNS liberado." : L"Falha no flush DNS.");
            break;
        case ID_B_AWAKE: {
            awake_ = !awake_;
            native::KeepAwake(awake_);
            ::SetWindowTextW(awakeBtn_, awake_ ? L"Keep-Awake: ON" : L"Keep-Awake: OFF");
            break;
        }
        case ID_B_TOP: {
            HWND fg = ::GetForegroundWindow();
            if (fg && fg != hwnd_ && ::SetWindowPos(fg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE))
                Msg(L"Janela ativa fixada no topo.");
            else Msg(L"Nenhuma janela válida.");
            break;
        }
        case ID_B_PANIC:
            if (Confirm(L"PANIC: encerra e revive o Explorer agora?")) {
                Msg(native::PanicButton() ? L"Explorer reiniciado." : L"Falha no panic.");
            }
            break;
        case ID_CLIP_COPY: {
            if (!getClip_) break;
            LRESULT sel = ::SendMessageW(clipList_, LB_GETCURSEL, 0, 0);
            auto items = getClip_();
            if (sel < 0 || static_cast<size_t>(sel) >= items.size()) { Msg(L"Selecione um item."); break; }
            const std::wstring& s = items[static_cast<size_t>(sel)];
            if (!::OpenClipboard(hwnd_)) { Msg(L"Clipboard ocupado."); break; }
            ::EmptyClipboard();
            size_t bytes = (s.size() + 1) * sizeof(wchar_t);
            HGLOBAL g = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (g) {
                void* p = ::GlobalLock(g);
                if (p) { ::memcpy(p, s.c_str(), bytes); ::GlobalUnlock(g); }
                ::SetClipboardData(CF_UNICODETEXT, g); // OS assume o handle
            }
            ::CloseClipboard();
            Msg(L"Copiado de volta.");
            break;
        }
        default: break;
        }
        return 0;
    }

    bool Confirm(const wchar_t* q) {
        if (!cfg_.confirmKill) return true;
        return ::MessageBoxW(hwnd_, q, L"NanoTask — confirmar", MB_YESNO | MB_ICONQUESTION) == IDYES;
    }

    void Msg(const wchar_t* t) {
        ::MessageBoxW(hwnd_, t, L"NanoTask", MB_OK | MB_ICONINFORMATION);
    }

    void CalcEnergy() {
        wchar_t wb[16]{};
        ::GetWindowTextW(wattsEdit_, wb, 16);
        int w = _wtoi(wb);
        if (w <= 0 || w > 2000) { Msg(L"TDP inválido (1–2000 W)."); return; }
        double hora = w / 1000.0 * cfg_.kwhPrice;
        wchar_t b[256]{};
        ::swprintf_s(b, L"Custo/hora: R$ %.3f   |   Dia: R$ %.2f   |   Mês (30d): R$ %.2f",
            hora, hora * 24.0, hora * 24.0 * 30.0);
        ::SetWindowTextW(lblEnergy_, b);
    }

    void InitVolume() {
        IMMDeviceEnumerator* en = nullptr;
        HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
            CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en));
        if (FAILED(hr) || !en) return;
        IMMDevice* dev = nullptr;
        hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        en->Release();
        if (FAILED(hr) || !dev) return;
        hr = dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&vol_));
        dev->Release();
        if (FAILED(hr) || !vol_) { vol_ = nullptr; return; }
        float v = 0.5f;
        if (SUCCEEDED(vol_->GetMasterVolumeLevelScalar(&v)))
            ::SendMessageW(volSlider_, TBM_SETPOS, TRUE, static_cast<LPARAM>(v * 100.f));
        RefreshVolLabel();
    }

    void ApplyVolume() {
        if (!vol_) return;
        int p = static_cast<int>(::SendMessageW(volSlider_, TBM_GETPOS, 0, 0));
        vol_->SetMasterVolumeLevelScalar(static_cast<float>(p) / 100.f, nullptr);
        RefreshVolLabel();
    }

    void RefreshVolLabel() {
        if (!lblVol_ || !vol_) return;
        float v = 0.f;
        if (SUCCEEDED(vol_->GetMasterVolumeLevelScalar(&v))) {
            wchar_t b[32]{};
            ::swprintf_s(b, L"%d%%", static_cast<int>(v * 100.f));
            ::SetWindowTextW(lblVol_, b);
        }
    }

    HWND hwnd_ = nullptr, tab_ = nullptr;
    HWND pages_[4]{};
    HWND procList_ = nullptr, prioCombo_ = nullptr;
    HWND portList_ = nullptr;
    HWND lblTemp_ = nullptr, lblCpu_ = nullptr, lblRam_ = nullptr, graph_ = nullptr;
    HWND wattsEdit_ = nullptr, lblEnergy_ = nullptr;
    HWND awakeBtn_ = nullptr, volSlider_ = nullptr, lblVol_ = nullptr, clipList_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH brush_ = nullptr;
    IAudioEndpointVolume* vol_ = nullptr;
    bool awake_ = false;
    Blackbox* box_ = nullptr;
    DashCfg cfg_{};
    std::function<std::vector<std::wstring>()> getClip_{};
    static constexpr UINT kTimer = 7;
};

} // namespace nt
