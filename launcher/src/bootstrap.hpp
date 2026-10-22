#pragma once
#include "native_platform.hpp"
namespace fetcher {
inline constexpr const char *toolsReleaseUrl =
    "https://api.github.com/repos/Skooma-Breath/Fetcher-Updater/releases/tags/fetcher-tester-tools";
// Alternate release endpoints are supplied only by the headless loopback test entry point.
Json bootstrapInstall(const fs::path &root, const Log &log = {},
                      const std::string &toolsUrl = toolsReleaseUrl,
                      const std::string &clientUrl = "");
}
