#pragma once
#include "Foundation.hpp"
#include <windows.h>

class UiLauncher {
public:
    static void Show(HWND) {
        if (s_uiProc && ::WaitForSingleObject(s_uiProc, 0) == WAIT_TIMEOUT) {
            HWND h = ::FindWindowW(L"NanoTaskUI", nullptr);
            if (h) { ::SetForegroundWindow(h); ::ShowWindow(h, SW_RESTORE); }
            return;
        }
        // Snapshot Blackbox via FileMapping seria aqui (Fase 3 integracao).
        // MVP: lanca NanoTask.UI.exe separado para manter host <5MB.
        wchar_t cmd[] = L"NanoTask.UI.exe";
        STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
        if (!::CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
            nt::LogWin32Error("UiLauncher:CreateProcess");
            ::MessageBoxW(nullptr, L"NanoTask.UI.exe nao encontrado (Fase 4)", L"NanoTask", MB_OK);
            return;
        }
        HANDLE hJob = ::CreateJobObjectW(nullptr, nullptr);
        if (hJob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jli{};
            jli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            ::SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jli, sizeof(jli));
            ::AssignProcessToJobObject(hJob, pi.hProcess);
        }
        ::CloseHandle(pi.hThread);
        if (s_uiProc) ::CloseHandle(s_uiProc);
        s_uiProc = pi.hProcess;
    }

    static void OnUiExited() {
        if (!s_uiProc) return;
        ::CloseHandle(s_uiProc); s_uiProc = nullptr;
        ::SetProcessWorkingSetSize(::GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    }
private:
    inline static HANDLE s_uiProc = nullptr;
};
