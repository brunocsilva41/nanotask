#include "pch.h"
#include "MainWindow.h"
#include "MainWindow.xaml.g.hpp"
#include "TempProviders.hpp"
#include "NativeSysCalls.hpp"
#include "Safety.hpp"
#include "Blackbox.hpp"
#include "Shared/BlackboxShared.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <algorithm>
#include <winrt/Microsoft.Windows.AppNotifications.h>

using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::Windows::AppNotifications;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Composition::SystemBackdrops;

namespace winrt::NanoTask::UI::implementation
{
    MainWindow::MainWindow()
    {
        vm_ = make<MainViewModel>();
        InitializeComponent();

        // Mica: composição 100% GPU via DComp
        mica_ = MicaController();
        backdropConfig_ = SystemBackdropConfiguration();
        backdropConfig_.IsInputActive(true);
        mica_.AddSystemBackdropTarget(
            this->try_as<::winrt::Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop>());
        mica_.SetSystemBackdropConfiguration(backdropConfig_);

        activatedToken_ = Activated({ this, &MainWindow::OnActivated });

        // Blackbox retroativo do host (FileMapping); nulo = host fechado
        map_ = OpenFileMappingW(FILE_MAP_READ, FALSE, nt::shared::kMapName);
        if (map_) mapView_ = MapViewOfFile(map_, FILE_MAP_READ, 0, 0, 0);

        RefreshProcs();
        RefreshAudio();
        timer_ = DispatcherTimer();
        timer_.Interval(std::chrono::seconds(2));
        timer_.Tick({ this, &MainWindow::OnTick });
        timer_.Start();
        OnTick(nullptr, nullptr);
    }

    void MainWindow::OnActivated(IInspectable const&, WindowActivatedEventArgs const& e)
    {
        // Spec 1.2: KillFocus aniquila a UI e devolve a RAM ao SO.
        if (e.WindowActivationState() == WindowActivationState::Deactivated)
        {
            timer_.Stop();
            timer_ = nullptr;
            mica_ = nullptr;
            backdropConfig_ = nullptr;
            vm_ = nullptr;
            sessions_.clear();
            pids_.clear();
            if (mapView_) { UnmapViewOfFile(mapView_); mapView_ = nullptr; }
            if (map_) { CloseHandle(map_); map_ = nullptr; }
            SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
            Application::Current().Exit();
        }
        else if (backdropConfig_)
        {
            backdropConfig_.IsInputActive(true);
        }
    }

    void MainWindow::OnTick(IInspectable const&, IInspectable const&)
    {
        if (!vm_) return;
        vm_.Refresh();

        // Temperatura real (NVML -> WMI), mesma cadeia do host
        static nt::MultiTempProvider temps;
        if (auto t = temps.ReadCelsius())
        {
            wchar_t b[64]{};
            std::string src(temps.Source());
            swprintf_s(b, L"%.0f °C  (%S)", static_cast<double>(*t), src.c_str());
            vm_.CpuTemp(b);
            Windows::UI::Color c = *t < 65.0 ? Colors::LimeGreen()
                    : *t <= 80.0 ? Colors::Orange() : Colors::Red();
            TempBig().Foreground(SolidColorBrush(c));
            if (*t > 80.0 && !toasted_)
            {
                toasted_ = true;
                try
                {
                    ::winrt::Microsoft::Windows::AppNotifications::AppNotificationManager::Default().Show(
                        ::winrt::Microsoft::Windows::AppNotifications::AppNotification(
                            L"<toast><visual><binding template=\"ToastGeneric\">"
                            L"<text>NanoTask: thermal throttling</text>"
                            L"<text>Possível corte de clock. Verifique a ventilação.</text>"
                            L"</binding></visual></toast>"));
                }
                catch (...) {}
                AlertBanner().Visibility(Visibility::Visible);
            }
            else if (*t < 75.0)
            {
                toasted_ = false;
                AlertBanner().Visibility(Visibility::Collapsed);
            }
        }
        DrawGraph();
    }

    void MainWindow::DrawGraph()
    {
        auto points = CpuLine().Points();
        points.Clear();
        double w = CpuLine().ActualWidth();
        double h = CpuLine().ActualHeight();
        if (w < 10.0 || h < 10.0) return;

        std::vector<float> vals;
        auto* ring = static_cast<nt::shared::NanoRing*>(mapView_);
        if (ring && ring->version == nt::shared::kContractVersion && ring->count > 1)
        {
            unsigned n = (std::min)(ring->count, nt::shared::kRingCap);
            for (unsigned i = 0; i < n; ++i) vals.push_back(ring->items[i].cpuPct);
        }
        else
        {
            live_.push_back(static_cast<float>(vm_.CpuPct()));
            if (live_.size() > 300) live_.pop_front();
            vals.assign(live_.begin(), live_.end());
        }
        if (vals.size() < 2) return;
        for (size_t i = 0; i < vals.size(); ++i)
        {
            float v = vals[i];
            if (v < 0.f) v = 0.f;
            if (v > 100.f) v = 100.f;
            points.Append({ static_cast<float>(w * i / (vals.size() - 1)),
                            static_cast<float>(h * (1.0 - v / 100.0)) });
        }
    }

