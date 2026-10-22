#pragma once
#include "native_platform.hpp"
namespace fetcher {
Json normalizeFilePatch(const Json &definition);
void applyFilePatch(const fs::path &target, const Json &definition);
}
