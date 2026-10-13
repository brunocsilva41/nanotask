#pragma once
// Winsock2 ANTES de windows.h (via Foundation) para evitar conflito winsock.h.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windns.h>
#include "Foundation.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <windns.h>
#include <concepts>
#include <span>
#include <ranges>
#include <vector>
#include <string>
#include <filesystem>
#include <thread>
#include <stop_token>
#include <array>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "dnsapi.lib")

namespace nt::native {

using NtSuspendProc_t = LONG (NTAPI*)(HANDLE);
using NtResumeProc_t  = LONG (NTAPI*)(HANDLE);
using NtSetSysInfo_t  = LONG (NTAPI*)(INT, PVOID, ULONG);

inline HMODULE Ntdll() noexcept {
    static HMODULE m = ::GetModuleHandleW(L"ntdll.dll");
    return m;
}

inline bool NtSuspend(DWORD pid) {
    static auto f = reinterpret_cast<NtSuspendProc_t>(::GetProcAddress(Ntdll(), "NtSuspendProcess"));
    if (!f) return false;
    UniqueHandle p{::OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid)};
    if (!p) { LogWin32Error("NtSuspend:Open"); return false; }
    if (f(p.get()) != 0) { LogWin32Error("NtSuspend"); return false; }
    return true;
}

inline bool NtResume(DWORD pid) {
    static auto f = reinterpret_cast<NtResumeProc_t>(::GetProcAddress(Ntdll(), "NtResumeProcess"));
    if (!f) return false;
    UniqueHandle p{::OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid)};
    if (!p) { LogWin32Error("NtResume:Open"); return false; }
    if (f(p.get()) != 0) { LogWin32Error("NtResume"); return false; }
    return true;
}

// SystemMemoryListInformation=80, MemoryPurgeStandbyList=4. Exige admin.
inline bool PurgeStandbyList() {
    static auto f = reinterpret_cast<NtSetSysInfo_t>(::GetProcAddress(Ntdll(), "NtSetSystemInformation"));
    if (!f) return false;
    INT cmd = 4;
    if (f(80, &cmd, sizeof(cmd)) != 0) { LogWin32Error("PurgeStandbyList"); return false; }
    return true;
}

inline bool EnableDebugPrivilege() {
    HANDLE tok = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &tok)) {
        LogWin32Error("DbgPriv:Token"); return false;
    }
    UniqueHandle t{tok};
    LUID luid{};
    if (!::LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) { LogWin32Error("DbgPriv:Lookup"); return false; }
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0] = {luid, SE_PRIVILEGE_ENABLED};
    if (!::AdjustTokenPrivileges(t.get(), FALSE, &tp, 0, nullptr, nullptr)) { LogWin32Error("DbgPriv:Adjust"); return false; }
    if (::GetLastError() != ERROR_SUCCESS) { LogWin32Error("DbgPriv:NotHeld"); return false; }
    return true;
}

inline bool KillOne(DWORD pid, UINT code = 1) {
    UniqueHandle p{::OpenProcess(PROCESS_TERMINATE, FALSE, pid)};
    if (!p) { LogWin32Error("KillOne:Open"); return false; }
    if (!::TerminateProcess(p.get(), code)) { LogWin32Error("KillOne:Term"); return false; }
    return true;
}

inline int ProcessTreeKill(DWORD root) {
    HANDLE hsnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hsnap == INVALID_HANDLE_VALUE) { LogWin32Error("TreeKill:Snap"); return 0; }
    UniqueHandle snap{hsnap};
    struct Node { DWORD pid, ppid; };
    std::vector<Node> all;
    PROCESSENTRY32W pe{sizeof(PROCESSENTRY32W)};
    // Fix: inicializar dwSize corretamente
    pe.dwSize = sizeof(pe);
    if (!::Process32FirstW(snap.get(), &pe)) { LogWin32Error("TreeKill:First"); return 0; }
    do { all.push_back({pe.th32ProcessID, pe.th32ParentProcessID}); }
    while (::Process32NextW(snap.get(), &pe));
    std::vector<DWORD> stack{root}, sub;
    while (!stack.empty()) {
        DWORD cur = stack.back(); stack.pop_back();
        sub.push_back(cur);
        for (auto& n : all) if (n.ppid == cur) stack.push_back(n.pid);
    }
    int killed = 0;
    for (DWORD pid : sub | std::views::reverse)
        if (KillOne(pid)) ++killed;
    return killed;
}

