#pragma once
// Settings.hpp — settings.json com parse nativo mínimo (sem third-party).
// Fail-open com defaults: arquivo ausente/corrompido nunca trava o boot.
#include "Foundation.hpp"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace nt {

enum class TaskbarMode : DWORD { Ghost = 0, Pinned = 1, Minimized = 2 };

struct Settings {
    TaskbarMode taskbarMode = TaskbarMode::Ghost;
    bool dynamicIcon = true;
    float warnC = 65.f, critC = 80.f;
    bool updatesEnabled = true;
    std::wstring updateChannel = L"stable"; // stable|beta
    int checkHours = 24;
    bool autoDownload = false;
    bool confirmKill = true;
    bool protectCritical = true;
    bool widgetEnabled = true; // widget flutuante visível no boot
    double kwhPrice = 0.85;
    int tdpWatts = 65;
};

inline std::filesystem::path AppDataDir() {
    PWSTR p = nullptr;
    std::filesystem::path d = std::filesystem::temp_directory_path();
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
        d = std::filesystem::path(p) / L"NanoTask";
        ::CoTaskMemFree(p);
    }
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d;
}

inline Settings LoadSettings() {
    Settings s;
    std::ifstream f(AppDataDir() / L"settings.json");
    if (!f) return s; // primeira execução: defaults
    std::string j((std::istreambuf_iterator<char>(f)), {});
    auto flag = [&](const char* k, bool& out) {
        auto pos = j.find(k);
        if (pos != std::string::npos) {
            auto v = j.find_first_of("tf", pos + strlen(k));
            if (v != std::string::npos) out = (j[v] == 't');
        }
    };
    if (j.find("\"pinned\"") != std::string::npos) s.taskbarMode = TaskbarMode::Pinned;
    else if (j.find("\"minimized\"") != std::string::npos) s.taskbarMode = TaskbarMode::Minimized;
    flag("\"dynamicIcon\"", s.dynamicIcon);
    flag("\"enabled\"", s.updatesEnabled);
    flag("\"autoDownload\"", s.autoDownload);
    flag("\"confirmKill\"", s.confirmKill);
    flag("\"protectCritical\"", s.protectCritical);
    flag("\"widgetEnabled\"", s.widgetEnabled);
    if (auto pos = j.find("\"kwhPrice\""); pos != std::string::npos) {
        auto num = j.find_first_of("0123456789", pos);
        if (num != std::string::npos) s.kwhPrice = std::stod(j.substr(num));
    }
    if (auto pos = j.find("\"tdpWatts\""); pos != std::string::npos) {
        auto num = j.find_first_of("0123456789", pos);
        if (num != std::string::npos) s.tdpWatts = std::stoi(j.substr(num));
    }
    if (j.find("\"beta\"") != std::string::npos) s.updateChannel = L"beta";
    return s;
}

} // namespace nt
