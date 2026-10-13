#pragma once
#include <windows.h>
#include <array>
#include <mutex>
#include <string>
#include <string_view>
#include <memory>
#include <format>

namespace nt {

// RAII para HANDLE
struct HandleCloser { void operator()(HANDLE h) const noexcept {
    if (h && h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
}};
using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;

struct HiconDeleter { void operator()(HICON h) const noexcept {
    if (h) ::DestroyIcon(h);
}};
using UniqueIcon = std::unique_ptr<std::remove_pointer_t<HICON>, HiconDeleter>;

// Log circular em memoria (256 entradas)
class RingLog {
public:
    static RingLog& Instance() noexcept { static RingLog s; return s; }
    void Push(DWORD err, std::string_view ctx) {
        std::lock_guard lk(m_mtx);
        m_buf[m_head % m_buf.size()] = std::format("[{}] err={} ctx={}", m_head, err, ctx);
        ++m_head;
    }
private:
    std::array<std::string, 256> m_buf{};
    uint64_t m_head{0};
    std::mutex m_mtx;
};

inline void LogWin32Error(std::string_view ctx) {
    RingLog::Instance().Push(::GetLastError(), ctx);
}

constexpr COLORREF TempToColor(float temp) noexcept {
    return (temp < 65.0f) ? RGB(74,222,128)
         : (temp <= 80.0f) ? RGB(250,204,21)
         : RGB(248,113,113);
}

} // namespace nt
