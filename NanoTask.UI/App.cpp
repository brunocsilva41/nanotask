#include "pch.h"
#include "App.h"
#include "MainWindow.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::NanoTask::UI::implementation
{
    App::App()
    {
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        window = make<MainWindow>();
        window.Activate();
    }
}
