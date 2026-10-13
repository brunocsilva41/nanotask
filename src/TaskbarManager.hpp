#pragma once
// TaskbarManager.hpp — os 3 modos de exibição (fantasma/fixo/minimizado).
// Fantasma: HWND_MESSAGE, sem botão na taskbar. Fixo: janela visível com
// WS_EX_APPWINDOW. Minimizado: auto-hide no SIZE_MINIMIZE.
#include "Settings.hpp"

namespace nt {

class TaskbarManager {
    TaskbarMode mode_ = TaskbarMode::Ghost;
    HWND hwnd_ = nullptr; // janela da UI/mini-dashboard (processo UI), pode ser nullptr
public:
    void SetMode(TaskbarMode m) {
        mode_ = m;
        Apply();
        // Persiste para o instalador/registry ler no boot
        HKEY k = nullptr;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\NanoTask",
                0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            DWORD v = static_cast<DWORD>(m);
            ::RegSetValueExW(k, L"TaskbarMode", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
            ::RegCloseKey(k);
        }
    }
    TaskbarMode Mode() const noexcept { return mode_; }

    void AttachWindow(HWND h) { hwnd_ = h; Apply(); }

    // Chamado no WM_SIZE da janela: minimizou → volta pra bandeja (modo 2)
    void OnSize(WPARAM wp) {
        if (mode_ == TaskbarMode::Minimized && wp == SIZE_MINIMIZE && hwnd_)
            ::ShowWindow(hwnd_, SW_HIDE);
    }

    void ToggleVisible() {
        if (!hwnd_) return;
        if (::IsWindowVisible(hwnd_)) ::ShowWindow(hwnd_, SW_HIDE);
        else { ::ShowWindow(hwnd_, SW_RESTORE); ::SetForegroundWindow(hwnd_); }
    }

private:
    void Apply() {
        if (!hwnd_ || !::IsWindow(hwnd_)) return;
        LONG_PTR ex = ::GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        if (mode_ == TaskbarMode::Ghost) {
            ::ShowWindow(hwnd_, SW_HIDE);
        } else if (mode_ == TaskbarMode::Pinned) {
            ::SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex | WS_EX_APPWINDOW);
            ::ShowWindow(hwnd_, SW_SHOW);
        } else { // Minimized
            ::ShowWindow(hwnd_, SW_SHOWMINIMIZED);
        }
    }
};

} // namespace nt
