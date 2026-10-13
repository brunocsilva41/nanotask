#pragma once
// Contrato binário host (NanoTask.exe) <-> UI (NanoTask.UI.exe) via FileMapping.
// POD estrito: mesma definição incluída dos dois lados. Versione via `version`.
#include <windows.h>
#include <type_traits>

namespace nt::shared {

constexpr wchar_t kMapName[] = L"Local\\NanoTaskBlackbox";
constexpr unsigned long kRingCap = 300;
constexpr unsigned long kContractVersion = 1;

struct NanoSample {
    SYSTEMTIME t{};
    float cpuPct = 0.f;
    float diskPct = 0.f;
    unsigned long long memUsedMB = 0;
    unsigned long topPid = 0;
    wchar_t topName[32]{};
};

struct NanoRing {
    unsigned long version = kContractVersion;
    unsigned long count = 0; // amostras válidas (<= kRingCap), ordem cronológica
    NanoSample items[kRingCap]{};
};

static_assert(std::is_standard_layout_v<NanoSample>, "NanoSample deve ser POD");
static_assert(std::is_standard_layout_v<NanoRing>, "NanoRing deve ser POD");

} // namespace nt::shared
