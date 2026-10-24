#include "server_manifest.hpp"
#include <regex>
#include <set>
namespace fetcher {
namespace {
void need(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::string dataPath(std::string value) {
    while (value.rfind("./", 0) == 0)
        value.erase(0, 2);
    if (value.empty() || value == ".")
        return ".";
    safeRelative(value);
    return value;
}
}
void validateModDefinition(const Json &d) {
    need(d.is_object(), "Invalid mod package.");
    for (auto key : {"downloadBytes", "expandedBytes"})
        if (d.contains(key))
            need(d.at(key).is_number_unsigned() &&
                     d.at(key).get<std::uint64_t>() <= (std::string(key) == "downloadBytes" ? 4ull : 8ull) * 1024 * 1024 * 1024,
                 "Package size hints must be nonnegative bounded byte counts.");
    int sources = int(d.contains("url")) + int(d.contains("repository")) + int(d.contains("nexus"));
    need(sources == 1, "Each mod package requires exactly one source.");
    auto format = d.value("archiveFormat", "auto");
    need(format == "auto" || format == "zip" || format == "7z" || format == "rar",
         "Supported archive formats are ZIP, 7z and RAR.");
    if (!d.contains("repository"))
        need(std::regex_match(d.at("sha256").get<std::string>(), std::regex("[0-9a-fA-F]{64}")),
             "Each direct/Nexus file must pin its SHA-256.");
    if (d.contains("url")) {
        auto url = d.at("url").get<std::string>();
        need(url.size() <= 4096 && url.find_first_of("\r\n") == std::string::npos,
             "Invalid package URL.");
        need(url.rfind("https://", 0) == 0 || url.rfind("http://127.0.0.1:", 0) == 0,
             "Mod archives require HTTPS.");
        need(!std::regex_search(
                 url, std::regex(R"(^https://([^/]*\.)?nexusmods\.com/)", std::regex::icase)),
             "Nexus page URLs are not direct archives; supply a Nexus "
             "game/mod/file reference.");
    }
    if (d.contains("nexus")) {
        const auto &n = d.at("nexus");
        need(n.is_object() && n.size() == 3 && n.contains("game") && n.contains("modId") &&
                 n.contains("fileId"),
             "Nexus reference requires only game, modId and fileId; access keys do "
             "not belong in manifests.");
        need(std::regex_match(n.at("game").get<std::string>(), std::regex("[a-z0-9-]{1,64}")) &&
                 n.at("modId").is_number_unsigned() && n.at("fileId").is_number_unsigned() &&
                 n.at("modId").get<std::uint64_t>() > 0 && n.at("fileId").get<std::uint64_t>() > 0,
             "Nexus files require a game domain and positive pinned mod/file IDs.");
    }
    if (d.contains("dataDirs")) {
        need(d.at("dataDirs").is_array() && !d.at("dataDirs").empty() &&
                 d.at("dataDirs").size() <= 64,
             "A mod needs 1 to 64 data directories.");
        for (const auto &p : d.at("dataDirs"))
            dataPath(p.get<std::string>());
    }
    normalizeModActions(d.value("actions", Json::array()));
    if (d.contains("fomod")) {
        normalizeFomod(d.at("fomod"));
        need(d.contains("dataDirs") && d.at("dataDirs") == Json::array({"."}),
             "FOMOD output is a fresh data directory; specify dataDirs: [\".\"].");
    }
}
Json normalizeServerManifest(const Json &value) {
    need(value.is_object(), "Publish a server manifest object wrapping the UMO "
                            "mod list, endpoint and load order.");
    auto p = value;
    for (auto it = p.begin(); it != p.end();) {
        if (!it.key().empty() && it.key().front() == '_')
            it = p.erase(it);
        else
            ++it;
    }
    need(p.value("schemaVersion", 1) == 1, "Unsupported server manifest schema.");
    need(p.value("mode", "managed") == "managed",
         "Remote manifests require managed configuration.");
    p["mode"] = "managed";
    if (p.contains("umoMods")) {
        const auto mods = p.at("umoMods");
        need(mods.is_array() && mods.size() <= 512, "Invalid UMO mod list.");
        if (!p.contains("packages"))
            p["packages"] = Json::array();
        bool deriveContent = !p.contains("content");
        if (deriveContent)
            p["content"] = Json::array({"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"});
        bool deriveGroundcover = !p.contains("groundcover");
        if (deriveGroundcover)
            p["groundcover"] = Json::array();
        std::size_t index = 0;
        for (const auto &m : mods) {
            auto downloads = m.at("download_info");
            auto name = m.at("name").get<std::string>();
            need(downloads.is_array() && !downloads.empty() && downloads.size() <= 32,
                 "UMO mod has no bounded download list: " + name);
            need(m.value("actions", Json::array()).empty(), "UMO actions are unsupported: " + name);
            std::size_t file = 0;
            for (const auto &d : downloads) {
                need(d.value("pinned", false),
                     "Pin the exact UMO file version before publishing: " + name);
                Json item = {{"id", "umo-" + std::to_string(index) + "-" + std::to_string(file++)},
                             {"name", name},
                             {"author", m.value("author", "")},
                             {"sha256", d.at("sha256")},
                             {"archiveFormat", d.value("archiveFormat", "auto")}};
                if (d.contains("fomod"))
                    item["fomod"] = normalizeFomod(d.at("fomod"));
                if (!d.value("actions", Json::array()).empty()) {
                    auto prefix = d.at("extract_to").get<std::string>();
                    safeRelative(prefix);
                    item["actions"] = normalizeModActions(d.at("actions"), prefix);
                }
                if (d.contains("direct_download") && !d.at("direct_download").is_null() &&
                    d.at("direct_download") != "") {
                    need(d.at("direct_download").is_string(),
                         "UMO direct_download must be an HTTPS archive URL.");
                    item["url"] = d.at("direct_download");
                } else {
                    std::smatch match;
                    auto url = m.at("url").get<std::string>();
                    need(
                        std::regex_match(
                            url, match,
                            std::regex(
                                R"(https://(?:www\.)?nexusmods\.com/([a-z0-9-]+)/mods/([0-9]+)/?)")),
                        "UMO entry requires a Nexus mod page or explicit direct "
                        "download: " +
                            name);
                    need(d.at("nexus_file_id").is_number_unsigned(),
                         "UMO Nexus file ID must be pinned: " + name);
                    item["nexus"] = {{"game", match[1].str()},
                                     {"modId", std::stoull(match[2].str())},
                                     {"fileId", d.at("nexus_file_id")}};
                }
                if (d.contains("dataDirs"))
                    item["dataDirs"] = d.at("dataDirs");
                else {
                    need(downloads.size() == 1,
                         "Multi-archive UMO mods need explicit per-download dataDirs: " + name);
                    auto prefix = d.at("extract_to").get<std::string>();
                    safeRelative(prefix);
                    item["dataDirs"] = Json::array();
                    for (const auto &root : m.at("data_paths")) {
                        auto path = root.get<std::string>();
                        need(path == prefix || path.rfind(prefix + "/", 0) == 0,
                             "UMO data_paths must be within extract_to; specify "
                             "per-download dataDirs: " +
                                 name);
                        item["dataDirs"].push_back(path == prefix ? "."
                                                                  : path.substr(prefix.size() + 1));
                    }
                }
                validateModDefinition(item);
                p["packages"].push_back(item);
            }
            if (deriveContent)
                for (const auto &plugin : m.value("plugins", Json::array()))
                    p["content"].push_back(plugin);
            if (deriveGroundcover)
                for (const auto &plugin : m.value("groundcover", Json::array()))
                    p["groundcover"].push_back(plugin);
            ++index;
        }
        p.erase("umoMods");
    }
    if (!p.contains("packages"))
        p["packages"] = Json::array();
    if (!p.contains("content"))
        p["content"] = Json::array({"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"});
    for (auto &d : p.at("packages")) {
        if (d.contains("actions"))
            d["actions"] = normalizeModActions(d.at("actions"));
        if (d.contains("fomod"))
            d["fomod"] = normalizeFomod(d.at("fomod"));
        validateModDefinition(d);
    }
    return p;
}
Json normalizeDiscoveredManifest(const std::string &text, const std::string &digest,
                                 const std::string &host, int port) {
    need(std::regex_match(digest, std::regex("[0-9a-f]{64}")) && textSha256(text) == digest,
         "Advertised server profile SHA-256 mismatch. Refresh the server list.");
    auto raw = parseJson(text, 2 * 1024 * 1024);
    const std::set<std::string> allowed = {
        "schemaVersion", "id", "name", "version", "mode", "description", "host", "port",
        "packages", "umoMods", "content", "requiredFiles", "groundcover", "fallbackArchives"};
    need(raw.is_object(), "Server profile must be a JSON object.");
    for (auto it = raw.begin(); it != raw.end(); ++it)
        need(allowed.count(it.key()) != 0, "Unsupported advertised profile field: " + it.key());
    auto p = normalizeServerManifest(raw);
    need(p.contains("version") && !p.at("version").get<std::string>().empty(),
         "Advertised profile needs a version.");
    // The registry's source address and listening port are authoritative. A
    // document may not redirect Play to its own host/port (also supports LAN aliases).
    endpoint(host, port);
    p["host"] = host;
    p["port"] = port;
    const std::set<std::string> packageFields = {
        "id", "name", "author", "version", "url", "sha256", "archiveFormat", "dataDirs",
        "actions", "fomod", "nexus", "repository", "releaseTag", "assetName"};
    for (const auto &d : p.at("packages")) {
        for (auto it = d.begin(); it != d.end(); ++it)
            need(packageFields.count(it.key()) != 0, "Unsupported advertised package field: " + it.key());
        need(d.contains("sha256") && std::regex_match(d.at("sha256").get<std::string>(),
                                                     std::regex("[0-9a-fA-F]{64}")),
             "Every advertised archive must have a pinned SHA-256, including GitHub releases.");
        if (d.contains("url"))
            need(d.at("url").get<std::string>().rfind("https://", 0) == 0,
                 "Advertised packages require HTTPS.");
    }
    return p;
}
Json readPackageReceipt(const fs::path &directory) {
    auto path = longPath(directory / ".fetcher-package.json");
    if (!fs::exists(path))
        return Json::object();
    // Receipts for supported 100k-entry archives can exceed the external JSON
    // budget. Only locally generated package inventories receive this larger cap.
    auto receipt = readJson(path, 128ull * 1024 * 1024);
    need(receipt.is_object() && receipt.contains("files") && receipt.at("files").is_array() &&
             receipt.at("files").size() <= 100000,
         "Invalid package receipt file count.");
    return receipt;
}
void validateModPayload(const fs::path &directory) {
    static const std::set<std::string> blocked = {".exe",  ".dll",  ".com", ".bat", ".cmd", ".ps1",
                                                  ".psm1", ".psd1", ".msi", ".msp", ".scr", ".cpl",
                                                  ".lnk",  ".url",  ".hta", ".vbs", ".vbe", ".wsf",
                                                  ".wsh",  ".pif",  ".reg", ".py",  ".jar"};
    auto receipt = readPackageReceipt(directory);
    for (const auto &record : receipt.at("files")) {
        auto path = safeRelative(record.at("path"));
        need(!blocked.count(lower(path.extension().string())),
             "Server mod archives cannot include native programs, shortcuts or "
             "installer scripts: " +
                 utf8(path.filename().wstring()));
    }
}
std::vector<fs::path> modDataRoots(const fs::path &directory, const Json &definition) {
    auto paths = definition.contains("dataDirs")
                     ? definition.at("dataDirs")
                     : readJson(directory / "openmw-client-package.json").at("dataDirs");
    need(paths.is_array() && !paths.empty() && paths.size() <= 64,
         "Mod archive requires explicit dataDirs or a package manifest.");
    std::vector<fs::path> out;
    for (const auto &path : paths) {
        auto name = dataPath(path.get<std::string>());
        auto root = name == "." ? directory : directory / safeRelative(name);
        noLinks(root);
        need(fs::is_directory(root), "A declared mod data directory is missing: " + name);
        out.push_back(root);
    }
    return out;
}
} // namespace fetcher
