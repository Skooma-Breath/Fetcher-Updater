#pragma once
#include "native_platform.hpp"
namespace fetcher {
// Converts declarative UMO entries into ordinary package definitions. No action
// is executed.
Json normalizeServerManifest(const Json &value);
void validateModDefinition(const Json &definition);
void validateModPayload(const fs::path &directory);
std::vector<fs::path> modDataRoots(const fs::path &directory, const Json &definition);
}
