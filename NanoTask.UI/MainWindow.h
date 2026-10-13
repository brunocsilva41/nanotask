#pragma once
#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"
#include "MainViewModel.h"
#include <audiopolicy.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Composition::SystemBackdrops;
#include <winrt/Microsoft.Windows.AppNotifications.h>
#include <deque>

namespace winrt::NanoTask::UI::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        NanoTask::UI::MainViewModel ViewModel() { return vm_; }

        void OnProcRefresh(IInspectable const&, RoutedEventArgs const&);
        void OnKill(IInspectable const&, RoutedEventArgs const&);
        void OnTreeKill(IInspectable const&, RoutedEventArgs const&);
        void OnEco(IInspectable const&, RoutedEventArgs const&);
        void OnBalanced(IInspectable const&, RoutedEventArgs const&);
        void OnTurbo(IInspectable const&, RoutedEventArgs const&);
        void OnDevFreeze(IInspectable const&, RoutedEventArgs const&);
        void OnPanic(IInspectable const&, RoutedEventArgs const&);
        void OnFlushDns(IInspectable const&, RoutedEventArgs const&);
        void OnRamFlush(IInspectable const&, RoutedEventArgs const&);
        void OnAudioRefresh(IInspectable const&, RoutedEventArgs const&);
        void OnAudioApply(IInspectable const&, RoutedEventArgs const&);
        void OnAppVolChanged(IInspectable const&, Controls::Primitives::RangeBaseValueChangedEventArgs const&);
        void OnTick(IInspectable const&, IInspectable const&);
        void OnActivated(IInspectable const&, WindowActivatedEventArgs const&);
        void ShowMsg(hstring const& text);

    private:
        struct AudioSession
        {
            hstring name;
            com_ptr<ISimpleAudioVolume> vol;
        };

        void RefreshProcs();
        void RefreshAudio();
        void DrawGraph();
        void ApplyFgPreset(unsigned long prio, bool eco);
        static unsigned long ForegroundPid();

        NanoTask::UI::MainViewModel vm_{ nullptr };
        DispatcherTimer timer_{ nullptr };
        ::winrt::Microsoft::UI::Composition::SystemBackdrops::MicaController mica_{ nullptr };
        ::winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropConfiguration backdropConfig_{ nullptr };
        std::vector<AudioSession> sessions_;
        std::vector<unsigned long> pids_;
        std::deque<float> live_;
        void* mapView_ = nullptr;
        HANDLE map_ = nullptr;
        bool devFrozen_ = false;
        bool toasted_ = false;
        event_token activatedToken_;
    };
}
