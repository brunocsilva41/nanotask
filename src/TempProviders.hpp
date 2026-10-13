#pragma once
// TempProviders.hpp — temperatura CPU/GPU em user-mode, sem driver kernel.
// Cadeia com fallback: NVML (GPU NVIDIA) -> WMI (zona ACPI). Sem new/delete.
#ifdef _MSC_VER
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#endif

#include "Blackbox.hpp" // nt::ITempProvider
#include <windows.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <optional>
#include <string_view>
#include <memory>
#include <utility>

namespace nt {

struct LibBox { // RAII p/ HMODULE (NÃO usar UniqueHandle: aquele chama CloseHandle)
    HMODULE h_ = nullptr;
    LibBox() = default;
    explicit LibBox(HMODULE h) noexcept : h_(h) {}
    LibBox(const LibBox&) = delete; LibBox& operator=(const LibBox&) = delete;
    LibBox(LibBox&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
    LibBox& operator=(LibBox&& o) noexcept {
        if (this != &o) { Close(); h_ = o.h_; o.h_ = nullptr; } return *this; }
    ~LibBox() { Close(); }
    void Close() noexcept { if (h_) { ::FreeLibrary(h_); h_ = nullptr; } }
    explicit operator bool() const noexcept { return h_ != nullptr; }
};

template <typename T> // RAII mínimo p/ COM (sem WIL no projeto)
struct ComPtr {
    T* p_ = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete; ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { Reset(); p_ = o.p_; o.p_ = nullptr; } return *this; }
    ~ComPtr() { Reset(); }
    void Reset() noexcept { if (p_) { p_->Release(); p_ = nullptr; } }
    T** Put() noexcept { Reset(); return &p_; }
    T* Get() const noexcept { return p_; }
    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
};

struct BstrBox {
    BSTR s_ = nullptr;
    BstrBox() = default;
    explicit BstrBox(const wchar_t* w) { s_ = ::SysAllocString(w); }
    BstrBox(const BstrBox&) = delete; BstrBox& operator=(const BstrBox&) = delete;
    ~BstrBox() { if (s_) ::SysFreeString(s_); }
    BSTR Get() const noexcept { return s_; }
    explicit operator bool() const noexcept { return s_ != nullptr; }
};

// 1. GPU NVIDIA via nvml.dll (user-mode, vem com o driver). Ausente = nullopt, log UMA vez.
class NvmlTempProvider final : public ITempProvider {
    struct NvmlDeviceOpaque {};
    using NvmlDevice = NvmlDeviceOpaque*;
    using InitFn = int (*)();
    using GetHandleFn = int (*)(unsigned int, NvmlDevice*);
    using GetTempFn = int (*)(NvmlDevice, int, unsigned int*); // sensor NVML_TEMPERATURE_GPU=0
    using ShutdownFn = int (*)();
    LibBox lib_;
    InitFn init_ = nullptr; GetHandleFn handle_ = nullptr;
    GetTempFn temp_ = nullptr; ShutdownFn shutdown_ = nullptr;
    bool ready_ = false, logged_ = false;
    void NoteOnce(std::string_view ctx) {
        if (!logged_) { logged_ = true; RingLog::Instance().Push(::GetLastError(), ctx); } }
public:
    NvmlTempProvider() {
        HMODULE h = ::LoadLibraryW(L"nvml.dll");
        if (!h) { NoteOnce("nvml.dll ausente (sem GPU NVIDIA)"); return; }
        lib_ = LibBox(h);
        init_ = reinterpret_cast<InitFn>(::GetProcAddress(h, "nvmlInit_v2"));
        handle_ = reinterpret_cast<GetHandleFn>(::GetProcAddress(h, "nvmlDeviceGetHandleByIndex_v2"));
        temp_ = reinterpret_cast<GetTempFn>(::GetProcAddress(h, "nvmlDeviceGetTemperature"));
        shutdown_ = reinterpret_cast<ShutdownFn>(::GetProcAddress(h, "nvmlShutdown"));
        if (!init_ || !handle_ || !temp_ || !shutdown_) { NoteOnce("nvml exports ausentes"); return; }
        if (init_() != 0) { NoteOnce("nvmlInit_v2 falhou"); return; }
        ready_ = true;
    }
    ~NvmlTempProvider() override { if (ready_ && shutdown_) shutdown_(); }
    std::optional<float> ReadCelsius() override {
        if (!ready_) return std::nullopt;
        NvmlDevice dev = nullptr;
        if (!handle_ || handle_(0, &dev) != 0 || !dev) { NoteOnce("nvml GetHandleByIndex falhou"); return std::nullopt; }
        unsigned int t = 0;
        if (!temp_ || temp_(dev, 0, &t) != 0) { NoteOnce("nvml GetTemperature falhou"); return std::nullopt; }
        logged_ = false;
        if (t > 115u) { RingLog::Instance().Push(0, "nvml temp fora de faixa"); return std::nullopt; }
        return static_cast<float>(t);
    }
};

// 2. Zona térmica ACPI via WMI. GROSSEIRO: é zona placa-mãe/BIOS, NÃO package CPU. Só fallback.
class WmiThermalZoneProvider final : public ITempProvider {
    bool comReady_ = false, needUninit_ = false;
    bool EnsureCom() {
        if (comReady_) return true;
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == RPC_E_CHANGED_MODE) { comReady_ = true; return true; }
        if (FAILED(hr)) { LogWin32Error("WMI CoInitializeEx"); return false; }
        needUninit_ = true; comReady_ = true;
        const HRESULT hs = ::CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
        if (FAILED(hs) && hs != RPC_E_TOO_LATE) LogWin32Error("WMI CoInitializeSecurity");
        return true;
    }
public:
    ~WmiThermalZoneProvider() override { if (needUninit_) ::CoUninitialize(); }
    std::optional<float> ReadCelsius() override {
        if (!EnsureCom()) return std::nullopt;
        ComPtr<IWbemLocator> loc;
        HRESULT hr = ::CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<void**>(loc.Put()));
        if (FAILED(hr) || !loc) { LogWin32Error("WMI CoCreateInstance WbemLocator"); return std::nullopt; }
        BstrBox ns(L"ROOT\\WMI");
        if (!ns) { LogWin32Error("WMI SysAllocString ns"); return std::nullopt; }
        ComPtr<IWbemServices> svc;
        hr = loc->ConnectServer(ns.Get(), nullptr, nullptr, nullptr, 0, nullptr, nullptr, svc.Put());
        if (FAILED(hr) || !svc) { LogWin32Error("WMI ConnectServer ROOT\\WMI"); return std::nullopt; }
        hr = ::CoSetProxyBlanket(svc.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        if (FAILED(hr)) { LogWin32Error("WMI CoSetProxyBlanket"); return std::nullopt; }
        BstrBox ql(L"WQL"), qq(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
        if (!ql || !qq) { LogWin32Error("WMI SysAllocString query"); return std::nullopt; }
        ComPtr<IEnumWbemClassObject> en;
        hr = svc->ExecQuery(ql.Get(), qq.Get(),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, en.Put());
        if (FAILED(hr) || !en) { LogWin32Error("WMI ExecQuery ThermalZone"); return std::nullopt; }
        ComPtr<IWbemClassObject> obj; ULONG got = 0;
        hr = en->Next(5000, 1, obj.Put(), &got);
        if (hr != S_OK || got == 0 || !obj) { LogWin32Error("WMI Next vazio (sem zona termica?)"); return std::nullopt; }
        BstrBox prop(L"CurrentTemperature");
        if (!prop) { LogWin32Error("WMI SysAllocString prop"); return std::nullopt; }
        VARIANT v{};
        hr = obj->Get(prop.Get(), 0, &v, nullptr, nullptr);
        if (FAILED(hr)) { LogWin32Error("WMI Get CurrentTemperature"); return std::nullopt; }
        double raw = 0.0;
        bool okType = true;
        switch (V_VT(&v)) {
        case VT_I4:  raw = static_cast<double>(V_I4(&v)); break;
        case VT_UI4: raw = static_cast<double>(V_UI4(&v)); break;
        case VT_R8:  raw = V_R8(&v); break;
        default:     okType = false; break;
        }
        (void)::VariantClear(&v);
        if (!okType) { RingLog::Instance().Push(0, "WMI CurrentTemperature tipo inesperado"); return std::nullopt; }
        const double c = raw / 10.0 - 273.15; // décimos de Kelvin -> Celsius
        if (c < -50.0 || c > 150.0) { RingLog::Instance().Push(0, "WMI temp fora de faixa"); return std::nullopt; }
        return static_cast<float>(c);
    }
};

// 3. AmdAdlStub — DESCARTADO por design (gap honesto): ADL (atiadlxx.dll) tem ABI
//    instável entre drivers e só é confiável com SDK casado ao driver. GPU AMD
//    descoberta no MVP (Source()="none" ou WMI grosseiro). Futuro: D3DKMT ou SDK casado.

// 4. Cadeia NVML -> WMI. Source() p/ UI e bug report.
class MultiTempProvider final : public ITempProvider {
    NvmlTempProvider nvml_; WmiThermalZoneProvider wmi_;
    std::string_view active_ = "none";
public:
    std::optional<float> ReadCelsius() override {
        if (auto t = nvml_.ReadCelsius()) { active_ = "nvml"; return t; }
        if (auto t = wmi_.ReadCelsius()) { active_ = "wmi"; return t; }
        active_ = "none"; return std::nullopt;
    }
    std::string_view Source() const noexcept { return active_; }
};

} // namespace nt
