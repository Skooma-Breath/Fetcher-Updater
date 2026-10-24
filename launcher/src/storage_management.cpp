#include "storage_management.hpp"
#include "server_manifest.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
namespace fetcher {
namespace {
void need(bool ok, const std::string &message) {
    if (!ok) throw std::runtime_error(message);
}
bool digest(const std::string &value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::uint64_t receiptBytes(const Json &receipt) {
    std::uint64_t bytes = 0;
    for (const auto &file : receipt.value("files", Json::array())) {
        auto size = file.at("size").get<std::uint64_t>();
        need(size <= 8ull * 1024 * 1024 * 1024 && bytes <= 8ull * 1024 * 1024 * 1024 - size,
             "Cache receipt size exceeds its limit.");
        bytes += size;
    }
    return bytes;
}
Json sizeReceipt(const fs::path &path) {
    try {
        auto value = readPackageReceipt(path);
        if (!value.is_object() || !value.contains("files") || !value["files"].is_array() || value["files"].empty())
            return Json::object();
        receiptBytes(value);
        return value;
    } catch (const std::exception &) { return Json::object(); }
}
std::string cacheLabel(const Json &receipt) {
    for (const auto &file : receipt.value("files", Json::array())) {
        auto path = fs::path(wide(file.at("path").get<std::string>()));
        auto extension = lower(utf8(path.extension().wstring()));
        if (extension == ".esm" || extension == ".esp" || extension == ".omwaddon" || extension == ".omwscripts")
            return utf8(path.filename().wstring());
    }
    return "Asset files";
}
struct Tree {
    std::map<std::string, fs::path> files;
    std::vector<fs::path> directories;
    std::uint64_t bytes = 0;
    std::string snapshot;
};
Tree tree(const fs::path &root) {
    noLinks(root);
    Tree result;
    std::map<std::string, std::string> stamps;
    for (const auto &entry : fs::recursive_directory_iterator(longPath(root))) {
        noLinks(entry.path());
        need(result.files.size() + result.directories.size() < 200000, "Cache tree is too large.");
        if (entry.is_directory()) { result.directories.push_back(entry.path()); continue; }
        need(entry.is_regular_file(), "Cache contains a special file.");
        auto name = utf8(entry.path().lexically_relative(longPath(root)).generic_wstring());
        safeRelative(name);
        auto key = lower(name);
        need(result.files.emplace(key, entry.path()).second, "Duplicate cache file.");
        auto size = entry.file_size();
        result.bytes += size;
        stamps[key] = std::to_string(size) + ":" +
                      std::to_string(entry.last_write_time().time_since_epoch().count());
    }
    result.snapshot = textSha256(Json(stamps).dump());
    return result;
}
bool owns(const fs::path &root, const std::string &kind, const std::string &key,
          const Json &receipt, const Tree &files, bool verify) {
    noLinks(root);
    if (receipt.value(kind == "packages" ? "sha256" : "layoutKey", "") != key ||
        !receipt.contains("files") || !receipt["files"].is_array() || receipt["files"].empty() ||
        receipt["files"].size() > 100000 || files.files.size() != receipt["files"].size() + 1)
        return false;
    receiptBytes(receipt);
    std::set<std::string> seen;
    for (const auto &record : receipt["files"]) {
        auto name = record.at("path").get<std::string>();
        safeRelative(name);
        auto normalized = lower(name);
        if (normalized == ".fetcher-package.json" || !seen.insert(normalized).second ||
            !files.files.count(normalized)) return false;
        auto path = files.files.at(normalized);
        noLinks(path);
        if (fs::file_size(path) != record.at("size").get<std::uint64_t>()) return false;
        auto expected = record.at("sha256").get<std::string>();
        if (!digest(expected) || (verify && sha256(path) != expected)) return false;
    }
    return files.files.count(".fetcher-package.json") != 0;
}
struct References {
    fs::path root;
    std::set<std::string> hashes;
    std::set<std::string> visited;
    std::map<std::string, std::string> releases;
    StorageResolver resolve;
    std::uint64_t bytes = 0;
    unsigned files = 0;
    void dataRoot(fs::path target) {
        auto managed = lower(utf8(fs::weakly_canonical(root / "_fetcher_launcher").generic_wstring()));
        auto value = lower(utf8(fs::weakly_canonical(fs::absolute(target)).generic_wstring()));
        while (value.size() > 3 && value.back() == '/') value.pop_back();
        auto prefix = value.back() == '/' ? value : value + "/";
        need(managed != value && managed.rfind(prefix, 0) != 0,
             "A saved data directory includes the whole managed cache. Cleanup is disabled.");
        for (auto kind : {"packages", "layouts", "downloads"}) {
            auto cache = managed + "/" + kind;
            need(cache != value && cache.rfind(prefix, 0) != 0,
                 "A saved data directory includes a whole cache category. Cleanup is disabled.");
        }
        if (value.rfind(managed + "/", 0) == 0) textHashes(value);
    }
    void profile(const Json &value) {
        if (!value.is_object() || !value.contains("packages")) return;
        need(value["packages"].is_array() && value["packages"].size() <= 128,
             "Saved profile package list is invalid.");
        for (const auto &definition : value["packages"]) {
            std::string hash = lower(definition.value("sha256", ""));
            if (!digest(hash)) {
                need(definition.contains("repository"), "Saved package has no resolvable hash.");
                auto key = definition.at("repository").get<std::string>() + "\n" +
                           definition.at("releaseTag").get<std::string>() + "\n" +
                           definition.at("assetName").get<std::string>();
                need(releases.size() < 512, "Too many saved release references.");
                if (!releases.count(key)) releases[key] = lower(resolve(definition).at("sha256"));
                hash = releases.at(key);
            }
            need(digest(hash), "Saved package hash is invalid.");
            hashes.insert(hash);
            if (definition.contains("fomod") || !definition.value("actions", Json::array()).empty())
                hashes.insert(modLayoutKey(definition, hash));
        }
    }
    void json(const Json &value) {
        if (value.is_string()) {
            auto text = lower(value.get<std::string>());
            if (digest(text)) hashes.insert(text);
            else if (text.rfind("sha256:", 0) == 0 && digest(text.substr(7))) hashes.insert(text.substr(7));
            else if (text.find("_fetcher_launcher") != std::string::npos) textHashes(text);
        } else if (value.is_object()) {
            profile(value);
            for (const auto &item : value.items()) json(item.value());
        } else if (value.is_array()) for (const auto &item : value) json(item);
        need(hashes.size() <= 100000, "Too many saved cache references.");
    }
    void textHashes(const std::string &text) {
        static const std::regex pattern("[0-9a-fA-F]{64}");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern);
             it != std::sregex_iterator(); ++it) hashes.insert(lower(it->str()));
    }
    void file(const fs::path &path) {
        noLinks(path);
        if (!fs::is_regular_file(path)) return;
        if (!visited.insert(lower(utf8(fs::absolute(path).lexically_normal().wstring()))).second) return;
        need(++files <= 10000, "Too many saved configuration files.");
        auto text = read(path, 8 * 1024 * 1024);
        bytes += text.size();
        need(bytes <= 64 * 1024 * 1024, "Saved configuration scan exceeds its limit.");
        if (lower(path.extension().string()) == ".json") {
            auto value = parseJson(text);
            json(value);
            if (lower(utf8(fs::absolute(path).lexically_normal().wstring())) ==
                lower(utf8(fs::absolute(root / "_fetcher_launcher/settings.json").lexically_normal().wstring())) &&
                value.contains("localMods")) {
                for (const auto &mods : value["localMods"].items())
                    for (const auto &mod : mods.value()) {
                        auto directory = fs::path(wide(mod.at("directory").get<std::string>()));
                        if (directory.is_relative()) directory = root / directory;
                        dataRoot(directory);
                    }
            }
        }
        else {
            textHashes(text);
            std::istringstream input(text); std::string line;
            static const std::regex include("^\\s*(config|data|data-local)\\s*=\\s*(.*?)\\s*$");
            while (std::getline(input, line)) {
                std::smatch match;
                if (!std::regex_match(line, match, include)) continue;
                auto setting = match[1].str();
                auto value = match[2].str();
                if (!value.empty() && value.front() == '"') {
                    std::istringstream quoted(value); std::string decoded;
                    quoted >> std::quoted(decoded, '"', '&');
                    need(bool(quoted), "Saved configuration has an invalid quoted path.");
                    value = decoded;
                } else {
                    value = value.substr(0, value.find('#'));
                    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
                }
                need(value.find_first_of("?%") == std::string::npos,
                     "Saved configuration has an include that cannot be resolved safely.");
                if (value.empty()) continue;
                auto target = fs::path(wide(value));
                if (target.is_relative()) target = path.parent_path() / target;
                if (setting != "config") { dataRoot(target); continue; }
                if (fs::is_directory(target)) file(target / "openmw.cfg");
                else if (fs::is_regular_file(target)) file(target);
            }
        }
    }
    void directory(const fs::path &path) {
        noLinks(path);
        if (!fs::exists(path)) return;
        unsigned entries = 0;
        for (auto it = fs::recursive_directory_iterator(longPath(path));
             it != fs::recursive_directory_iterator(); ++it) {
            need(++entries <= 200000, "Saved configuration tree exceeds its limit.");
            noLinks(it->path());
            auto name = lower(utf8(it->path().filename().wstring()));
            if (it->is_directory() && (name == "logs" || name == "screenshots" || name == "saves")) {
                it.disable_recursion_pending(); continue;
            }
            auto ext = lower(it->path().extension().string());
            if (it->is_regular_file() && (ext == ".cfg" || ext == ".json")) file(it->path());
        }
    }
};
fs::path candidatePath(const fs::path &root, const Json &candidate) {
    auto kind = candidate.at("kind").get<std::string>();
    auto key = candidate.at("key").get<std::string>();
    need((kind == "packages" || kind == "layouts" || kind == "downloads") && digest(key),
         "Invalid cleanup candidate.");
    auto base = fs::absolute(root / "_fetcher_launcher" / kind).lexically_normal();
    auto target = base / wide(key + (kind == "downloads" ? ".zip" : ""));
    noLinks(base); noLinks(target);
    auto resolved = fs::weakly_canonical(target);
    need(lower(utf8(resolved.parent_path().wstring())) == lower(utf8(fs::weakly_canonical(base).wstring())),
         "Cleanup target escaped its cache directory.");
    return longPath(target);
}
}
Json estimateStorage(const fs::path &root, const Json &profile, const StorageResolver &resolve) {
    Json result = {{"profile", profile.value("id", "")}, {"managed", profile.value("mode", "managed") != "installed"}, {"downloadBytes", 0},
                   {"extractionBytes", 0}, {"layoutBytes", 0}, {"unknownDownloads", 0},
                   {"unknownExtraction", 0}, {"unknownLayouts", 0}, {"cachedBytes", 0}};
    std::set<std::string> sources, layouts;
    for (const auto &definition : profile.value("packages", Json::array())) {
        std::string hash = lower(definition.value("sha256", ""));
        if (!digest(hash)) hash = lower(resolve(definition).at("sha256"));
        auto raw = root / "_fetcher_launcher/packages" / wide(hash);
        auto receipt = sizeReceipt(longPath(raw));
        std::uint64_t expanded = receipt.value("sha256", "") == hash ? receiptBytes(receipt) : 0;
        bool present = receipt.value("sha256", "") == hash && receipt.contains("files");
        if (sources.insert(hash).second) {
            result["cachedBytes"] = result["cachedBytes"].get<std::uint64_t>() + expanded;
            auto archive = longPath(root / "_fetcher_launcher/downloads" / wide(hash + ".zip"));
            noLinks(archive);
            if (fs::is_regular_file(archive))
                result["cachedBytes"] = result["cachedBytes"].get<std::uint64_t>() + fs::file_size(archive);
            if (!present && !fs::is_regular_file(archive)) {
                if (definition.contains("downloadBytes"))
                    result["downloadBytes"] = result["downloadBytes"].get<std::uint64_t>() + definition.at("downloadBytes").get<std::uint64_t>();
                else result["unknownDownloads"] = result["unknownDownloads"].get<unsigned>() + 1;
            }
            if (!present) {
                if (definition.contains("expandedBytes"))
                    result["extractionBytes"] = result["extractionBytes"].get<std::uint64_t>() + definition.at("expandedBytes").get<std::uint64_t>();
                else result["unknownExtraction"] = result["unknownExtraction"].get<unsigned>() + 1;
            }
        }
        if (definition.contains("fomod") || !definition.value("actions", Json::array()).empty()) {
            auto key = modLayoutKey(definition, hash);
            if (!layouts.insert(key).second) continue;
            auto derived = sizeReceipt(longPath(root / "_fetcher_launcher/layouts" / wide(key)));
            if (derived.value("layoutKey", "") == key) {
                result["cachedBytes"] = result["cachedBytes"].get<std::uint64_t>() + receiptBytes(derived);
            } else if (expanded || definition.contains("expandedBytes")) {
                result["layoutBytes"] = result["layoutBytes"].get<std::uint64_t>() +
                                        (expanded ? expanded : definition.at("expandedBytes").get<std::uint64_t>());
                result["unknownLayouts"] = result["unknownLayouts"].get<unsigned>() + 1;
            } else result["unknownLayouts"] = result["unknownLayouts"].get<unsigned>() + 1;
        }
    }
    return result;
}
Json scanUnusedCache(const fs::path &root, const StorageResolver &resolve) {
    InstallLock lock(root);
    Json result = {{"candidates", Json::array()}, {"bytes", 0}, {"retained", 0}, {"canClean", false}};
    auto managed = root / "_fetcher_launcher";
    noLinks(managed);
    References references; references.root = root; references.resolve = resolve;
    try {
        for (auto name : {"openmw.cfg", "settings.cfg", "fetcher-launcher-profiles.json",
                          "fetcher-update-state.json", "fetcher-simulator-umo.json"}) references.file(root / name);
        for (auto name : {"settings.json", "pending-profile.json", "tools.json", "client.json"}) references.file(managed / name);
        for (auto name : {"profiles", "rollback"}) references.directory(managed / name);
        references.directory(root / "profiles");
        auto layouts = managed / "layouts";
        noLinks(layouts);
        if (fs::exists(layouts)) for (const auto &entry : fs::directory_iterator(longPath(layouts))) {
            auto key = lower(utf8(entry.path().filename().wstring()));
            if (references.hashes.count(key)) {
                noLinks(entry.path());
                auto receipt = readPackageReceipt(entry.path());
                auto source = lower(receipt.value("sourceSha256", ""));
                need(digest(source), "Referenced layout has no valid source receipt.");
                references.hashes.insert(source);
            }
        }
    } catch (const std::exception &error) {
        result["message"] = std::string("Cleanup disabled: saved references could not be checked. ") + error.what();
        return result;
    }
    std::map<std::string, std::string> labels;
    for (auto kind : {"packages", "layouts", "downloads"}) {
        auto base = managed / kind;
        noLinks(base);
        if (!fs::exists(base)) continue;
        for (const auto &entry : fs::directory_iterator(longPath(base))) {
            auto name = utf8(entry.path().filename().wstring());
            auto key = lower(name);
            if (std::string(kind) == "downloads") {
                if (key.size() != 68 || key.substr(64) != ".zip") continue;
                key.resize(64);
            }
            if (!digest(key) || name != key + (std::string(kind) == "downloads" ? ".zip" : "")) continue;
            if (references.hashes.count(key)) { result["retained"] = result["retained"].get<unsigned>() + 1; continue; }
            try {
                noLinks(entry.path());
                Json candidate = {{"kind", kind}, {"key", key}};
                if (std::string(kind) == "downloads") {
                    need(entry.is_regular_file(), "Archive cache entry is not a file.");
                    need(entry.file_size() <= 4ull * 1024 * 1024 * 1024, "Archive cache size exceeds its limit.");
                    candidate["bytes"] = entry.file_size(); candidate["files"] = 1;
                    candidate["snapshot"] = std::to_string(entry.last_write_time().time_since_epoch().count());
                    candidate["label"] = labels.count(key) ? labels.at(key) : "Downloaded archive";
                } else {
                    need(entry.is_directory(), "Package cache entry is not a directory.");
                    auto receipt = readPackageReceipt(entry.path());
                    auto files = tree(entry.path());
                    need(owns(entry.path(), kind, key, receipt, files, false), "Cache has unknown or changed files.");
                    candidate["bytes"] = files.bytes; candidate["files"] = files.files.size();
                    candidate["snapshot"] = files.snapshot;
                    candidate["receiptHash"] = sha256(entry.path() / ".fetcher-package.json");
                    candidate["label"] = cacheLabel(receipt);
                    labels[key] = candidate["label"].get<std::string>();
                }
                need(result["candidates"].size() < 10000, "Too many cleanup candidates.");
                result["bytes"] = result["bytes"].get<std::uint64_t>() + candidate["bytes"].get<std::uint64_t>();
                result["candidates"].push_back(std::move(candidate));
            } catch (const std::exception &) { result["retained"] = result["retained"].get<unsigned>() + 1; }
        }
    }
    std::sort(result["candidates"].begin(), result["candidates"].end(), [](const Json &a, const Json &b) {
        return a.at("kind").get<std::string>() + a.at("key").get<std::string>() < b.at("kind").get<std::string>() + b.at("key").get<std::string>();
    });
    result["canClean"] = !result["candidates"].empty();
    result["token"] = textSha256(Json{{"root", lower(utf8(fs::absolute(root).wstring()))},
                                      {"references", references.hashes}, {"candidates", result["candidates"]}}.dump());
    result["message"] = "Only unused managed caches are listed. Saved profiles, rollback data and manual mod folders are retained.";
    return result;
}
Json cleanUnusedCache(const fs::path &root, const std::string &token, const StorageResolver &resolve) {
    InstallLock lock(root);
    assertGameStopped(root);
    auto plan = scanUnusedCache(root, resolve);
    need(plan.value("canClean", false) && plan.value("token", "") == token,
         "Storage changed or cleanup is unavailable. Scan the cache again.");
    // Validate every candidate before deleting anything, including same-size edits.
    for (const auto &candidate : plan["candidates"]) {
        auto path = candidatePath(root, candidate);
        auto kind = candidate.at("kind").get<std::string>();
        if (kind == "downloads") need(sha256(path) == candidate.at("key"), "Cached archive changed; nothing removed.");
        else {
            auto files = tree(path);
            need(owns(path, kind, candidate.at("key"), readPackageReceipt(path), files, true),
                 "Cached files were modified; nothing removed.");
        }
    }
    auto fresh = scanUnusedCache(root, resolve);
    need(fresh.value("token", "") == token && fresh.value("canClean", false), "Saved references changed. Scan again.");
    for (const auto &candidate : fresh["candidates"]) {
        auto path = candidatePath(root, candidate);
        if (candidate.at("kind") == "downloads") { noLinks(path); fs::remove(path); continue; }
        auto files = tree(path);
        need(files.snapshot == candidate.at("snapshot"), "Cache changed during cleanup.");
        for (const auto &[name, file] : files.files) {
            if (name == ".fetcher-package.json") continue;
            noLinks(file); fs::remove(file);
        }
        auto receipt = path / ".fetcher-package.json";
        noLinks(receipt); fs::remove(receipt);
        std::sort(files.directories.begin(), files.directories.end(), [](const fs::path &a, const fs::path &b) { return a.native().size() > b.native().size(); });
        for (const auto &directory : files.directories) { noLinks(directory); fs::remove(directory); }
        noLinks(path); fs::remove(path);
    }
    return {{"removedEntries", fresh["candidates"].size()}, {"removedLogicalBytes", fresh["bytes"]}};
}
}
