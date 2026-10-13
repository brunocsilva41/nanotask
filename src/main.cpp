#include "TrayHost.hpp"
#include "Settings.hpp"
#include "Safety.hpp"
#include "UpdateManager.hpp"
#include <string_view>

// wWinMain args: --tray (boot fantasma), --smoke-test (CI: init + sai com 0)
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR pCmdLine, int) {
    std::wstring_view cmd(pCmdLine ? pCmdLine : L"");
    const bool smoke = cmd.find(L"--smoke-test") != std::wstring_view::npos;

    nt::UniqueHandle mtx(::CreateMutexW(nullptr, FALSE, L"Local\\NanoTaskGhost"));
    if (!mtx || ::GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    nt::Settings cfg = nt::LoadSettings(); // fail-open: nunca trava o boot
    (void)cfg;

    nt::TrayHost host;
    if (!host.Init(hInst)) return 1;
    host.Configure(cfg);
    if (smoke) { host.Shutdown(); return 0; } // CI: prova init/shutdown limpos

    const bool smokeUi = cmd.find(L"--smoke-ui") != std::wstring_view::npos;
    if (smokeUi) {
        host.OpenDashboard(); // prova criação de todos os controles
        const DWORD mainId = ::GetCurrentThreadId();
        std::jthread killer([mainId](std::stop_token st) {
            for (int i = 0; i < 40 && !st.stop_requested(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ::PostThreadMessageW(mainId, WM_APP + 99, 0, 0);
        });
        MSG msg{};
        int code = 0;
        while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_APP + 99) { code = 0; break; }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        host.Shutdown();
        return code;
    }

    std::jthread worker([&host](std::stop_token st){ host.RunWorker(st); });

    int code = host.Pump();
    host.Shutdown();
    return code;
}
