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
    if (smoke) { host.Shutdown(); return 0; } // CI: prova init/shutdown limpos

    std::jthread worker([&host](std::stop_token st){ host.RunWorker(st); });

    int code = host.Pump();
    host.Shutdown();
    return code;
}
