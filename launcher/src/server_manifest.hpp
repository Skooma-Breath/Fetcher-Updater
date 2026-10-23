#pragma once
#include "native_platform.hpp"
namespace fetcher {
// Normalizes declarative mod plans; native layout operations never execute programs.
Json normalizeModActions(const Json &actions, const std::string &umoPrefix = "");
std::string modLayoutKey(const Json &definition, const std::string &sourceSha256);
void applyModActions(const fs::path &stage, const Json &actions, const fs::path &gameData = {});
Json normalizeFomod(const Json &definition);
void installFomod(const fs::path &raw, const fs::path &stage, const Json &definition);
Json normalizeServerManifest(const Json &value);
Json normalizeDiscoveredManifest(const std::string &text, const std::string &digest,
                                 const std::string &host, int port);
void validateModDefinition(const Json &definition);
void validateModPayload(const fs::path &directory);
Json readPackageReceipt(const fs::path &directory);
std::vector<fs::path> modDataRoots(const fs::path &directory, const Json &definition);
}
