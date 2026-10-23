#pragma once
#include "native_platform.hpp"
namespace fetcher {
Json launcherRuntime(const fs::path &executable, const fs::path &uiDirectory);
// Caller holds the installation lock. Copy only inventory-verified runtime files.
bool stageLauncherRuntime(const fs::path &root, const fs::path &stage);
}
