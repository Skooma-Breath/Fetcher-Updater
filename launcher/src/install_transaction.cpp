#include "install_transaction.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <regex>
#include <set>
namespace fetcher {
namespace {
int crashAfter = 0;
void need(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool allowed(const std::string &name, bool tools, const Json &policy) {
    if (!tools)
        return name == "fetcher-client-files.json" || !protectedClientPath(name, policy);
    static const std::set<std::string> names = {"apply-fetcher-public-test-config.bat",
                                                "apply-fetcher-public-test-config.ps1",
                                                "fetcher-canonical-fallbacks.cfg",
                                                "apply-fetcher-mod-compatibility.ps1",
                                                "apply-fetcher-zhi-compatibility.ps1",
                                                "fetcher-simulator-umo.json",
                                                "fetcher-client-patches.json",
                                                "fetcher-client-protection-policy.json",
                                                "fetcher-mod-compatibility-patches.json",
                                                "fetcher-update-channel.json",
                                                "fetcher_simulator_readme.txt",
                                                "install-fetcher-bardcraft-with-umo.bat",
                                                "install-fetcher-bardcraft-with-umo.ps1",
                                                "install-fetcher-client-mod-bundle.ps1",
                                                "install-fetcher-tester-tools.ps1",
                                                "setup-fetcher-updater.bat",
                                                "launch-fetcher-character.bat",
                                                "launch-fetcher-character.ps1",
                                                "update-fetcher-simulator.bat",
                                                "update-fetcher-simulator.ps1",
                                                "fetcherlauncher.exe",
                                                "fetcherlauncher-third-party-notices.txt",
                                                "ui/index.html",
                                                "ui/assets/fetcher-float.gif",
                                                "ui/assets/fetcher-float-right.gif",
                                                "ui/assets/potm2504a.jpg",
                                                "fetcher-tester-tools.json"};
    return names.count(lower(name)) != 0;
}
std::string hashIfFile(const fs::path &path) {
    noLinks(path);
    if (!fs::exists(path))
        return "";
    need(fs::is_regular_file(path), "Update path is not a regular file.");
    return sha256(path);
}
bool validId(const std::string &id) {
    return std::regex_match(id, std::regex("tx-(tools|client)-[0-9]+"));
}
fs::path backupPath(const fs::path &root, const std::string &id) {
    need(validId(id), "Invalid update backup identifier.");
    auto path = longPath(root / "_fetcher_launcher" / "rollback" / id);
    noLinks(path);
    return path;
}
Json journal(const fs::path &backup) {
    auto value = readJson(backup / "transaction.json");
    need(value.value("schemaVersion", 0) == 1 && value.at("files").is_array() &&
             value.at("files").size() <= 100000,
         "Unsupported update recovery journal.");
    need(value.at("kind") == "tools" || value.at("kind") == "client",
         "Unknown update recovery kind.");
    need(utf8(backup.filename().wstring())
                 .rfind("tx-" + value.at("kind").get<std::string>() + "-", 0) == 0,
         "Update recovery kind does not match its directory.");
    std::set<std::string> paths;
    for (const auto &record : value.at("files")) {
        auto name = record.at("path").get<std::string>();
        safeRelative(name);
        need(paths.insert(lower(name)).second &&
                 allowed(name, value.at("kind") == "tools", value.at("policy")),
             "Update recovery journal contains a protected or duplicate path.");
        for (auto key : {"before", "after"}) {
            auto hash = record.at(key).get<std::string>();
            need(hash.empty() || std::regex_match(hash, std::regex("[0-9a-f]{64}")),
                 "Invalid recovery file hash.");
        }
    }
    return value;
}
void atomicCopy(const fs::path &source, const fs::path &target, const fs::path &temporary) {
    noLinks(source);
    noLinks(target);
    noLinks(temporary);
    fs::create_directories(target.parent_path());
    fs::create_directories(temporary.parent_path());
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
    need(MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
         "Could not replace an update file; close this installation's game/launcher and retry.");
}
void restore(const fs::path &root, const fs::path &backup) {
    auto plan = journal(backup);
    // Validate every backup and current target before changing anything. Interrupted recovery
    // is idempotent: a target may contain either its old or its planned replacement bytes.
    for (const auto &record : plan.at("files")) {
        auto rel = safeRelative(record.at("path"));
        auto before = record.at("before").get<std::string>();
        auto after = record.at("after").get<std::string>();
        if (!before.empty())
            need(hashIfFile(backup / "files" / rel) == before,
                 "Backup is missing/corrupt; recovery stopped.");
        auto current = hashIfFile(root / rel);
        need(current == before || current == after,
             "An installed file changed after the update. Recovery stopped to preserve that edit.");
    }
    writeAtomic(backup / "restoring.json", "{\"restoring\":true}");
    std::size_t index = 0;
    for (auto record = plan.at("files").rbegin(); record != plan.at("files").rend(); ++record) {
        auto rel = safeRelative(record->at("path"));
        auto target = root / rel;
        if (record->at("before") != "")
            atomicCopy(backup / "files" / rel, target, backup / "work" / std::to_string(index++));
        else if (fs::exists(target)) {
            noLinks(target);
            fs::remove(target);
        }
    }
    auto receipt =
        root / "_fetcher_launcher" / (plan.at("kind") == "tools" ? "tools.json" : "client.json");
    noLinks(receipt);
    if (fs::exists(receipt))
        fs::remove(receipt);
    writeAtomic(backup / "rolled-back.json", "{\"rolledBack\":true}");
}
std::vector<fs::path> backups(const fs::path &root) {
    auto path = longPath(root / "_fetcher_launcher" / "rollback");
    noLinks(path);
    std::vector<fs::path> result;
    if (!fs::exists(path))
        return result;
    for (const auto &entry : fs::directory_iterator(path)) {
        auto id = utf8(entry.path().filename().wstring());
        if (validId(id)) {
            noLinks(entry.path());
            need(entry.is_directory(), "Invalid update backup directory.");
            result.push_back(entry.path());
        }
        need(result.size() <= 512,
             "Too many retained updates; archive old backups before updating.");
    }
    std::sort(result.begin(), result.end());
    return result;
}
}
void fixtureInstallCrashAfter(int files) {
    need(files >= 0 && files <= 100000, "Invalid fixture crash counter.");
    crashAfter = files;
}
Json listInstallBackups(const fs::path &root) {
    Json result = Json::array();
    for (const auto &backup : backups(root)) {
        if (!fs::exists(backup / "transaction.json") || !fs::exists(backup / "complete.json") ||
            fs::exists(backup / "rolled-back.json"))
            continue;
        auto plan = journal(backup);
        result.push_back({{"id", utf8(backup.filename().wstring())},
                          {"kind", plan.at("kind")},
                          {"files", plan.at("files").size()}});
    }
    std::reverse(result.begin(), result.end());
    return result;
}
Json recoverInstallTransactions(const fs::path &root) {
    InstallLock lock(root);
    Json recovered = Json::array();
    for (const auto &backup : backups(root)) {
        if (!fs::exists(backup / "transaction.json") || fs::exists(backup / "rolled-back.json"))
            continue;
        if (fs::exists(backup / "complete.json") && !fs::exists(backup / "restoring.json"))
            continue;
        assertGameStopped(root);
        auto plan = journal(backup);
        restore(root, backup);
        recovered.push_back(plan.at("kind"));
    }
    return recovered;
}
void restoreInstallBackup(const fs::path &root, const std::string &id) {
    InstallLock lock(root);
    assertGameStopped(root);
    auto backup = backupPath(root, id);
    need(fs::exists(backup / "complete.json") && !fs::exists(backup / "rolled-back.json"),
         "That update cannot be restored.");
    restore(root, backup);
}
void installTransaction(const fs::path &root, const fs::path &stage, const Json &policy,
                        bool tools) {
    InstallLock lock(root);
    assertGameStopped(root);
    recoverInstallTransactions(root);
    noLinks(root);
    noLinks(stage);
    auto manifest = tools ? "fetcher-tester-tools.json" : "fetcher-client-files.json";
    noLinks(stage / manifest);
    auto inventory = readJson(stage / manifest);
    need(inventory.value("schemaVersion", 0) == 1 && inventory.at("files").is_array() &&
             !inventory.at("files").empty() && inventory.at("files").size() <= 100000,
         "Invalid update inventory.");
    need(tools || fs::is_regular_file(stage / "openmw.exe"),
         "Client archive is missing openmw.exe.");
    Json records = inventory.at("files");
    std::set<std::string> names;
    for (const auto &record : records) {
        auto name = record.at("path").get<std::string>();
        auto rel = safeRelative(name);
        need(lower(name) != manifest && names.insert(lower(name)).second &&
                 allowed(name, tools, policy),
             "Update archive contains a protected/duplicate file.");
        need(fs::is_regular_file(stage / rel) &&
                 fs::file_size(stage / rel) == record.at("size").get<std::uintmax_t>() &&
                 hashIfFile(stage / rel) == record.at("sha256"),
             "Update inventory hash/size verification failed.");
    }
    if (fs::exists(root / manifest)) {
        noLinks(root / manifest);
        auto old = readJson(root / manifest);
        need(old.at("files").is_array() && old.at("files").size() <= 100000,
             "Invalid old update inventory.");
        for (const auto &record : old.at("files")) {
            auto name = record.at("path").get<std::string>();
            safeRelative(name);
            if (!names.count(lower(name)) && allowed(name, tools, policy)) {
                records.push_back({{"path", name}, {"sha256", ""}});
                names.insert(lower(name));
            }
        }
    }
    records.push_back({{"path", manifest}, {"sha256", sha256(stage / manifest)}});
    need(records.size() <= 100000, "Update transaction contains too many files.");
    auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    auto backup = backupPath(root, std::string("tx-") + (tools ? "tools-" : "client-") + stamp);
    fs::create_directories(backup);
    Json plan = {{"schemaVersion", 1},
                 {"kind", tools ? "tools" : "client"},
                 {"policy", policy},
                 {"files", Json::array()}};
    for (const auto &record : records) {
        auto name = record.at("path").get<std::string>();
        auto rel = safeRelative(name);
        auto before = hashIfFile(root / rel);
        if (!before.empty()) {
            fs::create_directories((backup / "files" / rel).parent_path());
            fs::copy_file(root / rel, backup / "files" / rel);
        }
        plan["files"].push_back(
            {{"path", name}, {"before", before}, {"after", record.at("sha256")}});
    }
    auto encodedPlan = plan.dump(2);
    need(encodedPlan.size() <= 8 * 1024 * 1024,
         "Update recovery journal exceeds the supported metadata limit.");
    writeAtomic(backup / "transaction.json", encodedPlan);
    try {
        std::size_t index = 0;
        for (const auto &record : plan.at("files")) {
            auto rel = safeRelative(record.at("path"));
            auto target = root / rel;
            if (record.at("after") != "")
                atomicCopy(stage / rel, target, backup / "work" / std::to_string(index));
            else if (fs::exists(target)) {
                noLinks(target);
                fs::remove(target);
            }
            ++index;
            if (crashAfter > 0 && index == static_cast<std::size_t>(crashAfter))
                TerminateProcess(GetCurrentProcess(), 87);
        }
        writeAtomic(backup / "complete.json", "{\"complete\":true}");
    } catch (...) {
        restore(root, backup);
        throw;
    }
}
}
