#pragma once
// Safety.hpp — guarda-costas contra o próprio app.
// Toda ação destrutiva (kill/suspend/standby) passa por aqui primeiro.
#include "Foundation.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <string_view>

namespace nt::safety {

// Processos que NUNCA podem ser mortos/suspensos, nem com confirmKill=false.
inline bool IsProtectedImage(std::wstring_view img) noexcept {
    constexpr std::array<std::wstring_view, 8> kBlock = {
        L"csrss.exe", L"wininit.exe", L"services.exe", L"lsass.exe",
        L"smss.exe", L"winlogon.exe", L"system", L"nanotask.exe"};
    for (auto b : kBlock)
        if (_wcsicmp(img.data(), b.data()) == 0) return true;
    return false;
}

inline bool IsCriticalPid(DWORD pid) noexcept {
    if (pid <= 4) return true; // System/Idle
    UniqueHandle snap{::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (!snap || snap.get() == INVALID_HANDLE_VALUE) return true; // fail-closed
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (!::Process32FirstW(snap.get(), &pe)) return true;
    do {
        if (pe.th32ProcessID == pid)
            return IsProtectedImage(pe.szExeFile);
    } while (::Process32NextW(snap.get(), &pe));
    return false; // PID inexistente: KillOne vai falhar com log, sem dano
}

// Retorna true se a ação pode prosseguir. Loga e nega por padrão em dúvida.
inline bool AuthorizeKill(DWORD pid, bool protectCritical = true) {
    if (pid == ::GetCurrentProcessId()) { LogWin32Error("Safety:self-kill bloqueado"); return false; }
    if (protectCritical && IsCriticalPid(pid)) { LogWin32Error("Safety:PID crítico bloqueado"); return false; }
    return true;
}

} // namespace nt::safety
