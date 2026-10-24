#include "native_backend.hpp"
#include "install_transaction.hpp"
#include "server_manifest.hpp"
#include "umo_helper.hpp"
#include "storage_management.hpp"
#include "server_ping.hpp"
#include "legacy_migration.hpp"
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <shellapi.h>
#include <sstream>
namespace fetcher {
namespace {
void need(bool b, const std::string &m) {
    if (!b)
        throw std::runtime_error(m);
}
Json optional(const fs::path &p, Json fallback = Json::object()) {
    return fs::exists(p) ? readJson(p) : fallback;
}
std::string id(const Json &value) {
    auto s = value.get<std::string>();
    need(std::regex_match(s, std::regex("[a-z0-9][a-z0-9-]{0,63}")),
         "Invalid profile/package identifier.");
    return s;
}
std::string stamp() {
    return std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
}
std::string cfgString(const fs::path &p) {
    auto s = utf8(fs::absolute(ordinaryPath(p)).generic_wstring());
    need(s.find_first_of("\"\r\n") == std::string::npos, "Invalid configuration path.");
    std::ostringstream quoted;
    quoted << std::quoted(s, '"', '&');
    return quoted.str();
}
std::vector<std::string> lines(const fs::path &p) {
    std::istringstream in(read(p));
    std::vector<std::string> out;
    std::string s;
    while (std::getline(in, s)) {
        if (!s.empty() && s.back() == '\r')
            s.pop_back();
        out.push_back(s);
    }
    return out;
}
std::vector<fs::path> configuredData(const fs::path &root) {
    std::vector<fs::path> result;
    if (!fs::exists(root / "openmw.cfg"))
        return result;
    std::regex pattern("^\\s*data\\s*=\\s*\"?([^\"]+?)\"?\\s*$");
    for (const auto &s : lines(root / "openmw.cfg")) {
        std::smatch m;
        if (std::regex_match(s, m, pattern)) {
            fs::path p = wide(m[1]);
            if (p.is_relative())
                p = root / p;
            result.push_back(p.lexically_normal());
        }
    }
    return result;
}
bool protectedPath(const std::string &path, const Json &policy, bool protectModData = true) {
    auto p = lower(path);
    if (p == "openmw.cfg" || p == "settings.cfg" || p == "server.cfg" || p == "playerdata.db" ||
        p == "server-lua-storage.bin" || p == "fetcherlauncher.exe" || p == "fetchersetup.exe" ||
        p == "fetcher-launcher-profiles.json" || p == "fetcher-update-state.json")
        return true;
    for (auto prefix : {"_fetcher_launcher/", "_fetcher_update/", "profiles/", "mp-keys/",
                        "userdata/", "saves/", "screenshots/", "logs/", "ui/"})
        if (p.rfind(prefix, 0) == 0)
            return true;
    if (protectModData && p.rfind("data files/", 0) == 0)
        return true;
    for (const auto &e : policy.value("exactPaths", Json::array()))
        if (p == lower(e.get<std::string>()))
            return true;
    for (const auto &e : policy.value("prefixes", Json::array()))
        if (p.rfind(lower(e.get<std::string>()), 0) == 0)
            return true;
    return false;
}
bool verifyFiles(const fs::path &root, const Json &records, bool hash) {
    if (!records.is_array() || records.empty())
        return false;
    std::set<std::string> seen;
    for (const auto &r : records) {
        auto name = r.at("path").get<std::string>();
        auto p = root / safeRelative(name);
        noLinks(p);
        if (!seen.insert(lower(name)).second || !fs::is_regular_file(p) ||
            fs::file_size(p) != r.at("size").get<std::uintmax_t>() ||
            (hash && sha256(p) != r.at("sha256").get<std::string>()))
            return false;
    }
    return true;
}
std::string cacheWriteTime(const WIN32_FILE_ATTRIBUTE_DATA &info) {
    return std::to_string((std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) |
                          info.ftLastWriteTime.dwLowDateTime);
}
std::uint64_t cacheFileSize(const WIN32_FILE_ATTRIBUTE_DATA &info) {
    return (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}
bool cacheAttributes(const fs::path &path, WIN32_FILE_ATTRIBUTE_DATA &info) {
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
        return false;
    need(!(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
         "Managed cache paths cannot contain symlinks/junctions.");
    return true;
}
Json cacheRecord(const fs::path &path, const std::string &name) {
    WIN32_FILE_ATTRIBUTE_DATA before{}, after{};
    need(cacheAttributes(path, before) && !(before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY),
         "Cache file disappeared during verification.");
    auto digest = sha256(path);
    need(cacheAttributes(path, after) && cacheWriteTime(before) == cacheWriteTime(after) &&
             cacheFileSize(before) == cacheFileSize(after),
         "Cache file changed during verification.");
    return {{"path", name}, {"size", cacheFileSize(after)}, {"sha256", digest},
            {"verifiedWriteTime", cacheWriteTime(after)}};
}
bool verifyPackageCache(const fs::path &root, Json &receipt, bool hashChanges = true,
                        bool fullHash = false) {
    auto &records = receipt["files"];
    if (!records.is_array() || records.empty())
        return false;
    noLinks(root);
    std::set<std::string> names;
    std::set<fs::path> checkedParents;
    bool changed = false;
    for (auto &record : records) {
        const auto name = record.at("path").get<std::string>();
        if (!names.insert(lower(name)).second)
            return false;
        const auto relative = safeRelative(name);
        auto path = longPath(root);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        // Check every leaf and each distinct parent beneath the already-checked
        // cache root, avoiding a full ancestor walk for each of 100k files.
        for (auto it = relative.begin(); it != relative.end(); ++it) {
            path /= *it;
            if (std::next(it) != relative.end() && !checkedParents.insert(path).second)
                continue;
            if (!cacheAttributes(path, info))
                return false;
        }
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            cacheFileSize(info) != record.at("size").get<std::uint64_t>())
            return false;
        const auto stamp = cacheWriteTime(info);
        const auto prior = record.value("verifiedWriteTime", "");
        if (!hashChanges && !prior.empty() && prior != stamp)
            return false;
        if (hashChanges && (fullHash || prior != stamp)) {
            if (sha256(path) != record.at("sha256").get<std::string>())
                return false;
            WIN32_FILE_ATTRIBUTE_DATA after{};
            if (!cacheAttributes(path, after) || cacheWriteTime(after) != stamp ||
                cacheFileSize(after) != cacheFileSize(info))
                return false;
            record["verifiedWriteTime"] = stamp;
            changed = true;
        }
    }
    // Old receipts receive metadata only after their complete hash check passed.
    if (changed)
        writeAtomic(root / ".fetcher-package.json", receipt.dump(2));
    return true;
}
std::vector<std::string> masters(const fs::path &p) {
    if (lower(p.extension().string()) == ".omwscripts")
        return {};
    std::ifstream f(p, std::ios::binary);
    char tag[4];
    std::uint32_t size = 0;
    f.read(tag, 4);
    f.read(reinterpret_cast<char *>(&size), 4);
    need(bool(f) && std::string(tag, 4) == "TES3" && size <= 2 * 1024 * 1024,
         "Invalid TES3 plugin: " + utf8(p.filename().wstring()));
    f.seekg(16);
    std::vector<char> data(size);
    f.read(data.data(), size);
    need(bool(f), "Truncated TES3 plugin header.");
    std::vector<std::string> out;
    for (size_t at = 0; at + 8 <= data.size();) {
        std::string key(data.data() + at, 4);
        std::uint32_t n = 0;
        std::memcpy(&n, data.data() + at + 4, 4);
        at += 8;
        need(n <= data.size() - at, "Invalid plugin master subrecord.");
        if (key == "MAST") {
            std::string value(data.data() + at, n);
            while (!value.empty() && value.back() == 0)
                value.pop_back();
            out.push_back(value);
        }
        at += n;
    }
    return out;
}
Json builtinProfiles() {
    Json catalog = {{"schemaVersion", 1},
            {"profiles",
             Json::array(
                 {{{"id", "installed"},
                   {"name", "Existing installation"},
                   {"mode", "installed"},
                   {"description", "Use the current mod setup and load order. No "
                                   "automatic mod changes."},
                   {"packages", Json::array()},
                   {"content", Json::array()}},
                  {{"id", "fetcher-vanilla"},
                   {"name", "Fetcher Vanilla (current public server)"},
                   {"mode", "managed"},
                   {"host", "163.192.106.225"},
                   {"port", 25569},
                   {"description", "Vanilla Morrowind with Fetcher surf maps, vehicles and Studio instruments."},
                   {"packages", Json::array()},
                   {"content", Json::array({"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"})}},
                  {{"id", "fetcher-vanilla-studio-preview"},
                   {"name", "Vanilla + Surf / Truck / Studio (preview)"},
                   {"mode", "managed"},
                   {"description", "Candidate content for the next vanilla-server rollout. "
                                   "Explicitly select this only for a matching staged server; "
                                   "public content has not changed."},
                   {"packages",
                    Json::array({{{"id", "fetcher-content"},
                                  {"url", "https://github.com/Skooma-Breath/Fetcher-Updater/"
                                          "releases/download/openmw-client-mods-mp-clients/"
                                          "openmw-client-mods-studio-candidate.zip"},
                                  {"sha256", "8ad68f4741d08560cb5e6cb7887fc834ca7ad6408847fcf"
                                             "182499e638a822fdb"},
                                  {"dataDirs", Json::array({"Data Files"})}}})},
                   {"content", Json::array({"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm",
                                            "surf_mesa_mw.omwaddon", "surf_utopia_mw.omwaddon",
                                            "surf_kitsune.omwaddon", "surf_kitsune.omwscripts",
                                            "surf_kitsune2.omwaddon", "mp_phase7_test.omwscripts",
                                            "FetcherVehicles.omwaddon"})},
                   {"requiredFiles", Json::array({"meshes/fetcher/synth/keyboard-stand.nif",
                                                  "icons/fetcher/synth/keyboard.dds",
                                                  "FetcherStudio/ATTRIBUTION.txt",
                                                  "FetcherVehicles/CREDITS.txt"})}}})}};
    auto &vanilla = catalog["profiles"][1];
    const auto &studio = catalog["profiles"][2];
    for (const auto *key : {"packages", "content", "requiredFiles"})
        vanilla[key] = studio.at(key);
    return catalog;
}
} // namespace
bool protectedClientPath(const std::string &name, const Json &policy) {
    // The public Vehicles client owns these three menu images in Data Files.
    // Keep arbitrary mods protected and retain explicit operator policy overrides.
    static const std::set<std::string> clientBranding = {
        "data files/textures/menu_mainmenu.dds", "data files/textures/menu_mainmenu_over.dds",
        "data files/textures/menu_mainmenu_pressed.dds"};
    return protectedPath(name, policy, clientBranding.count(lower(name)) == 0);
}
Backend::Backend(fs::path root, Log log, DiscoveryFetch discoveryFetch)
    : mRoot(fs::absolute(root).lexically_normal()), mManaged(mRoot / L"_fetcher_launcher"),
      mLog(std::move(log)), mDiscoveryFetch(discoveryFetch ? std::move(discoveryFetch) : httpGet) {
    noLinks(mManaged);
    recoverInstallTransactions(mRoot);
    mSettings = optional(mManaged / "settings.json", {{"schemaVersion", 1},
                                                      {"profile", "installed"},
                                                      {"host", ""},
                                                      {"port", 25565},
                                                      {"favorites", Json::array()},
                                                      {"localMods", Json::object()}});
    mReview = optional(mManaged / "pending-profile.json");
    mCatalog = optional(mRoot / "fetcher-launcher-profiles.json", builtinProfiles());
    need(mCatalog.value("schemaVersion", 0) == 1 && mCatalog.at("profiles").is_array() &&
             mCatalog["profiles"].size() <= 256,
         "Unsupported launcher profile catalog.");
    std::set<std::string> ids;
    for (const auto &p : mCatalog["profiles"]) {
        validateProfile(p);
        need(ids.insert(id(p.at("id"))).second, "Duplicate profile identifier.");
    }
    if (!ids.count(mSettings.value("profile", "installed")))
        mSettings["profile"] = mCatalog["profiles"].at(0).at("id");
}
void Backend::save() { writeAtomic(mManaged / "settings.json", mSettings.dump(2)); }
Json Backend::identity() const {
    auto host = mSettings.value("host", "");
    if (host.empty())
        return Json::object();
    return mSettings.value("identities", Json::object())
        .value(endpoint(host, mSettings.value("port", 25565)), Json::object());
}
void Backend::saveIdentity(const Json &request) {
    auto host = mSettings.value("host", "");
    need(!host.empty(), "Select a multiplayer server first.");
    auto account = request.value("account", ""), character = request.value("character", "");
    for (const auto &name : {account, character}) {
        need(name.size() <= 128, "Account and character names support at most 128 UTF-8 bytes.");
        for (unsigned char byte : name)
            need(byte >= 32 && byte != 127, "Invalid account/character name.");
    }
    mSettings["identities"][endpoint(host, mSettings.value("port", 25565))] = {
        {"account", account},
        {"character", character},
        {"autoEnter", request.value("autoEnter", true)}};
    save();
}
void Backend::validateProfile(const Json &p) const {
    id(p.at("id"));
    need(p.at("name").get<std::string>().size() <= 160, "Profile name too long.");
    auto mode = p.value("mode", "managed");
    need(mode == "managed" || mode == "installed", "Unknown profile mode.");
    need(p.value("packages", Json::array()).is_array() &&
             p.value("packages", Json::array()).size() <= 128,
         "Too many packages.");
    if (p.contains("host"))
        endpoint(p.at("host"), p.at("port"));
    need(p.value("version", "unversioned").size() <= 128, "Profile version too long.");
    std::set<std::string> identifiers;
    for (const auto &d : p.value("packages", Json::array())) {
        need(identifiers.insert(id(d.at("id"))).second, "Duplicate package ID.");
        validateModDefinition(d);
    }
    need(p.value("content", Json::array()).is_array() &&
             p.value("content", Json::array()).size() <= 1024,
         "Too many required plugins.");
    std::set<std::string> content;
    for (const auto &plugin : p.value("content", Json::array())) {
        auto s = plugin.get<std::string>();
        need(safeRelative(s).filename() == safeRelative(s), "Plugins must be filenames.");
        need(content.insert(lower(s)).second, "Duplicate profile plugin.");
    }
    for (auto key : {"requiredFiles", "groundcover", "fallbackArchives"}) {
        auto list = p.value(key, Json::array());
        need(list.is_array() && list.size() <= 4096, "Invalid profile file list.");
        for (const auto &value : list) {
            auto path = safeRelative(value);
            if (std::string(key) != "requiredFiles")
                need(path.filename() == path, "Groundcover/archive entries must be filenames.");
        }
    }
}
Json Backend::profile() const {
    for (const auto &p : mCatalog.at("profiles"))
        if (p.at("id") == mSettings.value("profile", "installed"))
            return p;
    throw std::runtime_error("Selected mod profile is missing.");
}
Json Backend::state() const {
    Json profiles = Json::array();
    for (const auto &p : mCatalog.at("profiles"))
        profiles.push_back(p);
    auto current = profile();
    bool gameReady = false;
    try {
        gameData();
        gameReady = true;
    } catch (const std::exception &) {
    }
    bool existing = false;
    if (fs::is_regular_file(mRoot / "openmw.cfg"))
        for (const auto &line : lines(mRoot / "openmw.cfg"))
            existing = existing || (line.rfind("content=", 0) == 0 && line.size() > 8);
    bool prepared =
        current.value("mode", "managed") == "installed"
            ? existing
            : fs::is_regular_file(mManaged / "profiles" / id(current.at("id")) / "openmw.cfg");
    Json mods = Json::array();
    for (const auto &name : current.value("content", Json::array()))
        mods.push_back({{"name", name}, {"required", true}, {"enabled", true}});
    for (const auto &m : mSettings.value("localMods", Json::object())
                             .value(current.at("id").get<std::string>(), Json::array()))
        mods.push_back(m);
    return {
        {"installRoot", utf8(mRoot.wstring())},
        {"profiles", profiles},
        {"selectedProfile", current.at("id")},
        {"host", mSettings.value("host", "")},
        {"port", mSettings.value("port", 25565)},
        {"gameData", mSettings.value("gameData", "")},
        {"theme", mSettings.value("theme", "graphite")},
        {"identity", identity()},
        {"setup",
         {{"clientInstalled", fs::is_regular_file(mRoot / "openmw.exe")},
          {"gameDataReady", gameReady},
          {"profilePrepared", prepared}}},
        {"servers", mServers},
        {"favorites", mSettings.value("favorites", Json::array())},
        {"mods", mods},
        {"status", mStatus},
        {"disk", diskSpace(mManaged)},
        {"storage", mStorage},
        {"cleanup", mCleanup},
        {"migration", mMigration},
        {"migrationStatus", legacyMigrationStatus(mRoot)},
        {"legacyMigrationAvailable", fs::exists(mRoot/"Data Files/fetcher-simulator/ModdingResources/TamrielData") ||
                                     fs::exists(mRoot/"Data Files/fetcher-simulator/Landmasses/TamrielRebuilt")},
        {"profileReview", mReview},
        {"nexusKeySaved", fs::is_regular_file(mManaged / "nexus-key.bin")},
        {"nexusProvider", mSettings.value("nexusProvider", "umo")},
        {"umo", umoStatus(mRoot)},
        {"updateBackups", listInstallBackups(mRoot)},
        {"canRollbackProfile", fs::is_regular_file(mManaged / "profiles" / id(current.at("id")) /
                                                   "previous-profile.json")}};
}
Json Backend::parseServers(const Json &value) {
    need(value.is_array() && value.size() <= 2048, "Invalid master server response.");
    Json out = Json::array();
    for (const auto &s : value)
        try {
            auto host = s.at("host").get<std::string>();
            int port = s.at("port").get<int>();
            endpoint(host, port);
            auto name = s.at("name").get<std::string>();
            need(name.size() <= 256, "Server name too long.");
            int now = s.at("current_players").get<int>(), max = s.at("max_players").get<int>();
            need(now >= 0 && max > 0 && max <= 4096 && now <= max, "Invalid player count.");
            auto profileUrl = s.value("mod_profile_url", s.value("profile_url", ""));
            if (profileUrl.size() > 4096 || profileUrl.rfind("https://", 0) != 0)
                profileUrl.clear();
            auto digest = s.value("mod_profile_sha256", "");
            auto serverId = s.value("id", "");
            // URLs supplied by a registry entry are never used for automatic discovery.
            // Construct the path ourselves, with a strictly validated registry ID.
            bool advertised = s.contains("mod_profile_sha256");
            if (advertised)
                profileUrl.clear();
            out.push_back({{"profileUrl", profileUrl},
                           {"profileAdvertised", advertised},
                           {"profileDigest", digest},
                           {"registryId", serverId},
                           {"host", host},
                           {"port", port},
                           {"name", name},
                           {"currentPlayers", now},
                           {"maxPlayers", max},
                           {"protocol", s.value("protocol_version", 0)},
                           {"gameMode", s.value("game_mode", "")},
                           {"country", s.value("country", "")}});
        } catch (const std::exception &) { /* Ignore malformed entries, never turn
                                              them into executable arguments. */
        }
    return out;
}
Json Backend::discoveredProfile(const std::string &host, int port) {
    for (const auto &server : mServers) {
        if (server.at("host") != host || server.at("port") != port ||
            !server.value("profileAdvertised", false))
            continue;
        auto registryId = server.at("registryId").get<std::string>();
        need(std::regex_match(registryId, std::regex(
                 "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")),
             "Invalid master server profile identity.");
        auto source = "https://master.fetchers.org/v1/servers/" + registryId + "/profile";
        auto p = normalizeDiscoveredManifest(mDiscoveryFetch(source, 2 * 1024 * 1024, false),
                                             server.at("profileDigest"), host, port);
        validateProfile(p);
        p["_ownerId"] = p.at("id");
        p["id"] = "server-" + textSha256(endpoint(host, port)).substr(0, 40);
        // Migrate an existing locally trusted profile for this endpoint in place,
        // preserving its settings, local MIDI directory and optional mod choices.
        for (const auto &old : mCatalog.at("profiles"))
            if (old.value("host", "") == host && old.value("port", 0) == port &&
                old.value("mode", "managed") == "managed") {
                p["id"] = old.at("id");
                break;
            }
        p["_sourceUrl"] = source;
        p["_discovered"] = true;
        p["_manifestDigest"] = server.at("profileDigest");
        p["_reviewDigest"] = server.at("profileDigest");
        for (const auto &d : p["packages"])
            if (d.contains("url"))
                validateDownloadUrl(d.at("url"));
        return p;
    }
    return Json();
}
void Backend::acceptProfile(const Json &p) {
    validateProfile(p);
    auto key = id(p.at("id"));
    bool exists = false;
    for (auto &old : mCatalog["profiles"])
        if (old.at("id") == key) {
            bool sameAdvertisedContent = old.value("_discovered", false) &&
                                         p.value("_discovered", false) &&
                                         old.value("_manifestDigest", "") == p.value("_manifestDigest", "");
            if (old != p && !sameAdvertisedContent)
                writeAtomic(mManaged / "profiles" / key / "previous-profile.json", old.dump(2));
            if (old != p) { mStorage = Json(); mCleanup = Json(); }
            old = p;
            exists = true;
            break;
        }
    need(exists || mCatalog["profiles"].size() < 256, "Too many server profiles.");
    if (!exists) {
        mCatalog["profiles"].push_back(p);
        mStorage = Json(); mCleanup = Json();
    }
    writeAtomic(mRoot / "fetcher-launcher-profiles.json", mCatalog.dump(2));
    mSettings["profile"] = key;
    mSettings["host"] = p.at("host");
    mSettings["port"] = p.at("port");
    mStatus = Json();
    save();
}
void Backend::syncDiscoveredProfile() {
    auto host = mSettings.value("host", "");
    auto port = mSettings.value("port", 25565);
    bool required = profile().value("_discovered", false);
    for (const auto &server : mServers)
        required = required || (server.at("host") == host && server.at("port") == port &&
                                server.value("profileAdvertised", false));
    if (!required)
        return;
    // Revalidate on each preparation/Play, including after launcher restart.
    mServers = parseServers(parseJson(mDiscoveryFetch("https://master.fetchers.org/v1/servers", 8 * 1024 * 1024, false)));
    auto p = discoveredProfile(host, port);
    need(!p.is_null(), "This server no longer advertises its mod profile. Refresh or contact its owner.");
    auto status = mStatus;
    acceptProfile(p);
    // Refreshing metadata must not dismiss a known native client/tools update.
    mStatus = std::move(status);
}
Json Backend::resolve(const Json &d) {
    if (d.contains("nexus"))
        return {{"sha256", lower(d.at("sha256"))}, {"nexus", d.at("nexus")}};
    if (d.contains("url")) {
        auto hash = lower(d.at("sha256").get<std::string>());
        need(std::regex_match(hash, std::regex("[0-9a-f]{64}")),
             "Direct package requires SHA-256.");
        return {{"url", d.at("url")}, {"sha256", hash}};
    }
    auto repo = d.at("repository").get<std::string>(), tag = d.at("releaseTag").get<std::string>(),
         asset = d.at("assetName").get<std::string>();
    need(std::regex_match(repo, std::regex("[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")) &&
             std::regex_match(tag, std::regex("[A-Za-z0-9_.-]+")),
         "Invalid GitHub release reference.");
    const auto release =
        parseJson(mDiscoveryFetch("https://api.github.com/repos/" + repo + "/releases/tags/" + tag,
                                  8 * 1024 * 1024, true));
    for (const auto &a : release.at("assets"))
        if (a.at("name") == asset) {
            auto digest = a.value("digest", "");
            need(digest.rfind("sha256:", 0) == 0, "Release asset has no GitHub SHA-256 digest.");
            need(std::regex_match(digest.substr(7), std::regex("[0-9a-f]{64}")),
                 "Invalid release digest.");
            if (d.contains("sha256"))
                need(lower(d.at("sha256")) == digest.substr(7),
                     "GitHub archive no longer matches the server's pinned SHA-256.");
            return {{"url", a.at("browser_download_url")},
                    {"sha256", digest.substr(7)},
                    {"source", repo + "@" + tag}};
        }
    throw std::runtime_error("Release asset is missing: " + asset);
}
fs::path Backend::package(const Json &definition, bool repair) {
    auto ref = resolve(definition);
    auto hash = ref.at("sha256").get<std::string>();
    auto dest = longPath(mManaged / "packages" / hash);
    noLinks(dest);
    auto receipt = readPackageReceipt(dest);
    if (!repair && receipt.value("sha256", "") == hash &&
        verifyPackageCache(dest, receipt)) {
        auto declared = definition.value("archiveFormat", "auto");
        need(declared == "auto" || declared == receipt.value("archiveFormat", "zip"),
             "Cached archive does not match its declared format.");
        return dest;
    }
    auto archive = mManaged / "downloads" / (hash + ".zip");
    if (!fs::is_regular_file(archive) || sha256(archive) != hash) {
        if (ref.contains("nexus")) {
            if (mSettings.value("nexusProvider", "umo") == "umo") {
                downloadUmo(mRoot, ref.at("nexus"), hash, archive, mLog);
            } else {
                need(fs::is_regular_file(mManaged / "nexus-key.bin"),
                     "This Nexus file needs your Nexus API key, or download its exact "
                     "file on Nexus and import the archive below.");
                auto n = ref.at("nexus");
                auto path = "/v1/games/" + n.at("game").get<std::string>() + "/mods/" +
                            std::to_string(n.at("modId").get<std::uint64_t>()) + "/files/" +
                            std::to_string(n.at("fileId").get<std::uint64_t>()) +
                            "/download_link.json";
                Json links;
                try {
                    links = parseJson(
                        nexusGet(path, unprotectSecret(read(mManaged / "nexus-key.bin"))));
                } catch (const std::exception &e) {
                    throw std::runtime_error(std::string(e.what()) +
                                             ". You can use the exact-file manual "
                                             "download/import route instead.");
                }
                need(links.is_array() && !links.empty(), "Nexus returned no eligible "
                                                         "download link; use manual "
                                                         "archive import.");
                ref["url"] = links.at(0).at("URI");
            }
        }
        if (!fs::is_regular_file(archive) || sha256(archive) != hash)
            download(ref.at("url"), archive, hash, mLog);
    }
    auto stage = longPath(mManaged / "staging" / ("package-" + stamp()));
    try {
        auto kind = detectArchiveFormat(archive);
        auto names = extractArchive(archive, stage, definition.value("archiveFormat", "auto"));
        Json records = Json::array();
        for (const auto &name : names)
            records.push_back(cacheRecord(stage / safeRelative(name), name));
        writeAtomic(stage / ".fetcher-package.json",
                    Json{{"sha256", hash}, {"archiveFormat", kind}, {"files", records}}.dump(2));
        if (fs::exists(dest))
            removeManaged(mManaged / "packages", dest);
        fs::create_directories(dest.parent_path());
        fs::rename(stage, dest);
    } catch (...) {
        if (fs::exists(stage))
            removeManaged(mManaged / "staging", stage);
        throw;
    }
    return dest;
}
fs::path Backend::layout(const fs::path &raw, const Json &definition, const fs::path &gameData,
                         bool fullHash) {
    auto actions = normalizeModActions(definition.value("actions", Json::array()));
    if (actions.empty() && !definition.contains("fomod"))
        return raw;
    auto source = readPackageReceipt(raw);
    auto key = modLayoutKey(definition, source.at("sha256"));
    auto dest = longPath(mManaged / "layouts" / key);
    noLinks(dest);
    auto previous = readPackageReceipt(dest);
    if (previous.value("layoutKey", "") == key &&
        verifyPackageCache(dest, previous, true, fullHash))
        return dest;
    if (!definition.contains("fomod")) {
        std::uint64_t copyBytes = 0;
        for (const auto &record : source.at("files")) copyBytes += record.at("size").get<std::uint64_t>();
        requireFreeSpace(mManaged, copyBytes);
    }
    auto stage = longPath(mManaged / "staging" / ("layout-" + stamp()));
    noLinks(stage);
    fs::create_directories(stage);
    try {
        if (definition.contains("fomod"))
            installFomod(raw, stage, definition.at("fomod"));
        else {
            // Preserve empty directories, but copy only files from the verified source receipt.
            for (const auto &entry : fs::recursive_directory_iterator(raw)) {
                noLinks(entry.path());
                if (entry.is_directory())
                    fs::create_directories(stage / entry.path().lexically_relative(raw));
            }
            for (const auto &record : source.at("files")) {
                auto rel = safeRelative(record.at("path"));
                need(rel != L".fetcher-package.json", "Archive uses reserved launcher metadata.");
                fs::create_directories((stage / rel).parent_path());
                fs::copy_file(raw / rel, stage / rel);
            }
        }
        applyModActions(stage, actions, gameData);
        Json records = Json::array();
        for (const auto &entry : fs::recursive_directory_iterator(stage)) {
            noLinks(entry.path());
            if (entry.is_regular_file())
                records.push_back(cacheRecord(
                    entry.path(), utf8(entry.path().lexically_relative(stage).generic_wstring())));
        }
        need(!records.empty(), "Layout removed every file from the package.");
        writeAtomic(
            stage / ".fetcher-package.json",
            Json{{"layoutKey", key}, {"sourceSha256", source.at("sha256")}, {"files", records}}
                .dump(2));
        validateModPayload(stage);
        if (fs::exists(dest))
            removeManaged(mManaged / "layouts", dest);
        fs::create_directories(dest.parent_path());
        fs::rename(stage, dest);
    } catch (...) {
        if (fs::exists(stage))
            removeManaged(mManaged / "staging", stage);
        throw;
    }
    return dest;
}
void Backend::installClient(const fs::path &root, const fs::path &stage, const Json &policy) {
    installTransaction(root, stage, policy, false);
}
void Backend::installTools(const fs::path &root, const fs::path &stage) {
    installTransaction(root, stage, Json::object(), true);
}
fs::path Backend::gameData() const {
    std::vector<fs::path> candidates;
    if (mSettings.contains("gameData") && !mSettings.at("gameData").get<std::string>().empty())
        candidates.emplace_back(wide(mSettings.at("gameData")));
    auto configured = configuredData(mRoot);
    candidates.insert(candidates.end(), configured.begin(), configured.end());
    for (auto p : candidates)
        if (fs::is_regular_file(p / L"Morrowind.esm") && fs::is_regular_file(p / L"Tribunal.esm") &&
            fs::is_regular_file(p / L"Bloodmoon.esm"))
            return fs::absolute(p);
    throw std::runtime_error("Set your owned Morrowind Data Files folder under Mods. Morrowind, "
                             "Tribunal and Bloodmoon are required.");
}
std::vector<std::string> Backend::loadOrder(const std::vector<std::string> &content,
                                            const std::vector<fs::path> &roots) {
    need(content.size() <= 1024, "Too many plugins.");
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < content.size(); ++i) {
        need(safeRelative(content[i]).filename() == safeRelative(content[i]),
             "Invalid plugin filename.");
        need(index.emplace(lower(content[i]), i).second, "Duplicate plugin.");
    }
    std::vector<std::vector<size_t>> deps(content.size());
    for (size_t i = 0; i < content.size(); ++i) {
        fs::path found;
        for (const auto &r : roots) {
            if (!fs::is_directory(r))
                continue;
            for (const auto &f : fs::directory_iterator(r))
                if (lower(utf8(f.path().filename().wstring())) == lower(content[i])) {
                    found = f.path();
                    break;
                }
        }
        need(!found.empty(), "Missing enabled plugin: " + content[i]);
        for (const auto &master : masters(found)) {
            auto it = index.find(lower(master));
            need(it != index.end(), "Missing master " + master + " required by " + content[i]);
            deps[i].push_back(it->second);
        }
    }
    std::vector<int> color(content.size(), 0);
    std::vector<std::string> order;
    std::function<void(size_t)> visit = [&](size_t i) {
        need(color[i] != 1, "Cyclic plugin master dependency.");
        if (color[i] == 2)
            return;
        color[i] = 1;
        for (auto j : deps[i])
            visit(j);
        color[i] = 2;
        order.push_back(content[i]);
    };
    for (size_t i = 0; i < content.size(); ++i)
        visit(i);
    return order;
}
fs::path Backend::prepare(bool fullHash) {
    auto p = profile();
    if (p.value("mode", "managed") == "installed")
        return {};
    auto data = gameData();
    std::vector<fs::path> roots = {mRoot / "resources/vfs-mw", data};
    for (const auto &d : p.at("packages")) {
        auto dir = package(d, false);
        dir = layout(dir, d, data, fullHash);
        // Helpers may exist in an upstream archive, but only an explicitly curated
        // layout/FOMOD output without native programs can enter a game profile.
        validateModPayload(dir);
        for (const auto &root : modDataRoots(dir, d))
            roots.push_back(root);
    }
    auto content = p.at("content").get<std::vector<std::string>>();
    auto extra = mSettings.value("localMods", Json::object())
                     .value(p.at("id").get<std::string>(), Json::array());
    for (const auto &m : extra)
        if (m.value("enabled", true)) {
            roots.emplace_back(wide(m.at("directory")));
            content.push_back(m.at("name"));
        }
    for (const auto &name : p.value("requiredFiles", Json::array())) {
        auto rel = safeRelative(name);
        bool exists = false;
        for (const auto &r : roots)
            exists = exists || fs::is_regular_file(r / rel);
        need(exists, "Required profile asset is missing: " + name.get<std::string>() +
                         ". Check the server manifest and pinned archive layout.");
    }
    auto order = loadOrder(content, roots);
    auto required = p.at("content").get<std::vector<std::string>>();
    std::vector<std::string> filtered;
    for (const auto &s : order)
        if (std::find(required.begin(), required.end(), s) != required.end())
            filtered.push_back(s);
    need(filtered == required,
         "Server profile load order conflicts with plugin master dependencies.");
    auto config = mManaged / "profiles" / id(p.at("id"));
    fs::create_directories(config);
    fs::create_directories(config / "userdata");
    fs::create_directories(config / "local-data");
    std::ostringstream out;
    out << "# Generated by Fetcher Launcher. This profile does not replace the "
           "main "
           "config.\nreplace=config\nreplace=data\nreplace=content\nreplace="
           "fallback-archive\nreplace="
           "groundcover\nresources="
        << cfgString(mRoot / "resources") << "\n";
    out << "data-local=" << cfgString(config / "local-data") << "\n";
    for (const auto &r : roots)
        out << "data=" << cfgString(r) << "\n";
    for (const auto &archive : p.value(
             "fallbackArchives", Json::array({"Morrowind.bsa", "Tribunal.bsa", "Bloodmoon.bsa"})))
        out << "fallback-archive=" << archive.get<std::string>() << "\n";
    auto groundcover = p.value("groundcover", Json::array()).get<std::vector<std::string>>();
    if (!groundcover.empty()) {
        auto all = content;
        all.insert(all.end(), groundcover.begin(), groundcover.end());
        loadOrder(all, roots);
    }
    for (const auto &name : groundcover)
        out << "groundcover=" << name << "\n";
    for (const auto &name : order)
        out << "content=" << name << "\n";
    out << "user-data=" << cfgString(config / "userdata") << "\nencoding=win1252\n";
    if (fs::exists(mRoot / "fetcher-canonical-fallbacks.cfg")) {
        for (const auto &s : lines(mRoot / "fetcher-canonical-fallbacks.cfg")) {
            if (s.empty())
                continue;
            need(s.rfind("fallback=", 0) == 0,
                 "Canonical fallbacks contain a non-fallback setting.");
            out << s << "\n";
        }
    } else if (fs::exists(mRoot / "openmw.cfg"))
        for (const auto &s : lines(mRoot / "openmw.cfg"))
            if (s.rfind("fallback=", 0) == 0)
                out << s << "\n";
    writeAtomic(config / "openmw.cfg", out.str());
    writeAtomic(config / "prepared.json",
                Json{{"profile", p.at("id")},
                     {"manifestDigest", p.value("_reviewDigest", "local")},
                     {"content", order}}
                    .dump(2));
    return config;
}
Json Backend::check(bool repair, bool install, bool toolsOnly) {
    InstallLock lock(mRoot);
    if (install)
        assertGameStopped(mRoot);
    Json reasons = Json::array();
    const Json tools = {{"id", "tools"},
                        {"repository", "Skooma-Breath/Fetcher-Updater"},
                        {"releaseTag", "fetcher-tester-tools"},
                        {"assetName", "fetcher-tester-tools.zip"}};
    const auto toolsRef = resolve(tools);
    auto toolsState = optional(mManaged / "tools.json");
    bool toolsCurrent = toolsState.value("sha256", "") == toolsRef.at("sha256");
    bool toolsInstalled = false;
    if (toolsCurrent && repair) {
        auto inventory = optional(mRoot / "fetcher-tester-tools.json");
        toolsCurrent = verifyFiles(mRoot, inventory.value("files", Json::array()), true);
    }
    if (!toolsCurrent) {
        reasons.push_back("Launcher/tools");
        if (install) {
            installTools(mRoot, package(tools, false));
            writeAtomic(mManaged / "tools.json", toolsRef.dump(2));
            toolsCurrent = true;
            toolsInstalled = true;
            if (mLog)
                mLog("Launcher/tools installed. The GUI will restart if its executable or interface changed.\n");
        }
    }
    if (toolsOnly) {
        mStatus = {{"status", "profileUnavailable"}, {"upToDate", false},
                   {"launcherUpdated", toolsInstalled},
                   {"message", toolsInstalled ? "Launcher/tools updated. Reopen before retrying the unavailable server profile."
                                               : "Launcher/tools are current; the selected server profile is unavailable."}};
        return mStatus;
    }
    auto channel = optional(mRoot / "fetcher-update-channel.json",
                            {{"clientRepository", "Fetcher-Simulator/Fetcher-Simulator"},
                             {"clientReleaseTag", "Fetcher-Simulator"},
                             {"clientAssetName", "fetcher-simulator.zip"}});
    Json client = {{"id", "client"},
                   {"repository", channel.at("clientRepository")},
                   {"releaseTag", channel.at("clientReleaseTag")},
                   {"assetName", channel.at("clientAssetName")}};
    auto ref = resolve(client);
    auto state = optional(mManaged / "client.json");
    auto legacy = optional(mRoot / "fetcher-update-state.json");
    auto previous = legacy.value("client", Json::object());
    bool digestMatches =
        state.value("sha256", "") == ref.at("sha256") ||
        previous.value("assetDigest", "") == "sha256:" + ref.at("sha256").get<std::string>();
    bool clientCurrent = digestMatches && fs::is_regular_file(mRoot / "openmw.exe");
    if (clientCurrent) {
        auto inv = optional(mRoot / "fetcher-client-files.json");
        clientCurrent = verifyFiles(mRoot, inv.value("files", Json::array()), repair);
    }
    if (!clientCurrent) {
        reasons.push_back("Native client");
        if (install) {
            auto stage = package(client, false);
            installClient(mRoot, stage, optional(mRoot / "fetcher-client-protection-policy.json"));
            writeAtomic(mManaged / "client.json", ref.dump(2));
            clientCurrent = true;
        }
    }
    auto p = profile();
    bool modsCurrent = true;
    if (p.value("mode", "managed") != "installed")
        for (const auto &d : p.at("packages")) {
            auto r = resolve(d);
            auto dir = longPath(mManaged / "packages" / r.at("sha256").get<std::string>());
            auto receipt = readPackageReceipt(dir);
            bool current = receipt.value("sha256", "") == r.at("sha256") &&
                           verifyPackageCache(dir, receipt, repair, repair);
            if (current && (d.contains("fomod") || !d.value("actions", Json::array()).empty())) {
                auto key = modLayoutKey(d, r.at("sha256"));
                auto dir = longPath(mManaged / "layouts" / key);
                auto receipt = readPackageReceipt(dir);
                current = receipt.value("layoutKey", "") == key &&
                          verifyPackageCache(dir, receipt, repair, repair);
            }
            if (!current) {
                reasons.push_back(d.at("id"));
                if (install)
                    package(d, repair);
                else
                    modsCurrent = false;
            }
        }
    if (install && p.value("mode", "managed") != "installed")
        prepare(repair);
    bool current = clientCurrent && modsCurrent && toolsCurrent;
    mStatus = {{"schemaVersion", 1},
               {"status", current ? "current" : "updateAvailable"},
               {"upToDate", current},
               {"message", current ? "Selected client and mod profile are ready."
                                   : "Install/repair the selected client and mod profile."},
               {"reasons", current ? Json::array() : reasons},
               {"client", ref},
               {"profile", p.at("id")}};
    return mStatus;
}
void Backend::measurePings() {
    std::vector<std::string> hosts;
    for (const auto& server : mServers) hosts.push_back(server.at("host"));
    auto pings = FetcherNet::measureServerPings(hosts);
    for (std::size_t i = 0; i < pings.size(); ++i) {
        mServers[i]["pingMs"] = pings[i].milliseconds < 0 ? Json() : Json(pings[i].milliseconds);
        mServers[i]["pingMethod"] = pings[i].milliseconds<0?"unavailable":pings[i].tcp?"TCP":"ICMP";
    }
}
Json Backend::legacyReplacements() {
    Json result=Json::object();
    const auto selected=profile();
    for(const auto& definition:selected.at("packages")) {
        auto hash=definition.value("sha256","");
        bool td=hash=="009530c0383759b842298e827bf1ffd88e29b68668bbefe794bc376713e821cf";
        bool tr=hash=="31d1f45469291cd71408fe7b795ded0e593fab226796aa35b1b86b588825c0bc";
        if(!td && !tr)continue;
        auto path=mManaged/"packages"/hash;
        if(definition.contains("fomod") || !definition.value("actions",Json::array()).empty())path=mManaged/"layouts"/modLayoutKey(definition,hash);
        auto roots=modDataRoots(path,definition);
        if(td && roots.size()==1)result["td"]=utf8(ordinaryPath(roots[0]).wstring());
        if(tr && roots.size()==2){result["core"]=utf8(ordinaryPath(roots[0]).wstring());result["factions"]=utf8(ordinaryPath(roots[1]).wstring());}
    }
    need(result.size()==3,"Select and prepare the current Fetcher experimental TR/TD profile first.");
    return result;
}
Json Backend::command(const Json &q) {
    auto op = q.at("op").get<std::string>();
    InstallLock lock(mRoot);
    if (op == "init")
        return state();
    if(op=="previewLegacy" || op=="stageLegacy" || op=="finishLegacy" || op=="restoreLegacy") {
        auto inventory=loadLegacyInventory(mRoot/"ui/index.html");
        Json replacements=Json::object();
        if(op!="restoreLegacy") {syncDiscoveredProfile();assertGameStopped(mRoot);prepare();replacements=legacyReplacements();}
        auto action=op=="previewLegacy"?"preview":op=="stageLegacy"?"stage":op=="finishLegacy"?"finish":"restore";
        mMigration=legacyMigration(mRoot,inventory,replacements,action,q.value("token",""),mLog);
        mCleanup=Json();mStorage=Json();
        return state();
    }
    if (op == "storageEstimate") {
        syncDiscoveredProfile();
        mStorage = estimateStorage(mRoot, profile(), [this](const Json &d) { return resolve(d); });
        return state();
    }
    if (op == "scanCache") {
        syncDiscoveredProfile();
        mCleanup = scanUnusedCache(mRoot, [this](const Json &d) { return resolve(d); });
        mStorage = estimateStorage(mRoot, profile(), [this](const Json &d) { return resolve(d); });
        return state();
    }
    if (op == "cleanCache") {
        syncDiscoveredProfile();
        try {
            auto removed = cleanUnusedCache(mRoot, q.at("token"), [this](const Json &d) { return resolve(d); });
            mCleanup = Json{{"message", std::to_string(removed.at("removedEntries").get<std::uint64_t>()) + " unused verified cache entries removed."}, {"removed", removed}, {"canClean", false}};
        } catch (...) { mCleanup = Json(); throw; }
        return state();
    }
    if (op == "theme") {
        const auto theme = q.at("id").get<std::string>();
        need(theme == "graphite" || theme == "obsidian" || theme == "space" || theme == "ember",
             "Choose a built-in launcher theme.");
        mSettings["theme"] = theme;
        save();
        return state();
    }
    if (op == "startup") {
        try {
            mServers = parseServers(parseJson(mDiscoveryFetch(
                "https://master.fetchers.org/v1/servers", 8 * 1024 * 1024, false)));
        } catch (const std::exception &e) {
            if (mLog) mLog(std::string("Server list unavailable: ") + e.what() + "\n");
        }
        try {
            check(false, false);
        } catch (const std::exception &e) {
            if (mLog) mLog(std::string("Startup update check unavailable: ") + e.what() + "\n");
            mStatus = {{"status", "checkFailed"},
                       {"message", "Update check unavailable. Retry on Updates; your saved setup is unchanged."}};
        }
        measurePings();
        return state();
    }
    if (op == "prepare" || op == "launch" || op == "check")
        syncDiscoveredProfile();
    if (op == "rollbackUpdate") {
        restoreInstallBackup(mRoot, q.at("id"));
        mStatus = {{"status", "restored"},
                   {"message", "Update restored. Reopen the launcher before continuing."}};
        return state();
    }
    if (op == "refreshServers") {
        mServers = parseServers(parseJson(mDiscoveryFetch("https://master.fetchers.org/v1/servers", 8 * 1024 * 1024, false)));
        measurePings();
        return state();
    }
    if (op == "selectServer") {
        endpoint(q.at("host"), q.at("port"));
        if (mServers.empty())
            mServers = parseServers(parseJson(mDiscoveryFetch(
                "https://master.fetchers.org/v1/servers", 8 * 1024 * 1024, false)));
        auto discovered = discoveredProfile(q.at("host"), q.at("port"));
        if (!discovered.is_null()) {
            acceptProfile(discovered);
            return state();
        }
        mSettings["host"] = q.at("host");
        mSettings["port"] = q.at("port");
        mSettings["profile"] = "installed";
        for (const auto &p : mCatalog["profiles"])
            if (p.value("host", "") == q.at("host") && p.value("port", 0) == q.at("port"))
                mSettings["profile"] = p.at("id");
        mStatus = Json();
        save();
        return state();
    }
    if (op == "profile") {
        auto key = id(q.at("id"));
        bool found = false;
        for (const auto &p : mCatalog["profiles"])
            found = found || p.at("id") == key;
        need(found, "Unknown mod profile.");
        mSettings["profile"] = key;
        auto selected = profile();
        if (selected.contains("host")) {
            mSettings["host"] = selected.at("host");
            mSettings["port"] = selected.at("port");
        }
        mStatus = Json();
        save();
        return state();
    }
    if (op == "favorite") {
        auto address = endpoint(q.at("host"), q.at("port"));
        auto &favorites = mSettings["favorites"];
        bool had = false;
        for (auto i = favorites.begin(); i != favorites.end(); ++i)
            if (*i == address) {
                favorites.erase(i);
                had = true;
                break;
            }
        if (!had)
            favorites.push_back(address);
        save();
        return state();
    }
    if (op == "gameData") {
        auto path = fs::path(wide(q.at("path").get<std::string>()));
        need(fs::is_regular_file(path / "Morrowind.esm") &&
                 fs::is_regular_file(path / "Tribunal.esm") &&
                 fs::is_regular_file(path / "Bloodmoon.esm"),
             "Choose the Data Files folder containing Morrowind, Tribunal and "
             "Bloodmoon.");
        mSettings["gameData"] = utf8(fs::absolute(path).wstring());
        save();
        return state();
    }
    if (op == "identity") {
        saveIdentity(q);
        return state();
    }
    if (op == "addProfile" || op == "reviewProfile") {
        auto source = q.value("url", "");
        std::string text;
        if (q.contains("path")) {
            auto path = fs::absolute(fs::path(wide(q.at("path")))).lexically_normal();
            text = read(path);
            source = "local:" + utf8(path.generic_wstring());
        } else {
            validateDownloadUrl(source);
            text = httpGet(source);
        }
        auto raw = parseJson(text);
        if (raw.contains("umo")) {
            need(!raw.contains("umoMods"),
                 "Choose an embedded UMO list or a pinned UMO URL, not both.");
            auto ref = raw.at("umo");
            need(ref.size() == 2 && ref.contains("url") && ref.contains("sha256"),
                 "UMO list reference needs only url and SHA-256.");
            auto list = httpGet(ref.at("url"));
            need(textSha256(list) == lower(ref.at("sha256")), "UMO list SHA-256 mismatch.");
            auto mods = parseJson(list);
            need(mods.is_array(), "Export the UMO list as an array of mods.");
            raw["umoMods"] = mods;
            raw.erase("umo");
        }
        auto p = normalizeServerManifest(raw);
        validateProfile(p);
        need(p.contains("host") && p.contains("port"),
             "Published server manifests need their host and port.");
        need(p.contains("version") && !p.at("version").get<std::string>().empty(),
             "Published server manifests need a version.");
        if (q.contains("expectedHost"))
            need(p.at("host") == q.at("expectedHost") && p.at("port") == q.at("expectedPort"),
                 "The advertised manifest endpoint does not match the selected "
                 "server.");
        auto ownerId = id(p.at("id"));
        p["id"] = "remote-" + textSha256(source + "\n" + ownerId).substr(0, 40);
        p["_ownerId"] = ownerId;
        p["_sourceUrl"] = source;
        for (auto &d : p["packages"]) {
            if (d.contains("repository")) {
                auto pinned = resolve(d);
                d["url"] = pinned.at("url");
                d["sha256"] = pinned.at("sha256");
                d["sourceRelease"] = d.at("repository").get<std::string>() + "@" +
                                     d.at("releaseTag").get<std::string>();
                d.erase("repository");
                d.erase("releaseTag");
                d.erase("assetName");
            }
            if (d.contains("url"))
                validateDownloadUrl(d.at("url"));
        }
        p["_reviewDigest"] = textSha256(p.dump());
        mReview = {{"token", p.at("_reviewDigest")}, {"profile", p}};
        writeAtomic(mManaged / "pending-profile.json", mReview.dump(2));
        return state();
    }
    if (op == "dismissProfile") {
        mReview = Json::object();
        writeAtomic(mManaged / "pending-profile.json", mReview.dump());
        return state();
    }
    if (op == "confirmProfile") {
        need(mReview.contains("profile") && q.at("token") == mReview.at("token"),
             "Review the current manifest before accepting it.");
        auto p = mReview.at("profile");
        acceptProfile(p);
        mReview = Json::object();
        writeAtomic(mManaged / "pending-profile.json", mReview.dump());
        return state();
    }
    if (op == "rollbackProfile") {
        auto key = id(profile().at("id"));
        auto prior = readJson(mManaged / "profiles" / key / "previous-profile.json");
        validateProfile(prior);
        need(prior.at("id") == key, "Rollback profile identity mismatch.");
        for (auto &p : mCatalog["profiles"])
            if (p.at("id") == key)
                p = prior;
        writeAtomic(mRoot / "fetcher-launcher-profiles.json", mCatalog.dump(2));
        mStatus = Json();
        mSettings["host"] = prior.at("host");
        mSettings["port"] = prior.at("port");
        save();
        return state();
    }
    if (op == "umoInstall") {
        installUmo(mRoot, mLog);
        return state();
    }
    if (op == "umoConnect") {
        connectUmo(mRoot, mLog);
        mSettings["nexusProvider"] = "umo";
        save();
        return state();
    }
    if (op == "umoForget") {
        forgetUmo(mRoot);
        mSettings["nexusProvider"] = "umo";
        save();
        return state();
    }
    if (op == "umoProtocol") {
        umoProtocol(mRoot, q.at("enable").get<bool>());
        return state();
    }
    if (op == "nexusProvider") {
        auto provider = q.at("provider").get<std::string>();
        need(provider == "umo" || provider == "personal-testing", "Unknown Nexus provider.");
        mSettings["nexusProvider"] = provider;
        save();
        return state();
    }
    if (op == "nexusKey") {
        auto key = q.at("key").get<std::string>();
        if (key.empty())
            fs::remove(mManaged / "nexus-key.bin");
        else {
            need(key.size() >= 10 && key.size() <= 512 &&
                     key.find_first_of("\r\n") == std::string::npos,
                 "Invalid Nexus API key.");
            writeAtomic(mManaged / "nexus-key.bin", protectSecret(key));
        }
        mSettings["nexusProvider"] = key.empty() ? "umo" : "personal-testing";
        save();
        return state();
    }
    if (op == "importArchive") {
        auto packageId = id(q.at("id"));
        auto selected = profile();
        for (const auto &d : selected.at("packages"))
            if (d.at("id") == packageId) {
                auto ref = resolve(d);
                auto hash = ref.at("sha256").get<std::string>();
                auto input = fs::absolute(fs::path(wide(q.at("path"))));
                noLinks(input);
                need(fs::is_regular_file(input) &&
                         fs::file_size(input) <= 4ull * 1024 * 1024 * 1024 && sha256(input) == hash,
                     "Imported archive does not match the server's pinned SHA-256.");
                auto target = mManaged / "downloads" / (hash + ".zip");
                noLinks(target);
                fs::create_directories(target.parent_path());
                auto partial = target;
                partial += L".import";
                fs::copy_file(input, partial, fs::copy_options::overwrite_existing);
                need(sha256(partial) == hash, "Imported archive changed during copy.");
                need(MoveFileExW(partial.c_str(), target.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                     "Could not cache imported archive.");
                return state();
            }
        throw std::runtime_error("Choose a package in the selected server profile.");
    }
    if (op == "nexusPage") {
        auto selected = profile();
        for (const auto &d : selected.at("packages"))
            if (d.at("id") == q.at("id") && d.contains("nexus")) {
                auto n = d.at("nexus");
                auto url = L"https://www.nexusmods.com/" + wide(n.at("game")) + L"/mods/" +
                           std::to_wstring(n.at("modId").get<std::uint64_t>()) +
                           L"?tab=files&file_id=" +
                           std::to_wstring(n.at("fileId").get<std::uint64_t>());
                need(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr,
                                                             nullptr, SW_SHOWNORMAL)) > 32,
                     "Could not open Nexus.");
                return state();
            }
        throw std::runtime_error("No matching Nexus package.");
    }
    if (op == "addMod") {
        auto p = profile();
        need(p.value("mode", "managed") != "installed",
             "Use a managed profile to add optional local mods.");
        auto name = q.at("name").get<std::string>();
        need(safeRelative(name).filename() == safeRelative(name), "Enter a plugin filename.");
        auto dir = fs::absolute(fs::path(wide(q.at("directory"))));
        need(fs::is_regular_file(dir / safeRelative(name)), "Local plugin is missing.");
        auto key = p.at("id").get<std::string>();
        auto &extras = mSettings["localMods"][key];
        if (extras.is_null())
            extras = Json::array();
        need(extras.size() < 256, "Too many local mods.");
        for (const auto &e : extras)
            need(lower(e.at("name")) != lower(name), "Plugin already added.");
        for (const auto &e : p.at("content"))
            need(lower(e) != lower(name), "Required server plugin is already enabled.");
        extras.push_back({{"name", name},
                          {"directory", utf8(dir.wstring())},
                          {"enabled", true},
                          {"required", false}});
        save();
        return state();
    }
    if (op == "editMod") {
        auto key = profile().at("id").get<std::string>();
        auto &extras = mSettings["localMods"][key];
        auto index = q.at("index").get<size_t>();
        need(index < extras.size(), "Optional mod index out of range.");
        if (q.contains("enabled"))
            extras[index]["enabled"] = q.at("enabled").get<bool>();
        if (q.contains("move")) {
            int to = int(index) + q.at("move").get<int>();
            need(to >= 0 && to < int(extras.size()), "Mod movement is out of range.");
            std::swap(extras[index], extras[size_t(to)]);
        }
        save();
        return state();
    }
    if (op == "check") {
        check(false, false);
        return state();
    }
    if (op == "update" || op == "repair") {
        assertGameStopped(mRoot);
        try {
            syncDiscoveredProfile();
        } catch (const std::exception &) {
            // An unavailable/newer server document must not prevent upgrading
            // the launcher that reads it. Never prepare stale requirements here.
            auto status = check(op == "repair", true, true);
            if (!status.value("launcherUpdated", false))
                throw std::runtime_error("Server requirements are unavailable. Launcher/tools are already current; retry or select another server.");
            return state();
        }
        check(op == "repair", true);
        return state();
    }
    if (op == "prepare") {
        assertGameStopped(mRoot);
        prepare();
        return state();
    }
    if (op == "launch") {
        assertGameStopped(mRoot);
        if (q.contains("account"))
            saveIdentity(q);
        need(mStatus.is_null() || mStatus.empty() || mStatus.value("upToDate", false) ||
                 mStatus.value("status", "") == "checkFailed",
             "Install known available client/profile updates before launching.");
        auto cfg = prepare();
        std::vector<std::wstring> args;
        if (!cfg.empty())
            args.push_back(L"--config=" + cfg.wstring());
        auto host = mSettings.value("host", "");
        if (!host.empty()) {
            auto selected = identity();
            auto account = selected.value("account", ""),
                 character = selected.value("character", "");
            need(!account.empty(), "Enter your multiplayer account under Servers before playing.");
            args.push_back(L"--connect=" + wide(endpoint(host, mSettings.value("port", 25565))));
            args.push_back(L"--mp-account=" + wide(account));
            if (!character.empty()) {
                args.push_back(L"--mp-character=" + wide(character));
                args.push_back(L"--mp-profile-root=" + (mRoot / "profiles").wstring());
                if (selected.value("autoEnter", true))
                    args.push_back(L"--mp-auto-enter=1");
            }
        }
        launch(mRoot / L"openmw.exe", args, mRoot);
        return state();
    }
    if (op == "wizard") {
        launch(mRoot / L"openmw-wizard.exe", {}, mRoot);
        return state();
    }
    throw std::runtime_error("Unknown launcher action.");
}
} // namespace fetcher
