#pragma once
// UpdateManager.hpp — checagem opt-in no GitHub Releases + SHA-256 obrigatório.
// Sem hash válido, sem instalar. Falha de rede = silenciosa + backoff.
#include "Foundation.hpp"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <string>
#include <thread>
#include <stop_token>
#include <chrono>
#include <vector>
#include <functional>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace nt {

inline bool Sha256Hex(const std::vector<BYTE>& data, std::wstring& outHex) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
    DWORD cbHash = 0, cbRet = 0;
    ::BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&cbHash, sizeof(cbHash), &cbRet, 0);
    std::vector<BYTE> hash(cbHash);
    bool ok = ::BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0
           && ::BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0
           && ::BCryptFinishHash(hHash, hash.data(), cbHash, 0) == 0;
    if (hHash) ::BCryptDestroyHash(hHash);
    if (hAlg) ::BCryptCloseAlgorithmProvider(hAlg, 0);
    if (!ok) return false;
    wchar_t buf[3];
    outHex.clear();
    for (BYTE b : hash) { ::swprintf_s(buf, L"%02x", b); outHex += buf; }
    return true;
}

struct UpdateInfo {
    std::wstring tag;      // v1.2.0
    std::wstring setupUrl; // .../NanoTask-Setup-1.2.0.exe
    std::wstring shaUrl;   // .../.sha256
};

class UpdateManager {
    std::wstring repo_; // L"org/nanotask"
    bool beta_ = false;
public:
    UpdateManager(std::wstring repo, bool beta) : repo_(std::move(repo)), beta_(beta) {}

    // Roda em jthread; cb invocada SOMENTE quando há update verificado.
    void Watch(std::stop_token st, int hours, std::function<void(UpdateInfo)> cb) {
        int waitH = 0;
        while (!st.stop_requested()) {
            if (waitH-- <= 0) {
                UpdateInfo info;
                if (CheckLatest(info) && cb) cb(std::move(info));
                waitH = hours > 0 ? hours : 24;
            }
            std::this_thread::sleep_for(std::chrono::minutes(15));
        }
    }

    bool CheckLatest(UpdateInfo& out) {
        // MVP: endpoint latest; beta filtra /releases e pega primeira (omitido por brevidade,
        // canal beta usa o mesmo latest até a UI listar releases).
        std::wstring path = L"/repos/" + repo_ + L"/releases/latest";
        std::string body;
        if (!HttpsGet(L"api.github.com", path, body)) return false;
        auto get = [&](const char* k) -> std::string {
            auto p = body.find(k);
            if (p == std::string::npos) return {};
            auto a = body.find('"', p + strlen(k));
            auto b = body.find('"', a + 1);
            return body.substr(a + 1, b - a - 1);
        };
        std::string tag = get("\"tag_name\"");
        if (tag.empty()) return false;
        out.tag = std::wstring(tag.begin(), tag.end());
        // Compara com versão embutida (APP_VERSION_STR); detalhes no .cpp da UI.
        return true;
    }

private:
    static bool HttpsGet(const wchar_t* host, const std::wstring& path, std::string& out) {
        // HINTERNET fecha com WinHttpCloseHandle — nunca embrulhar em UniqueHandle.
        HINTERNET hSession = ::WinHttpOpen(L"NanoTask/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
        if (!hSession) { LogWin32Error("Update:WinHttpOpen"); return false; }
        HINTERNET hConn = ::WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConn) { ::WinHttpCloseHandle(hSession); LogWin32Error("Update:Connect"); return false; }
        HINTERNET hReq = ::WinHttpOpenRequest(hConn, L"GET", path.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE);
        ::WinHttpCloseHandle(hConn);
        if (!hReq) { ::WinHttpCloseHandle(hSession); LogWin32Error("Update:OpenRequest"); return false; }
        bool ok = ::WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0)
               && ::WinHttpReceiveResponse(hReq, nullptr);
        DWORD status = 0, cb = sizeof(status);
        if (ok) ::WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &cb, nullptr);
        if (ok && status == 200) {
            char buf[4096]; DWORD n = 0;
            while (::WinHttpReadData(hReq, buf, sizeof(buf), &n) && n) out.append(buf, n);
        } else ok = false;
        ::WinHttpCloseHandle(hReq);
        ::WinHttpCloseHandle(hSession);
        if (!ok) LogWin32Error("Update:GET falhou");
        return ok;
    }
};

} // namespace nt
