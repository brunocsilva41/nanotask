#pragma once
#include "Foundation.hpp"
#include <windows.h>
#include <pdh.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <array>
#include <vector>
#include <mutex>
#include <thread>
#include <stop_token>
#include <optional>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")

namespace nt {

struct Sample {
    SYSTEMTIME t{};
    float cpuPct = 0, diskPct = 0;
    uint64_t memUsedMB = 0;
    DWORD topPid = 0;
    wchar_t topName[32]{};
};

class Blackbox {
    std::array<Sample, 300> ring_{};
    size_t head_ = 0, count_ = 0;
    mutable std::mutex m_;
public:
    void Push(Sample s) {
        std::lock_guard g(m_);
        ring_[head_] = s;
        head_ = (head_ + 1) % ring_.size();
        if (count_ < ring_.size()) ++count_;
    }
    std::vector<Sample> Snapshot() const {
        std::lock_guard g(m_);
        std::vector<Sample> out; out.reserve(count_);
        for (size_t i = 0; i < count_; ++i)
            out.push_back(ring_[(head_ + ring_.size() - count_ + i) % ring_.size()]);
        return out;
    }
};

struct ITempProvider {
    virtual ~ITempProvider() = default;
    virtual std::optional<float> ReadCelsius() = 0;
};

class TelemetryCollector {
    Blackbox& box_;
    PDH_HQUERY q_ = nullptr;
    PDH_HCOUNTER cpu_ = nullptr, disk_ = nullptr, availMB_ = nullptr;
    std::jthread worker_;

    static void TopProcByWS(DWORD& pid, wchar_t (&name)[32]) {
        pid = 0; name[0] = 0;
        HANDLE hsnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hsnap == INVALID_HANDLE_VALUE) return;
        UniqueHandle snap{hsnap};
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        SIZE_T best = 0;
        if (::Process32FirstW(snap.get(), &pe)) do {
            HANDLE hp = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (hp) {
                UniqueHandle p{hp};
                PROCESS_MEMORY_COUNTERS pmc{};
                if (::GetProcessMemoryInfo(p.get(), &pmc, sizeof(pmc)) && pmc.WorkingSetSize > best) {
                    best = pmc.WorkingSetSize; pid = pe.th32ProcessID;
                    wcsncpy_s(name, pe.szExeFile, _TRUNCATE);
                }
            }
        } while (::Process32NextW(snap.get(), &pe));
    }

