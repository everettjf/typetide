#pragma once
#include "Translator.h"
#include <atomic>
#include <functional>
#include <string>

namespace builtin {

constexpr int64_t kModelBytes = 2489909760LL;
std::wstring ModelPath();
bool Installed();
// Runs on a worker thread. Completed downloads are SHA-256 verified before publication.
bool Download(const std::atomic<bool>& cancel,
              const std::function<void(double, const std::wstring&)>& progress,
              std::wstring* error);
// Runs on a worker thread; starts the bundled, loopback-only inference helper lazily.
bool Stream(const TranslationRequest& request, uint64_t id,
            const translator::DeltaFn& onDelta, const std::atomic<bool>& cancel,
            std::wstring* full, std::wstring* error);
void Shutdown();

} // namespace builtin
