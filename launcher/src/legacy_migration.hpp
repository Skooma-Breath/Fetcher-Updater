#pragma once
#include "native_platform.hpp"
namespace fetcher
{
    // Inventory and replacement roots come from trusted launcher code, never a
    // server action.
    Json legacyMigration(const fs::path& root, const Json& inventory, const Json& replacements,
        const std::string& operation, const std::string& token = "", const Log& log = {});
    Json legacyMigrationStatus(const fs::path& root);
}
