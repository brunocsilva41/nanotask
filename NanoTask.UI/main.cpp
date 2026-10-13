#include "pch.h"
#include "App.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(apartment_type::single_threaded);
    Application::Start([](auto&&) { make<NanoTask::UI::implementation::App>(); });
    return 0;
}