    static std::wstring ExeOf(unsigned long pid)
    {
        HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hsnap == INVALID_HANDLE_VALUE) return L"?";
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        std::wstring out = L"?";
        if (Process32FirstW(hsnap, &pe))
        {
            do {
                if (pe.th32ProcessID == pid) { out = pe.szExeFile; break; }
            } while (Process32NextW(hsnap, &pe));
        }
        CloseHandle(hsnap);
        return out;
    }

    void MainWindow::RefreshProcs()
    {
        ProcList().Items().Clear();
        pids_.clear();
        HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hsnap == INVALID_HANDLE_VALUE) return;
        struct E { unsigned long pid; std::wstring name; SIZE_T ws; };
        std::vector<E> all;
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (Process32FirstW(hsnap, &pe))
        {
            do {
                HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                SIZE_T ws = 0;
                if (hp)
                {
                    PROCESS_MEMORY_COUNTERS pmc{};
                    if (GetProcessMemoryInfo(hp, &pmc, sizeof(pmc))) ws = pmc.WorkingSetSize;
                    CloseHandle(hp);
                }
                all.push_back({ pe.th32ProcessID, pe.szExeFile, ws });
            } while (Process32NextW(hsnap, &pe));
        }
        CloseHandle(hsnap);
        std::sort(all.begin(), all.end(), [](const E& a, const E& b) { return a.ws > b.ws; });
        wchar_t b[320]{};
        for (size_t i = 0; i < all.size() && i < 30; ++i)
        {
            swprintf_s(b, L"%5lu  %-24s %4llu MB", all[i].pid, all[i].name.c_str(),
                static_cast<unsigned long long>(all[i].ws >> 20));
            ProcList().Items().Append(box_value(b));
            pids_.push_back(all[i].pid);
        }
    }

    void MainWindow::OnProcRefresh(IInspectable const&, RoutedEventArgs const&) { RefreshProcs(); }

    void MainWindow::OnKill(IInspectable const&, RoutedEventArgs const&)
    {
        int i = ProcList().SelectedIndex();
        if (i < 0 || static_cast<size_t>(i) >= pids_.size()) return;
        unsigned long pid = pids_[static_cast<size_t>(i)];
        if (nt::safety::AuthorizeKill(pid, true) && nt::native::KillOne(pid)) RefreshProcs();
    }

    void MainWindow::OnTreeKill(IInspectable const&, RoutedEventArgs const&)
    {
        int i = ProcList().SelectedIndex();
        if (i < 0 || static_cast<size_t>(i) >= pids_.size()) return;
        unsigned long pid = pids_[static_cast<size_t>(i)];
        if (nt::safety::AuthorizeKill(pid, true)) nt::native::ProcessTreeKill(pid);
        RefreshProcs();
    }

    unsigned long MainWindow::ForegroundPid()
    {
        HWND fg = GetForegroundWindow();
        unsigned long pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        return pid;
    }

    void MainWindow::ApplyFgPreset(unsigned long prio, bool eco)
    {
        unsigned long pid = ForegroundPid();
        if (pid <= 4 || !nt::safety::AuthorizeKill(pid, true)) return;
        HANDLE hp = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
        if (!hp) return;
        SetPriorityClass(hp, prio);
        if (eco) nt::ApplyEcoQoS(hp, true);
        CloseHandle(hp);
    }

    void MainWindow::OnEco(IInspectable const&, RoutedEventArgs const&) { ApplyFgPreset(BELOW_NORMAL_PRIORITY_CLASS, true); }
    void MainWindow::OnBalanced(IInspectable const&, RoutedEventArgs const&) { ApplyFgPreset(NORMAL_PRIORITY_CLASS, false); }
    void MainWindow::OnTurbo(IInspectable const&, RoutedEventArgs const&) { ApplyFgPreset(HIGH_PRIORITY_CLASS, false); }

    void MainWindow::OnDevFreeze(IInspectable const&, RoutedEventArgs const&)
    {
        static const wchar_t* targets[] = { L"vmmem", L"dockerd.exe", L"sqlservr.exe" };
        devFrozen_ = !devFrozen_;
        HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hsnap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (Process32FirstW(hsnap, &pe))
        {
            do {
                std::wstring n = pe.szExeFile;
                for (auto& c : n) c = static_cast<wchar_t>(towlower(c));
                for (auto t : targets)
                {
                    if (n == t)
                    {
                        if (devFrozen_) nt::native::NtSuspend(pe.th32ProcessID);
                        else nt::native::NtResume(pe.th32ProcessID);
                    }
                }
            } while (Process32NextW(hsnap, &pe));
        }
        CloseHandle(hsnap);
    }

    void MainWindow::OnPanic(IInspectable const&, RoutedEventArgs const&)
    {
        ShowMsg(nt::native::PanicButton() ? L"Explorer reiniciado." : L"Falha no panic.");
    }

    void MainWindow::OnFlushDns(IInspectable const&, RoutedEventArgs const&)
    {
        ShowMsg(nt::native::FlushDns() ? L"DNS liberado." : L"Falha no flush DNS.");
    }

    void MainWindow::OnRamFlush(IInspectable const&, RoutedEventArgs const&)
    {
        ShowMsg(nt::native::PurgeStandbyList() ? L"Standby esvaziada." : L"Falha — rode como admin.");
    }

    void MainWindow::RefreshAudio()
    {
        sessions_.clear();
        AudioList().Items().Clear();
        try
        {
            auto en = create_instance<IMMDeviceEnumerator>(__uuidof(IMMDeviceEnumerator), CLSCTX_ALL);
            com_ptr<IMMDevice> dev;
            check_hresult(en->GetDefaultAudioEndpoint(eRender, eConsole, dev.put()));
            com_ptr<IAudioSessionManager2> mgr;
            check_hresult(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(mgr.put())));
            com_ptr<IAudioSessionEnumerator> sess;
            check_hresult(mgr->GetSessionEnumerator(sess.put()));
            int n = 0;
            check_hresult(sess->GetCount(&n));
            for (int i = 0; i < n; ++i)
            {
                com_ptr<IAudioSessionControl> ctl;
                check_hresult(sess->GetSession(i, ctl.put()));
                com_ptr<ISimpleAudioVolume> vol;
                try { vol = ctl.as<ISimpleAudioVolume>(); }
                catch (...) { continue; }
                PWSTR dn = nullptr;
                hstring name;
                if (SUCCEEDED(ctl->GetDisplayName(&dn)) && dn && *dn)
                {
                    name = dn;
                    CoTaskMemFree(dn);
                }
                else
                {
                    com_ptr<IAudioSessionControl2> c2;
                    unsigned long pid = 0;
                    if (SUCCEEDED(ctl->QueryInterface(c2.put())))
                        c2->GetProcessId(&pid);
                    if (pid) name = ExeOf(pid);
                    else name = L"sistema";
                }
                float f = 0.f;
                if (FAILED(vol->GetMasterVolume(&f))) continue;
                wchar_t b[300]{};
                swprintf_s(b, L"%s — %d%%", name.c_str(), static_cast<int>(f * 100.f));
                AudioList().Items().Append(box_value(b));
                sessions_.push_back({ name, vol });
            }
        }
        catch (...) {}
        if (!sessions_.empty())
        {
            AudioList().SelectedIndex(0);
            wchar_t b[32]{};
            swprintf_s(b, L"%.0f%%", AppVol().Value());
            VolLabel().Text(b);
        }
    }

    void MainWindow::OnAudioRefresh(IInspectable const&, RoutedEventArgs const&) { RefreshAudio(); }

    void MainWindow::OnAppVolChanged(IInspectable const&, Controls::Primitives::RangeBaseValueChangedEventArgs const&)
    {
        wchar_t b[32]{};
        swprintf_s(b, L"%.0f%%", AppVol().Value());
        VolLabel().Text(b);
    }

    void MainWindow::OnAudioApply(IInspectable const&, RoutedEventArgs const&)
    {
        int i = AudioList().SelectedIndex();
        if (i < 0 || static_cast<size_t>(i) >= sessions_.size()) return;
        float v = static_cast<float>(AppVol().Value() / 100.0);
        sessions_[static_cast<size_t>(i)].vol->SetMasterVolume(v, nullptr);
        RefreshAudio();
    }

    void MainWindow::ShowMsg(hstring const& text)
    {
        ContentDialog dlg;
        dlg.Title(box_value(L"NanoTask"));
        dlg.Content(box_value(text));
        dlg.CloseButtonText(L"OK");
        dlg.XamlRoot(this->Content().XamlRoot());
        (void)dlg.ShowAsync();
    }
}
