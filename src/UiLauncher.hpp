#pragma once
// UiLauncher.hpp — Lazy Instantiation da WinUI 3 em processo separado.
// O host fantasma nunca carrega XAML; no clique, lança NanoTask.UI.exe.
// Retorna false se o exe não existir (chamador usa o Dashboard Win32).
#include "Foundation.hpp"
#include <string>
#include <vector>

class UiLauncher {
public:
    // Tenta mostrar a UI WinUI. true = lançada ou trazida à frente.
    static bool Show(HWND) {
        if (s_uiProc && ::WaitForSingleObject(s_uiProc, 0) == WAIT_TIMEOUT) {
            FocusUi(s_uiPid);
            return true;
        }
        if (s_uiProc) { ::CloseHandle(s_uiProc); s_uiProc = nullptr; s_uiPid = 0; }

        wchar_t dir[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, dir, MAX_PATH);
        std::wstring exe(dir);
        if (auto p = exe.find_last_of(L"\\/"); p != std::wstring::npos) exe.resize(p);
        exe += L"\\NanoTask.UI.exe";
        if (::GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES)
            return false; // sem UI instalada -> fallback Dashboard

        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + exe + L"\"";
        if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
            nt::LogWin32Error("UiLauncher:CreateProcess");
            return false;
        }
        HANDLE hJob = ::CreateJobObjectW(nullptr, nullptr);
        if (hJob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jli{};
            jli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            ::SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jli, sizeof(jli));
            ::AssignProcessToJobObject(hJob, pi.hProcess);
        }
        ::CloseHandle(pi.hThread);
        s_uiProc = pi.hProcess;
        s_uiPid = pi.dwProcessId;
        return true;
    }

    static void OnUiExited() {
        if (!s_uiProc) return;
        ::CloseHandle(s_uiProc); s_uiProc = nullptr; s_uiPid = 0;
        ::SetProcessWorkingSetSize(::GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    }

private:
    struct FocusCtx { DWORD pid; HWND found = nullptr; };
    static BOOL CALLBACK EnumWnd(HWND h, LPARAM l) {
        auto* c = reinterpret_cast<FocusCtx*>(l);
        DWORD pid = 0;
        ::GetWindowThreadProcessId(h, &pid);
        if (pid == c->pid && ::IsWindowVisible(h)) { c->found = h; return FALSE; }
        return TRUE;
    }
    static void FocusUi(DWORD pid) {
        FocusCtx ctx{ pid };
        ::EnumWindows(EnumWnd, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.found) {
            ::AllowSetForegroundWindow(pid);
            ::ShowWindow(ctx.found, SW_RESTORE);
            ::SetForegroundWindow(ctx.found);
        }
    }
    inline static HANDLE s_uiProc = nullptr;
    inline static DWORD s_uiPid = 0;
};