inline int KillByPort(uint16_t port) {
    int killed = 0;
    const DWORD want = ::htons(port);
    auto killPid = [&](DWORD pid) { if (pid > 4) killed += KillOne(pid) ? 1 : 0; };
    DWORD sz = 0;
    ::GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    std::vector<std::byte> buf(std::max<DWORD>(sz, 4096));
    if (::GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
        auto* t = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buf.data());
        for (auto row : std::span(t->table, t->dwNumEntries))
            if (row.dwLocalPort == want) killPid(row.dwOwningPid);
    } else LogWin32Error("KillByPort:TCP");
    sz = static_cast<DWORD>(buf.size());
    ::GetExtendedUdpTable(nullptr, &sz, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    buf.resize(std::max<DWORD>(sz, 4096));
    if (::GetExtendedUdpTable(buf.data(), &sz, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
        auto* u = reinterpret_cast<PMIB_UDPTABLE_OWNER_PID>(buf.data());
        for (auto row : std::span(u->table, u->dwNumEntries))
            if (row.dwLocalPort == want) killPid(row.dwOwningPid);
    } else LogWin32Error("KillByPort:UDP");
    return killed;
}

inline bool SetPriorityAndAffinity(DWORD pid, DWORD prio, DWORD_PTR mask) {
    constexpr std::array<DWORD,6> valid = {IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS,
        NORMAL_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS, REALTIME_PRIORITY_CLASS};
    if (std::ranges::find(valid, prio) == valid.end()) return false;
    UniqueHandle p{::OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pid)};
    if (!p) { LogWin32Error("Prio:Open"); return false; }
    if (!::SetPriorityClass(p.get(), prio)) { LogWin32Error("Prio:Class"); return false; }
    if (mask != 0 && !::SetProcessAffinityMask(p.get(), mask)) { LogWin32Error("Prio:Affinity"); return false; }
    return true;
}

inline bool FlushDns() {
    // Sem API pública de flush total: ipconfig com caminho absoluto fixo
    // (nenhum input de usuário entra na linha de comando).
    wchar_t sys[MAX_PATH]{};
    if (!::GetSystemDirectoryW(sys, MAX_PATH)) { LogWin32Error("FlushDns:SysDir"); return false; }
    std::wstring cmd = std::wstring(sys) + L"\\ipconfig.exe /flushdns";
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        LogWin32Error("FlushDns:CreateProcess"); return false;
    }
    ::WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hThread); ::CloseHandle(pi.hProcess);
    if (code != 0) { RingLog::Instance().Push(code, "FlushDns:exit!=0"); return false; }
    return true;
}

inline std::jthread CleanTempDirsAsync() {
    return std::jthread([](std::stop_token) {
        std::error_code ec;
        std::array<std::filesystem::path,2> dirs = {
            std::filesystem::temp_directory_path(ec),
            std::filesystem::path(L"C:\\Windows\\Temp")};
        for (auto& d : dirs) {
            std::error_code it;
            for (auto e : std::filesystem::directory_iterator(d, it)) {
                if (it) { RingLog::Instance().Push(static_cast<DWORD>(it.value()), "Temp:Iter"); break; }
                std::filesystem::remove_all(e.path(), ec);
                if (ec) RingLog::Instance().Push(static_cast<DWORD>(ec.value()), "Temp:Remove");
            }
        }
    });
}

inline void KeepAwake(bool on) {
    ::SetThreadExecutionState(on
        ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED)
        : ES_CONTINUOUS);
}

inline bool PanicButton() {
    HANDLE hsnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hsnap != INVALID_HANDLE_VALUE) {
        UniqueHandle snap{hsnap};
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (::Process32FirstW(snap.get(), &pe)) do {
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) KillOne(pe.th32ProcessID);
        } while (::Process32NextW(snap.get(), &pe));
    }
    wchar_t sys[MAX_PATH]; ::GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = std::wstring(sys) + L"\\..\\explorer.exe";
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        LogWin32Error("PanicButton:Revive"); return false;
    }
    ::CloseHandle(pi.hThread); ::CloseHandle(pi.hProcess);
    return true;
}

// Window Sniper: WH_MOUSE_LL nao precisa de DLL.
inline HHOOK g_sniperHook = nullptr;
inline LRESULT CALLBACK SniperProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == WM_LBUTTONDOWN) {
        if ((::GetAsyncKeyState(VK_CONTROL) & 0x8000) && (::GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
            auto* ms = reinterpret_cast<PMSLLHOOKSTRUCT>(lp);
            HWND w = ::WindowFromPoint(ms->pt);
            if (w) w = ::GetAncestor(w, GA_ROOT);
            DWORD pid = 0;
            ::GetWindowThreadProcessId(w, &pid);
            if (pid > 4) KillOne(pid);
            return 1;
        }
    }
    return ::CallNextHookEx(g_sniperHook, code, wp, lp);
}
inline bool ArmWindowSniper() {
    if (g_sniperHook) return true;
    g_sniperHook = ::SetWindowsHookExW(WH_MOUSE_LL, SniperProc, nullptr, 0);
    if (!g_sniperHook) { LogWin32Error("Sniper:Hook"); return false; }
    return true;
}
inline void DisarmWindowSniper() {
    if (g_sniperHook) { ::UnhookWindowsHookEx(g_sniperHook); g_sniperHook = nullptr; }
}

} // namespace nt::native
