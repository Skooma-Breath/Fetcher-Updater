#include "launcher_runtime.hpp"
#include <map>
#include <stdexcept>
namespace fetcher {
namespace {
void need(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
}
Json launcherRuntime(const fs::path &executable, const fs::path &uiDirectory) {
    std::map<std::string, fs::path> paths;
    paths["FetcherLauncher.exe"] = executable;
    noLinks(uiDirectory);
    need(fs::is_directory(uiDirectory), "Launcher UI directory is missing.");
    for (const auto &entry : fs::recursive_directory_iterator(uiDirectory)) {
        noLinks(entry.path());
        if (entry.is_regular_file()) {
            need(paths.size() < 4096, "Too many launcher interface files.");
            auto relative = utf8(entry.path().lexically_relative(uiDirectory).generic_wstring());
            safeRelative(relative);
            paths["ui/" + relative] = entry.path();
        }
    }
    need(paths.count("ui/index.html") != 0, "Launcher interface is missing.");
    Json files = Json::object();
    std::uintmax_t total = 0;
    for (const auto &[name, path] : paths) {
        noLinks(path);
        need(fs::is_regular_file(path), "Launcher executable/interface file is missing.");
        auto size = fs::file_size(path);
        need(size <= 128 * 1024 * 1024, "Launcher runtime file is too large.");
        total += size;
        need(total <= 256 * 1024 * 1024, "Launcher runtime is too large.");
        files[name] = {{"size", size}, {"sha256", sha256(path)}};
    }
    return files;
}
bool stageLauncherRuntime(const fs::path &root, const fs::path &stage) {
    auto runtime = launcherRuntime(root / "FetcherLauncher.exe", root / "ui");
    auto inventory = readJson(root / "fetcher-tester-tools.json");
    need(inventory.value("schemaVersion", 0) == 1 && inventory.at("files").is_array() &&
             inventory.at("files").size() <= 100000, "Invalid launcher tools inventory.");
    Json verified = Json::object();
    for (const auto &record : inventory.at("files")) {
        auto name = record.at("path").get<std::string>();
        safeRelative(name);
        if (name != "FetcherLauncher.exe" && name.rfind("ui/", 0) != 0) continue;
        need(runtime.contains(name), "An inventoried launcher runtime file is missing.");
        need(!verified.contains(name), "Duplicate launcher runtime inventory entry.");
        Json expected = {{"size", record.at("size")}, {"sha256", record.at("sha256")}};
        need(expected == runtime.at(name), "Updated launcher files do not match their verified inventory.");
        verified[name] = expected;
    }
    need(verified == runtime, "Launcher runtime contains unverified or missing interface files.");
    noLinks(stage);
    need(!fs::exists(stage), "Launcher restart staging directory already exists.");
    fs::create_directories(stage);
    try {
        for (auto it = runtime.begin(); it != runtime.end(); ++it) {
            auto relative = safeRelative(it.key());
            fs::create_directories((stage / relative).parent_path());
            fs::copy_file(root / relative, stage / relative);
        }
        need(launcherRuntime(stage / "FetcherLauncher.exe", stage / "ui") == runtime,
             "Launcher files changed while preparing restart.");
    } catch (...) {
        removeManaged(stage.parent_path(), stage);
        throw;
    }
    return inventory.value("launcherRestartHandshake", 0) == 1;
}
}
