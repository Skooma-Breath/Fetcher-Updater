#pragma once
#include "native_platform.hpp"
namespace fetcher
{
    Json loadLegacyInventory(const fs::path& interfaceFile);
    // Inventory and replacement roots come from trusted launcher code, never a
    // server action.
    Json legacyMigration(const fs::path& root, const Json& inventory, const Json& replacements,
        const std::string& operation, const std::string& token = "", const Log& log = {});
    Json legacyMigrationStatus(const fs::path& root);
    bool legacyMigrationPending(const fs::path& root);
    Json automaticallyMigrateLegacy(
        const fs::path& root, const Json& inventory, const Json& replacements, const Log& log = {});
}
