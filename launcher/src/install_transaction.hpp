#pragma once
#include "native_platform.hpp"
namespace fetcher {
bool protectedClientPath(const std::string &name, const Json &policy);
void installTransaction(const fs::path &root, const fs::path &stage, const Json &policy,
                        bool tools);
Json recoverInstallTransactions(const fs::path &root);
Json listInstallBackups(const fs::path &root);
void restoreInstallBackup(const fs::path &root, const std::string &id);
// Only enabled by the CLI-only fixture path; never configured by profiles or GUI.
void fixtureInstallCrashAfter(int files);
}
