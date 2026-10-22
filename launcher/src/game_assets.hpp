#pragma once
#include "native_platform.hpp"
namespace fetcher {
Json normalizeGameAssets(const Json &definition);
void restoreGameAssets(const fs::path &stage, const fs::path &gameData, const Json &definition);
}
