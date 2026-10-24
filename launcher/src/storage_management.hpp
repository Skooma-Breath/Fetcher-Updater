#pragma once
#include "native_platform.hpp"
namespace fetcher {
using StorageResolver = std::function<Json(const Json &)>;
Json estimateStorage(const fs::path &root, const Json &profile, const StorageResolver &resolve);
Json scanUnusedCache(const fs::path &root, const StorageResolver &resolve);
Json cleanUnusedCache(const fs::path &root, const std::string &token,
                      const StorageResolver &resolve);
}
