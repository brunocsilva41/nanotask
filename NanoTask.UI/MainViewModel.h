#pragma once
#include "MainViewModel.g.h"
#include <pdh.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml::Data;

namespace winrt::NanoTask::UI::implementation
{
    struct MainViewModel : MainViewModelT<MainViewModel>
    {
        MainViewModel();
        ~MainViewModel();
        hstring CpuTemp();
        void CpuTemp(hstring const& v);
        double TempValue();
        double CpuPct();
        hstring MemText();
        hstring TopProc();
        hstring PowerText();
        void Refresh();
        event_token PropertyChanged(PropertyChangedEventHandler const& h);
        void PropertyChanged(event_token t) noexcept;
    private:
        void Raise(std::wstring_view name);
        event<PropertyChangedEventHandler> ev_;
        hstring cpuTemp_ = L"\u2014";
        hstring mem_ = L"\u2014";
        hstring top_ = L"\u2014";
        hstring pw_ = L"\u2014";
        double tempValue_ = 0.0;
        double cpuPct_ = 0.0;
        PDH_HQUERY q_ = nullptr;
        PDH_HCOUNTER cpu_ = nullptr;
        PDH_HCOUNTER avail_ = nullptr;
    };
}
