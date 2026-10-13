#pragma once
#include "App.g.h"
#include "App.xaml.g.h"

namespace winrt::NanoTask::UI::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
        Microsoft::UI::Xaml::Window window{ nullptr };
    };
}
