#pragma once
// BugReport.hpp — junta diagnóstico sem dados pessoais e abre a issue pré-preenchida.
#include "Settings.hpp"
#include <shellapi.h>

namespace nt {

// Copia logs + settings (redigido) para %LOCALAPPDATA%\NanoTask\report\ e abre
// o formulário de bug do GitHub com versão/modo pré-preenchidos.
inline void OpenBugReport(const wchar_t* repo, const wchar_t* version, TaskbarMode mode) {
    auto dir = AppDataDir() / L"report";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // Redige settings: copia removendo linhas com "token/key/secret"
    {
        std::ifstream in(AppDataDir() / L"settings.json");
        std::ofstream out(dir / L"settings.redacted.json");
        std::string line;
        while (in && std::getline(in, line)) {
            std::string low = line;
            for (auto& c : low) c = (char)tolower(c);
            if (low.find("token") == std::string::npos
             && low.find("secret") == std::string::npos
             && low.find("key") == std::string::npos)
                out << line << '\n';
        }
    }
    // Últimos 200KB de log (ring em disco, se habilitado)
    {
        std::error_code c2;
        auto logSrc = AppDataDir() / L"logs" / L"nanotask.log";
        auto sz = std::filesystem::file_size(logSrc, c2);
        if (!c2) {
            std::ifstream in(logSrc, std::ios::binary);
            in.seekg(sz > 204800 ? sz - 204800 : 0);
            std::ofstream out(dir / L"nanotask.tail.log", std::ios::binary);
            out << in.rdbuf();
        }
    }

    const wchar_t* modeStr = mode == TaskbarMode::Pinned ? L"fixo"
        : mode == TaskbarMode::Minimized ? L"minimizado" : L"fantasma";
    wchar_t url[1024]{};
    ::swprintf_s(url, L"https://github.com/%s/issues/new?template=bug_report.yml"
        L"&title=[bug] &versao=%s&modo=%s", repo, version, modeStr);
    ::ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace nt