    void Loop(std::stop_token st) {
        if (::PdhOpenQueryW(nullptr, 0, &q_) != ERROR_SUCCESS) { LogWin32Error("PdhOpen"); return; }
        ::PdhAddCounterW(q_, L"\\Processor(_Total)\\% Processor Time", 0, &cpu_);
        ::PdhAddCounterW(q_, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &disk_);
        ::PdhAddCounterW(q_, L"\\Memory\\Available MBytes", 0, &availMB_);
        ::PdhCollectQueryData(q_);
        MEMORYSTATUSEX mem{sizeof(mem)};
        PDH_FMT_COUNTERVALUE v{};
        while (!st.stop_requested()) {
            Sample s{}; ::GetSystemTime(&s.t);
            if (::PdhCollectQueryData(q_) == ERROR_SUCCESS) {
                if (::PdhGetFormattedCounterValue(cpu_, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS)
                    s.cpuPct = static_cast<float>(v.doubleValue);
                if (::PdhGetFormattedCounterValue(disk_, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS)
                    s.diskPct = static_cast<float>(v.doubleValue);
                if (::PdhGetFormattedCounterValue(availMB_, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS) {
                    ::GlobalMemoryStatusEx(&mem);
                    s.memUsedMB = (mem.ullTotalPhys >> 20) - static_cast<uint64_t>(v.doubleValue);
                }
            }
            TopProcByWS(s.topPid, s.topName);
            box_.Push(s);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        ::PdhCloseQuery(q_); q_ = nullptr;
    }

public:
    explicit TelemetryCollector(Blackbox& b) : box_(b) {}
    void Start() { worker_ = std::jthread([this](std::stop_token st){ Loop(st); }); }
};

// --- Automacao: presets afinidade/prioridade ---
struct Rule {
    std::wstring exe;
    DWORD priority = NORMAL_PRIORITY_CLASS;
    DWORD_PTR affinityMask = 0;
    bool ecoQoS = false;
};

inline std::vector<Rule> LoadRules(std::filesystem::path json) {
    std::vector<Rule> out;
    std::wifstream f(json); if (!f) return out;
    std::wstringstream ss; ss << f.rdbuf();
    std::wstring s = ss.str();
    size_t p = 0;
    while ((p = s.find(L'{', p)) != std::wstring::npos) {
        size_t e = s.find(L'}', p);
        if (e == std::wstring::npos) break;
        Rule r;
        auto grab = [&](const wchar_t* k) -> std::wstring {
            size_t a = s.find(k, p);
            if (a == std::wstring::npos || a > e) return L"";
            a = s.find(L'"', a + wcslen(k) + 1);
            if (a == std::wstring::npos || a > e) return L"";
            size_t b = s.find(L'"', a + 1);
            return s.substr(a + 1, b - a - 1);
        };
        r.exe = grab(L"exe");
        if (r.exe.empty()) { p = e + 1; continue; }
        std::wstring pr = grab(L"priority");
        if (pr == L"idle") r.priority = IDLE_PRIORITY_CLASS;
        else if (pr == L"high") r.priority = HIGH_PRIORITY_CLASS;
        else if (pr == L"below") r.priority = BELOW_NORMAL_PRIORITY_CLASS;
        else if (pr == L"above") r.priority = ABOVE_NORMAL_PRIORITY_CLASS;
        r.ecoQoS = s.find(L"\"eco\":true", p) != std::wstring::npos
                && s.find(L"\"eco\":true", p) < e;
        for (auto& c : r.exe) c = towlower(c);
        out.push_back(std::move(r)); p = e + 1;
    }
    return out;
}

inline void ApplyEcoQoS(HANDLE h, bool on) {
    using NtSetFn = LONG (NTAPI*)(HANDLE, ULONG, PVOID, ULONG);
    static auto NtSet = reinterpret_cast<NtSetFn>(
        ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess"));
    if (!NtSet) return;
    struct Throttle { ULONG Version = 3, ControlMask = 0, StateMask = 0; } st{};
    st.ControlMask = st.StateMask = on ? 0x1 : 0;
    NtSet(h, 33, &st, sizeof(st));
}

class RuleEngine {
    std::unordered_map<std::wstring, Rule> rules_;
    std::jthread worker_;
    void Loop(std::stop_token st) {
        while (!st.stop_requested()) {
            HANDLE hsnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hsnap != INVALID_HANDLE_VALUE) {
                UniqueHandle snap{hsnap};
                PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
                if (::Process32FirstW(snap.get(), &pe)) do {
                    std::wstring n(pe.szExeFile);
                    for (auto& c : n) c = towlower(c);
                    auto it = rules_.find(n);
                    if (it != rules_.end()) {
                        HANDLE hp = ::OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe.th32ProcessID);
                        if (hp) {
                            UniqueHandle h{hp};
                            ::SetPriorityClass(h.get(), it->second.priority);
                            if (it->second.affinityMask) ::SetProcessAffinityMask(h.get(), it->second.affinityMask);
                            if (it->second.ecoQoS) ApplyEcoQoS(h.get(), true);
                        }
                    }
                } while (::Process32NextW(snap.get(), &pe));
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
public:
    void SetRules(std::vector<Rule> v) {
        rules_.clear();
        for (auto& r : v) rules_[r.exe] = r;
    }
    void Start() { worker_ = std::jthread([this](std::stop_token st){ Loop(st); }); }
};

class ThermalGuard {
    std::function<void(float)> cb_;
    bool latched_ = false;
public:
    explicit ThermalGuard(std::function<void(float)> cb) : cb_(std::move(cb)) {}
    void Poll(float tempC) {
        if (tempC > 80.f && !latched_) { latched_ = true; if (cb_) cb_(tempC); }
        else if (tempC < 75.f) latched_ = false;
    }
};

} // namespace nt
