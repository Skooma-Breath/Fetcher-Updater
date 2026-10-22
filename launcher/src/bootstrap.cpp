#include "bootstrap.hpp"
#include "install_transaction.hpp"
#include <chrono>
#include <fstream>
#include <regex>
#include <set>
namespace fetcher {
namespace {
void need(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
Json asset(const std::string &url, const std::string &name) {
    auto release = parseJson(httpGet(url));
    need(release.at("assets").is_array(), "Release has no asset list.");
    Json result;
    for (const auto &entry : release.at("assets")) {
        if (entry.at("name") != name)
            continue;
        need(result.is_null(), "Release contains duplicate setup assets.");
        auto digest = entry.at("digest").get<std::string>();
        need(std::regex_match(digest, std::regex("sha256:[0-9a-f]{64}")),
             "Setup requires a GitHub SHA-256 digest for " + name);
        auto downloadUrl = entry.at("browser_download_url").get<std::string>();
        validateDownloadUrl(downloadUrl);
        result = {{"url", downloadUrl}, {"sha256", digest.substr(7)}};
    }
    need(!result.is_null(), "Setup release is missing " + name);
    return result;
}
std::string clientRelease(const Json &channel) {
    auto repository = channel.at("clientRepository").get<std::string>();
    auto tag = channel.at("clientReleaseTag").get<std::string>();
    need(std::regex_match(repository, std::regex("[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")) &&
             std::regex_match(tag, std::regex("[A-Za-z0-9_.-]+")),
         "Invalid client release in the verified setup package.");
    return "https://api.github.com/repos/" + repository + "/releases/tags/" + tag;
}
void executable(const fs::path &path) {
    noLinks(path);
    need(fs::is_regular_file(path) && fs::file_size(path) >= 64,
         "Setup package is missing a Windows executable.");
    std::ifstream input(path, std::ios::binary);
    unsigned char header[64]{};
    input.read(reinterpret_cast<char *>(header), sizeof(header));
    need(bool(input), "Could not read a setup executable header.");
    std::uint64_t offset = std::uint32_t(header[60]) | (std::uint32_t(header[61]) << 8) |
                           (std::uint32_t(header[62]) << 16) | (std::uint32_t(header[63]) << 24);
    need(header[0] == 'M' && header[1] == 'Z' && offset <= fs::file_size(path) - 6,
         "Setup package contains an invalid Windows executable.");
    input.seekg(static_cast<std::streamoff>(offset));
    unsigned char signature[6];
    input.read(reinterpret_cast<char *>(signature), sizeof(signature));
    need(bool(input) && signature[0] == 'P' && signature[1] == 'E' && signature[2] == 0 &&
             signature[3] == 0 && signature[4] == 0x64 && signature[5] == 0x86,
         "Setup requires Windows x64 executables.");
}
void inventoryContains(const fs::path &stage, const std::string &manifest,
                       const std::set<std::string> &required) {
    auto inventory = readJson(stage / manifest);
    std::set<std::string> found;
    for (const auto &record : inventory.at("files"))
        found.insert(lower(record.at("path")));
    for (const auto &name : required)
        need(found.count(name) != 0, "Setup inventory is missing " + name);
}
}
Json bootstrapInstall(const fs::path &directory, const Log &log, const std::string &toolsUrl,
                      const std::string &clientUrl) {
    need(!directory.empty(), "Choose an installation folder.");
    auto root = longPath(directory);
    need(ordinaryPath(root) != ordinaryPath(root).root_path(),
         "Choose a dedicated installation folder, not a drive root.");
    noLinks(root);
    InstallLock lock(root);
    assertGameStopped(root);
    recoverInstallTransactions(root);
    auto managed = root / "_fetcher_launcher";
    fs::create_directories(managed / "staging");
    auto install = [&](const Json &reference, bool tools) {
        auto hash = reference.at("sha256").get<std::string>();
        auto archive = managed / "downloads" / (hash + ".zip");
        download(reference.at("url"), archive, hash, log);
        auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        auto stage = managed / "staging" / ("bootstrap-" + stamp);
        try {
            extractZip(archive, stage);
            if (tools) {
                inventoryContains(stage, "fetcher-tester-tools.json",
                                  {"fetcherlauncher.exe", "ui/index.html",
                                   "fetcherlauncher-third-party-notices.txt",
                                   "ui/assets/fetcher-float.gif",
                                   "ui/assets/fetcher-float-right.gif", "ui/assets/potm2504a.jpg",
                                   "fetcher-update-channel.json"});
                executable(stage / "FetcherLauncher.exe");
                auto channel = readJson(stage / "fetcher-update-channel.json");
                clientRelease(channel);
                need(channel.at("clientAssetName") == "fetcher-simulator.zip",
                     "Unsupported client archive name in the setup channel.");
            } else {
                inventoryContains(stage, "fetcher-client-files.json", {"openmw.exe"});
                executable(stage / "openmw.exe");
                need(fs::is_directory(stage / "resources" / "vfs-mw"),
                     "Client setup package is missing resources/vfs-mw.");
            }
            if (log)
                log(tools ? "Installing verified launcher files.\n"
                          : "Installing verified game client.\n");
            auto policy = fs::is_regular_file(root / "fetcher-client-protection-policy.json")
                              ? readJson(root / "fetcher-client-protection-policy.json")
                              : Json::object();
            installTransaction(root, stage, policy, tools);
            writeAtomic(managed / (tools ? "tools.json" : "client.json"), reference.dump(2));
            removeManaged(managed / "staging", stage);
        } catch (...) {
            if (fs::exists(stage))
                removeManaged(managed / "staging", stage);
            throw;
        }
    };
    if (log)
        log("Checking the Fetcher Launcher release.\n");
    auto tools = asset(toolsUrl, "fetcher-tester-tools.zip");
    install(tools, true);
    auto channel = readJson(root / "fetcher-update-channel.json");
    if (log)
        log("Checking the game client release.\n");
    auto client = asset(clientUrl.empty() ? clientRelease(channel) : clientUrl,
                        channel.at("clientAssetName"));
    install(client, false);
    // The engine needs a local/global anchor before following --config directories.
    if (!fs::exists(root / "openmw.cfg")) {
        auto folder = utf8(ordinaryPath(root).generic_wstring());
        need(folder.find_first_of("\"\r\n") == std::string::npos,
             "Invalid installation configuration path.");
        fs::create_directories(root / "userdata");
        writeAtomic(root / "openmw.cfg",
                    "# Fetcher first-install configuration anchor.\nresources=\"" + folder +
                        "/resources\"\nuser-data=\"" + folder + "/userdata\"\n");
    }
    if (log)
        log("Setup complete. Select your owned Morrowind Data Files and a server profile in the "
            "launcher.\n");
    return {{"installRoot", utf8(ordinaryPath(root).wstring())},
            {"launcher", utf8(ordinaryPath(root / "FetcherLauncher.exe").wstring())},
            {"tools", tools},
            {"client", client},
            {"installed", true}};
}
}
