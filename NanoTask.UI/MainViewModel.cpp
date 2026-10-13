#include "pch.h"
#include "MainViewModel.h"
#include <tlhelp32.h>
#include <psapi.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml::Data;

namespace winrt::NanoTask::UI::implementation
{
    MainViewModel::MainViewModel()
    {
        if (PdhOpenQueryW(nullptr, 0, &q_) == ERROR_SUCCESS)
        {
            PdhAddCounterW(q_, L"\\Processor(_Total)\\% Processor Time", 0, &cpu_);
            PdhAddCounterW(q_, L"\\Memory\\Available MBytes", 0, &avail_);
            PdhCollectQueryData(q_);
        }
        Refresh();
    }

    MainViewModel::~MainViewModel()
    {
        if (q_) { PdhCloseQuery(q_); q_ = nullptr; }
    }

    hstring MainViewModel::CpuTemp() { return cpuTemp_; }
    void MainViewModel::CpuTemp(hstring const& v) { cpuTemp_ = v; Raise(L"CpuTemp"); }
    double MainViewModel::TempValue() { return tempValue_; }
    double MainViewModel::CpuPct() { return cpuPct_; }
    hstring MainViewModel::MemText() { return mem_; }
    hstring MainViewModel::TopProc() { return top_; }
    hstring MainViewModel::PowerText() { return pw_; }

    event_token MainViewModel::PropertyChanged(PropertyChangedEventHandler const& h)
    {
        return ev_.add(h);
    }

    void MainViewModel::PropertyChanged(event_token t) noexcept
    {
        ev_.remove(t);
    }

    void MainViewModel::Raise(std::wstring_view name)
    {
        ev_(*this, PropertyChangedEventArgs(hstring{ name }));
    }

    void MainViewModel::Refresh()
    {
        if (q_ && PdhCollectQueryData(q_) == ERROR_SUCCESS)
        {
            PDH_FMT_COUNTERVALUE v{};
            if (PdhGetFormattedCounterValue(cpu_, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS)
                cpuPct_ = v.doubleValue;
            if (PdhGetFormattedCounterValue(avail_, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS)
            {
                MEMORYSTATUSEX ms{};
                ms.dwLength = sizeof(ms);
                if (GlobalMemoryStatusEx(&ms))
                {
                    auto usedMB = (ms.ullTotalPhys >> 20) - static_cast<uint64_t>(v.doubleValue);
                    wchar_t b[64]{};
                    swprintf_s(b, L"%llu MB em uso", static_cast<unsigned long long>(usedMB));
                    mem_ = b;
                }
            }
        }

        // Top processo por WorkingSet
        HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hsnap != INVALID_HANDLE_VALUE)
        {
            PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
            SIZE_T best = 0;
            wchar_t name[260]{};
            if (Process32FirstW(hsnap, &pe))
            {
                do {
                    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                    if (hp)
                    {
                        PROCESS_MEMORY_COUNTERS pmc{};
                        if (GetProcessMemoryInfo(hp, &pmc, sizeof(pmc)) && pmc.WorkingSetSize > best)
                        {
                            best = pmc.WorkingSetSize;
                            wcsncpy_s(name, pe.szExeFile, _TRUNCATE);
                        }
                        CloseHandle(hp);
                    }
                } while (Process32NextW(hsnap, &pe));
            }
            CloseHandle(hsnap);
            if (name[0])
            {
                wchar_t b[300]{};
                swprintf_s(b, L"%s (%llu MB)", name, static_cast<unsigned long long>(best >> 20));
                top_ = b;
            }
        }

        // Energia: TDP 65W x R$0,85/kWh (defaults; sincronia com settings.json na próxima iteração)
        constexpr double kwh = 0.85, tdp = 65.0;
        double hora = tdp / 1000.0 * kwh;
        wchar_t e[160]{};
        swprintf_s(e, L"R$ %.3f/h  ·  R$ %.2f/dia  ·  R$ %.2f/mês", hora, hora * 24.0, hora * 720.0);
        pw_ = e;

        Raise(L"MemText");
        Raise(L"TopProc");
        Raise(L"PowerText");
    }
}
