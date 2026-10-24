#include "native_backend.hpp"
#include "server_manifest.hpp"
#include "launcher_runtime.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <miniz.h>
#include <windows.h>
using namespace fetcher;
namespace {
int assertions = 0;
void check(bool value, const char *message) {
    ++assertions;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, const char *message) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, message);
}
void plugin(const fs::path &path, const std::vector<std::string> &masters = {}) {
    std::string data;
    for (auto m : masters) {
        m.push_back(0);
        auto n = std::uint32_t(m.size());
        data += "MAST";
        data.append(reinterpret_cast<const char *>(&n), 4);
        data += m;
    }
    auto size = std::uint32_t(data.size());
    std::string bytes = "TES3";
    bytes.append(reinterpret_cast<const char *>(&size), 4);
    bytes.append(8, 0);
    bytes += data;
    writeAtomic(path, bytes);
}
void zip(const fs::path &path, const std::string &name) {
    mz_zip_archive z{};
    check(mz_zip_writer_init_file(&z, path.string().c_str(), 0) != 0, "create zip");
    check(mz_zip_writer_add_mem(&z, name.c_str(), "hello", 5, MZ_BEST_COMPRESSION) != 0,
          "add zip entry");
    check(mz_zip_writer_finalize_archive(&z) != 0, "finalize zip");
    mz_zip_writer_end(&z);
}
void sevenZip(const fs::path &path, const std::vector<std::pair<std::string, std::string>> &files,
              const std::string &method = "lzma2") {
    auto archive = archive_write_new();
    check(archive_write_set_format_7zip(archive) == ARCHIVE_OK, "create 7z writer");
    check(archive_write_set_options(archive, ("7zip:compression=" + method).c_str()) == ARCHIVE_OK,
          "LZMA2 writer");
    check(archive_write_open_filename_w(archive, path.c_str()) == ARCHIVE_OK, "open 7z writer");
    for (const auto &file : files) {
        auto entry = archive_entry_new();
        archive_entry_set_pathname_utf8(entry, file.first.c_str());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_size(entry, file.second.size());
        check(archive_write_header(archive, entry) == ARCHIVE_OK, "write 7z header");
        check(archive_write_data(archive, file.second.data(), file.second.size()) ==
                  file.second.size(),
              "write 7z data");
        archive_entry_free(entry);
    }
    check(archive_write_close(archive) == ARCHIVE_OK, "finish 7z writer");
    archive_write_free(archive);
}
void startupTests(const fs::path &root) {
    auto install = root / "startup";
    writeAtomic(install / "openmw.cfg", "content=Personal.esp\n");
    int lists = 0, releases = 0;
    auto fetch = [&](const std::string &url, std::uint64_t, bool) {
        if (url == "https://master.fetchers.org/v1/servers") {
            ++lists;
            return std::string("[]");
        }
        check(url.rfind("https://api.github.com/repos/", 0) == 0, "startup fetches only release metadata");
        ++releases;
        auto name = url.find("fetcher-tester-tools") != std::string::npos ? "fetcher-tester-tools.zip" : "fetcher-simulator.zip";
        return Json{{"assets", Json::array({{{"name", name}, {"digest", "sha256:" + std::string(64, 'a')},
                                              {"browser_download_url", "https://example.org/download.zip"}}})}}.dump();
    };
    Backend backend(install, {}, fetch);
    auto state = backend.command({{"op", "startup"}});
    check(lists == 1 && releases == 2 && state.at("status").at("status") == "updateAvailable",
          "startup refreshes servers and checks launcher/client releases once");
    check(!fs::exists(install / "_fetcher_launcher/downloads") &&
              read(install / "openmw.cfg") == "content=Personal.esp\n",
          "startup update check does not install or alter configuration");
    Backend offline(install, {}, [](const std::string &, std::uint64_t, bool) -> std::string {
        throw std::runtime_error("offline fixture");
    });
    auto failed = offline.command({{"op", "startup"}});
    check(failed.at("status").at("status") == "checkFailed" &&
              failed.at("selectedProfile") == "installed", "offline startup keeps saved setup and allows retry");
    try {
        offline.command({{"op", "launch"}});
        check(false, "fixture has no executable");
    } catch (const std::exception &e) {
        check(std::string(e.what()) == "Executable is missing.",
              "failed startup check does not itself block launch of an existing setup");
    }
    try {
        backend.command({{"op", "launch"}});
        check(false, "known update must block stale client");
    } catch (const std::exception &e) {
        check(std::string(e.what()).find("Install known available") != std::string::npos,
              "known available client updates still block stale launch");
    }
}
void runtimeTests(const fs::path &root) {
    auto install = root / "runtime";
    writeAtomic(install / "FetcherLauncher.exe", "executable one");
    writeAtomic(install / "ui/index.html", "interface one");
    writeAtomic(install / "ui/assets/icon.gif", "gif");
    auto capture = [&] { return launcherRuntime(install / "FetcherLauncher.exe", install / "ui"); };
    auto initial = capture();
    auto inventory = [&] {
        Json files = Json::array();
        auto runtime = capture();
        for (auto it = runtime.begin(); it != runtime.end(); ++it)
            files.push_back({{"path", it.key()}, {"sha256", it.value().at("sha256")},
                             {"size", it.value().at("size")}});
        Json value = {{"schemaVersion", 1}, {"launcherRestartHandshake", 1}, {"files", files}};
        writeAtomic(install / "fetcher-tester-tools.json", value.dump());
        return value;
    };
    inventory();
    writeAtomic(install / "Update-Fetcher-Simulator.ps1", "new updater script");
    writeAtomic(install / "openmw.exe", "new client");
    writeAtomic(install / "Data Files/mod.esp", "new mod");
    check(capture() == initial, "client/content/script-only updates do not need a launcher restart");
    writeAtomic(install / "ui/index.html", "interface two");
    check(capture() != initial, "UI-only update needs restart");
    rejects([&] { stageLauncherRuntime(install, root / "unsafe-runtime-stage"); },
            "changed interface without matching inventory cannot restart");
    check(!fs::exists(root / "unsafe-runtime-stage"), "bad runtime rejected before staging");
    auto current = capture();
    inventory();
    check(stageLauncherRuntime(install, root / "verified-runtime-stage"), "new runtime supports readiness acknowledgement");
    check(launcherRuntime(root / "verified-runtime-stage/FetcherLauncher.exe",
                         root / "verified-runtime-stage/ui") == current,
          "restart stages the exact verified executable and interface together");
    writeAtomic(install / "FetcherLauncher.exe", "executable two");
    check(capture() != current, "executable-only update needs restart");
    auto value = inventory();
    value["files"].push_back({{"path", "ui/missing.js"}, {"sha256", std::string(64, '0')}, {"size", 1}});
    writeAtomic(install / "fetcher-tester-tools.json", value.dump());
    rejects([&] { stageLauncherRuntime(install, root / "missing-runtime-stage"); },
            "partial runtime cannot trigger a handoff");
    value = inventory();
    value["files"].push_back(value["files"][0]);
    writeAtomic(install / "fetcher-tester-tools.json", value.dump());
    rejects([&] { stageLauncherRuntime(install, root / "duplicate-runtime-stage"); },
            "duplicate runtime inventory refused");
    value = inventory();
    value.erase("launcherRestartHandshake");
    writeAtomic(install / "fetcher-tester-tools.json", value.dump());
    check(!stageLauncherRuntime(install, root / "legacy-runtime-stage"), "older inventories select legacy GUI startup acknowledgement");
}
void discoveryTests(const fs::path &root) {
    Json raw = {{"id", "owner-name"}, {"name", "Community"}, {"version", "1"},
                {"host", "redirect.evil.example"}, {"port", 1}, {"packages", Json::array()},
                {"content", Json::array({"Morrowind.esm"})}};
    auto text = raw.dump();
    auto digest = textSha256(text);
    auto p = normalizeDiscoveredManifest(text, digest, "198.51.100.10", 25565);
    check(p.at("host") == "198.51.100.10" && p.at("port") == 25565,
          "profile cannot redirect the selected server endpoint");
    auto largeProfile = raw;
    largeProfile["description"] = std::string(300 * 1024, 'x');
    const auto largeText = largeProfile.dump();
    check(normalizeDiscoveredManifest(largeText, textSha256(largeText), "example.org", 25565)
              .at("description").get<std::string>().size() == 300 * 1024,
          "bounded large profiles can carry complete compatibility recipes");
    const auto oversizedText = std::string(2 * 1024 * 1024 + 1, ' ');
    rejects([&] { normalizeDiscoveredManifest(oversizedText, textSha256(oversizedText), "example.org", 25565); },
            "advertised profiles over two MiB rejected before parsing");
    rejects([&] { normalizeDiscoveredManifest(text + " ", digest, "example.org", 25565); },
            "profile bytes must match the registry digest");
    for (const auto &field : {"umo", "command", "_discovered", "launcherUrl"}) {
        auto bad = raw;
        bad[field] = "https://evil.example/";
        auto bytes = bad.dump();
        rejects([&] { normalizeDiscoveredManifest(bytes, textSha256(bytes), "example.org", 25565); },
                "advertised profiles cannot introduce external manifests or instructions");
    }
    auto bad = raw;
    bad["packages"] = Json::array({{{"id", "unpinned"}, {"repository", "owner/repo"},
                                   {"releaseTag", "latest"}, {"assetName", "mod.zip"}}});
    auto bytes = bad.dump();
    rejects([&] { normalizeDiscoveredManifest(bytes, textSha256(bytes), "example.org", 25565); },
            "advertised GitHub packages require pinned hashes");
    std::string serverId = "01234567-89ab-4cde-8fab-0123456789ab";
    Json entry = {{"id", serverId}, {"host", "198.51.100.10"}, {"port", 25565},
                  {"name", "Community"}, {"current_players", 0}, {"max_players", 32},
                  {"mod_profile_sha256", digest}, {"mod_profile_url", "https://evil.example/profile"}};
    int lists = 0, profiles = 0;
    auto fetch = [&](const std::string &url, std::uint64_t limit, bool redirects) {
        check(!redirects, "discovery forbids redirects");
        if (url == "https://master.fetchers.org/v1/servers") {
            ++lists;
            return Json::array({entry}).dump();
        }
        check(url == "https://master.fetchers.org/v1/servers/" + serverId + "/profile" &&
                  limit == 2 * 1024 * 1024, "master profile URL is derived and bounded");
        ++profiles;
        return text;
    };
    auto install = root / "discovery-install";
    writeAtomic(install / "openmw.cfg", "personal configuration");
    Backend backend(install, {}, fetch);
    backend.command({{"op", "refreshServers"}});
    check(profiles == 0, "listing does not fetch profile or mod archives");
    auto selected = backend.command({{"op", "selectServer"}, {"host", "198.51.100.10"}, {"port", 25565}});
    auto key = selected.at("selectedProfile");
    check(key.get<std::string>().rfind("server-", 0) == 0 && profiles == 1,
          "selecting server automatically installs declarative profile without manual acceptance");
    entry["mod_profile_sha256"] = std::string(64, '0');
    rejects([&] { backend.command({{"op", "launch"}}); }, "Play revalidates and fails closed on changed digest");
    check(lists == 2 && read(install / "openmw.cfg") == "personal configuration",
          "failed discovery preserves personal configuration");
    raw["version"] = "2";
    text = raw.dump();
    entry["mod_profile_sha256"] = textSha256(text);
    Backend reloaded(install, {}, fetch);
    rejects([&] { reloaded.command({{"op", "prepare"}}); }, "missing owned game data still blocks preparation");
    auto state = reloaded.state();
    check(state.at("selectedProfile") == key, "profile identity survives launcher and server restarts");
    bool version2 = false;
    for (const auto &profile : state.at("profiles"))
        if (profile.at("id") == key)
            version2 = profile.at("version") == "2";
    check(version2, "new owner profile version is discovered automatically");
    serverId = "01234567-89ab-4cde-8fab-0123456789ac";
    entry["id"] = serverId;
    rejects([&] { reloaded.command({{"op", "prepare"}}); }, "registry renewal still checks owned game data");
    check(readJson(install / "_fetcher_launcher/profiles" / key.get<std::string>() /
                       "previous-profile.json").at("version") == "1",
          "registry renewal preserves the previous real manifest revision");
    entry.erase("mod_profile_sha256");
    rejects([&] { reloaded.command({{"op", "prepare"}}); }, "removed advertisement cannot silently downgrade to installed mode");
    entry["mod_profile_sha256"] = textSha256(text);
    entry["host"] = "163.192.106.225";
    entry["port"] = 25569;
    auto existingSettings = install / "_fetcher_launcher/profiles/fetcher-vanilla/userdata/settings.cfg";
    writeAtomic(existingSettings, "personal settings");
    reloaded.command({{"op", "refreshServers"}});
    auto migrated = reloaded.command({{"op", "selectServer"}, {"host", "163.192.106.225"}, {"port", 25569}});
    check(migrated.at("selectedProfile") == "fetcher-vanilla" && read(existingSettings) == "personal settings",
          "automatic discovery preserves an existing endpoint's profile settings and data directory");
}
void archiveAndLayoutTests(const fs::path &root) {
    auto archive = root / "sample.7z";
    sevenZip(archive, {{"Data Files/notes.txt", "native LZMA2 test"}});
    auto files = extractArchive(archive, root / "7z-good");
    check(files.size() == 1 && read(root / "7z-good/Data Files/notes.txt") == "native LZMA2 test",
          "7z LZMA2 extraction");
    for (const auto &method : {"lzma1", "bzip2", "deflate", "ppmd"}) {
        auto packed = root / (std::string(method) + ".7z");
        sevenZip(packed, {{"file.txt", std::string(4096, 'A')}}, method);
        auto target = root / (std::string(method) + "-out");
        extractArchive(packed, target);
        check(read(target / "file.txt") == std::string(4096, 'A'),
              "7z alternative codec roundtrip");
    }
    auto fixtures = fs::u8path(FETCHER_ARCHIVE_FIXTURES);
    auto corrupt = read(fixtures / "test_read_format_rar5_compressed.rar");
    corrupt[corrupt.size() / 2] ^= 0x20;
    writeAtomic(root / "corrupt.rar", corrupt);
    rejects([&] { extractArchive(root / "corrupt.rar", root / "rar-corrupt"); },
            "corrupt RAR rejected");
    extractArchive(fixtures / "unicode-lzma2.7z", root / "7z-unicode");
    check(read(root / L"7z-unicode/Data Files/\u00e9.txt") == "Unicode filename",
          "7z independent Unicode filename");
    check(read(root / L"7z-unicode/\u65e5\u672c\u8a9e/sample.txt") == "Japanese directory",
          "7z independent Unicode directory");
    rejects([&] { extractArchive(archive, root / "wrong-format", "rar"); },
            "declared archive format mismatch");
    sevenZip(root / "7z-escape.7z", {{"../escaped.txt", "outside"}});
    rejects([&] { extractArchive(root / "7z-escape.7z", root / "7z-bad"); },
            "7z traversal rejected");
    check(!fs::exists(root / "escaped.txt"), "7z traversal wrote nothing outside stage");
    sevenZip(root / "7z-duplicate.7z", {{"file.txt", "one"}, {"FILE.txt", "two"}});
    rejects([&] { extractArchive(root / "7z-duplicate.7z", root / "7z-duplicates"); },
            "7z case collision rejected");
    auto truncated = read(archive);
    truncated.resize(truncated.size() - 10);
    writeAtomic(root / "truncated.7z", truncated);
    rejects([&] { extractArchive(root / "truncated.7z", root / "truncated-output"); },
            "truncated 7z rejected");
    auto catalog = readJson(fixtures / "upstream.json");
    for (const auto &fixture : catalog.at("fixtures")) {
        auto path = fixtures / fixture.at("file").get<std::string>();
        check(sha256(path) == fixture.at("sha256"), "upstream fixture identity");
        auto target = root / ("fixture-" + path.filename().string());
        if (fixture.contains("expectedFiles")) {
            auto entries = extractArchive(path, target);
            check(entries.size() == fixture.at("expectedFiles").size(), "RAR file count");
            for (const auto &expected : fixture.at("expectedFiles"))
                check(sha256(target / safeRelative(expected.at("path"))) == expected.at("sha256"),
                      "RAR/RAR5 decoded bytes match independent 7-Zip hashes");
        } else
            rejects([&] { extractArchive(path, target); },
                    "RAR/7z links, encryption and incomplete volumes rejected");
    }
    auto stage = root / "layout";
    fs::create_directories(stage);
    writeAtomic(stage / "variants/modern/texture.dds", "modern texture");
    writeAtomic(stage / "Data Files/Old.esp", "plugin bytes");
    writeAtomic(stage / "unused/readme.txt", "unused");
    auto actions = normalizeModActions(Json::parse(R"json([
        {"action":"copy","src":"Mod/variants/modern","dst":"Mod/Data Files/textures"},
        {"action":"rename","src":"Mod/Data Files/Old.esp","dst":"Mod/Data Files/New.esp"},
        {"action":"remove","paths":["Mod/unused"]}
    ])json"),
                                       "Mod");
    check(actions[1]["action"] == "move" && actions[0]["src"] == "variants/modern",
          "UMO paths and action normalization");
    applyModActions(stage, actions);
    check(read(stage / "Data Files/textures/texture.dds") == "modern texture" &&
              read(stage / "Data Files/New.esp") == "plugin bytes" &&
              !fs::exists(stage / "unused") && fs::exists(stage / "variants/modern/texture.dds") &&
              !fs::exists(stage / "Data Files/Old.esp"),
          "native copy/rename/remove layout");
    for (const auto &invalid : {
             R"json([{"action":"run","command":"bad"}])json",
             R"json([{"action":"clean","path":"x.esp"}])json",
             R"json([{"action":"remove","path":"../outside"}])json",
             R"json([{"action":"copy","src":"source","dst":".fetcher-package.json"}])json",
             R"json([{"action":"copy","src":"source","dst":"out","command":"bad"}])json"})
        rejects([&] { normalizeModActions(Json::parse(invalid)); },
                "unsupported/unsafe mod action rejected during review");
    rejects([&] {
        normalizeModActions(
            Json::parse(R"json([{"action":"copy","src":"Other/file","dst":"Mod/out"}])json"),
            "Mod");
    }, "cross-mod UMO action rejected");
    rejects([&] {
        applyModActions(
            stage,
            Json::parse(R"json([{"action":"copy","src":"variants","dst":"variants/nested"}])json"));
    }, "recursive self-copy rejected");
    rejects([&] {
        applyModActions(
            stage,
            Json::parse(
                R"json([{"action":"copy","src":"Data Files/New.esp","dst":"Data Files/textures/texture.dds"}])json"));
    }, "implicit overwrite rejected");
    applyModActions(
        stage,
        Json::parse(
            R"json([{"action":"copy","src":"Data Files/New.esp","dst":"Data Files/textures/texture.dds","force":true}])json"));
    check(read(stage / "Data Files/textures/texture.dds") == "plugin bytes",
          "explicit force overwrite");
}
} // namespace
int main() {
    auto root = fs::temp_directory_path() /
                (L"fetcher-native-tests-" + std::to_wstring(GetCurrentProcessId()));
    try {
        noLinks(root);
        fs::create_directories(root);
        {
            InstallLock lock(root);
            bool refused = false;
            std::thread contender([&] {
                try {
                    InstallLock alias(longPath(root / ""));
                } catch (const std::exception &) {
                    refused = true;
                }
            });
            contender.join();
            check(refused, "extended and trailing-separator aliases share the same update lock");
        }
        check(lower("\xc3\x89") == "\xc3\xa9", "Windows Unicode filename case folding");
        rejects([&] { parseJson(std::string(512, '[') + std::string(512, ']')); },
                "deep JSON metadata rejected");
        for (auto path : {"../outside", "/absolute", "C:/outside", "folder/../x", "AUX.txt",
                          "dir/file.", "dir\\file", "file:stream", "x\nfile"})
            rejects([&] { safeRelative(path); }, "unsafe relative path rejected");
        check(safeRelative("Data Files/meshes/keyboard.nif").filename() == L"keyboard.nif",
              "ordinary asset path");
        startupTests(root);
        runtimeTests(root);
        discoveryTests(root);
        archiveAndLayoutTests(root);
        writeAtomic(root / L"test.txt", "hello");
        check(textSha256("hello") == sha256(root / L"test.txt"), "memory/file SHA-256 consistency");
        auto secret = protectSecret("test-key-not-a-real-credential");
        check(secret.find("test-key-not-a-real-credential") == std::string::npos &&
                  unprotectSecret(secret) == "test-key-not-a-real-credential",
              "Windows user key protection");
        for (auto url : {"http://example.org/archive.zip", "https://127.0.0.1/archive.zip",
                         "https://10.0.0.1/archive.zip", "https://169.254.169.254/latest/meta-data",
                         "https://100.64.0.1/archive.zip", "https://192.168.0.1/archive.zip",
                         "https://198.18.0.1/archive.zip", "https://203.0.113.1/archive.zip",
                         "https://[::1]/archive.zip", "https://[::ffff:127.0.0.1]/archive.zip"})
            rejects([&] { validateDownloadUrl(url); },
                    "production downloads refuse non-public destinations");
        check(displayUrl("https://example.org/archive.zip?secret=hidden#private") ==
                  "https://example.org/archive.zip",
              "download display redacts query and fragment");
        check(sha256(root / L"test.txt") ==
                  "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824",
              "SHA-256 known answer");
        writeAtomic(root / L"test.txt", "updated");
        check(read(root / L"test.txt") == "updated", "atomic replacement");
        // Real Tamriel Data inventories exceed the external manifest JSON budget.
        Json receipt = {{"files", Json::array()}};
        for (int i = 0; i < 50000; ++i)
            receipt["files"].push_back(
                {{"path", "textures/" + std::string(70, 'x') + std::to_string(i) + ".dds"},
                 {"sha256", std::string(64, 'a')},
                 {"size", 1}});
        auto receiptPath = root / "receipt/.fetcher-package.json";
        writeAtomic(receiptPath, receipt.dump(2));
        check(fs::file_size(receiptPath) > 8 * 1024 * 1024, "large receipt fixture exceeds 8 MiB");
        check(readPackageReceipt(root / "receipt").at("files").size() == 50000,
              "bounded internal inventory supports large mod archives");
        rejects([&] { readJson(receiptPath); }, "external JSON retains original 8 MiB limit");
        receipt["files"] = Json::array();
        for (int i = 0; i < 100001; ++i)
            receipt["files"].push_back(Json::object());
        writeAtomic(receiptPath, receipt.dump());
        rejects([&] { readPackageReceipt(root / "receipt"); },
                "internal receipt entry bound retained");
        auto deep = root / std::string(100, 'a') / std::string(100, 'b') / std::string(100, 'c');
        writeAtomic(deep / "note.txt", "long path bytes");
        check(read(deep / "note.txt") == "long path bytes" &&
                  sha256(deep / "note.txt") == textSha256("long path bytes"),
              "extended-length file read/write/hash without registry changes");
        noLinks(deep / "note.txt");
        check(ordinaryPath(longPath(deep)) == deep,
              "extended paths round-trip to ordinary config paths");
        removeManaged(root, deep.parent_path().parent_path());
        check(!fs::exists(longPath(deep)), "long-path cleanup keeps managed containment checks");
        rejects([&] { removeManaged(root, longPath(root)); },
                "extended alias cannot delete managed root");
        zip(root / "good.zip", "Data Files/test.txt");
        auto names = extractZip(root / "good.zip", root / "good");
        check(names.size() == 1 && read(root / "good/Data Files/test.txt") == "hello",
              "native ZIP extraction/CRC");
        auto windowsZip = read(root / "good.zip");
        std::string originalName = "Data Files/test.txt", windowsName = "Data Files\\test.txt";
        for (auto position = windowsZip.find(originalName); position != std::string::npos;
             position = windowsZip.find(originalName, position + windowsName.size()))
            windowsZip.replace(position, originalName.size(), windowsName);
        writeAtomic(root / "windows.zip", windowsZip);
        auto windowsFiles = extractZip(root / "windows.zip", root / "windows");
        check(windowsFiles == std::vector<std::string>{"Data Files/test.txt"} &&
                  read(root / "windows/Data Files/test.txt") == "hello",
              "Windows ZIP separators normalize to canonical relative paths");
        auto longZip = longPath(deep / "ZIP");
        auto longSeven = longPath(deep / "7z");
        check(extractArchive(root / "good.zip", longZip).size() == 1 &&
                  read(longZip / safeRelative("Data Files/test.txt")) == "hello",
              "ZIP extraction uses native separators in extended-length paths");
        check(extractArchive(root / "sample.7z", longSeven).size() == 1 &&
                  read(longSeven / safeRelative("Data Files/notes.txt")) == "native LZMA2 test",
              "7z extraction supports extended-length destinations");
        zip(root / "bad.zip", "../escaped.txt");
        rejects([&] { extractZip(root / "bad.zip", root / "bad"); }, "ZIP traversal rejected");
        auto windowsBad = read(root / "bad.zip");
        std::string traversal = "../escaped.txt", windowsTraversal = "..\\escaped.txt";
        for (auto position = windowsBad.find(traversal); position != std::string::npos;
             position = windowsBad.find(traversal, position + windowsTraversal.size()))
            windowsBad.replace(position, traversal.size(), windowsTraversal);
        writeAtomic(root / "windows-bad.zip", windowsBad);
        rejects([&] { extractZip(root / "windows-bad.zip", root / "windows-bad"); },
                "Windows ZIP traversal remains forbidden after separator normalization");
        check(!fs::exists(root.parent_path() / "escaped.txt"),
              "ZIP traversal wrote nothing outside stage");
        rejects([&] { removeManaged(root, root); }, "managed root cannot delete itself");
        rejects([&] { removeManaged(root, root.parent_path()); }, "managed deletion containment");
        check(endpoint("::1", 25567) == "[::1]:25567", "IPv6 endpoint");
        rejects([&] { endpoint("host --evil", 25567); }, "server argument injection");
        auto servers = Backend::parseServers(Json::array({{{"host", "example.org"},
                                                           {"port", 25565},
                                                           {"name", "Community"},
                                                           {"current_players", 1},
                                                           {"max_players", 32}},
                                                          {{"host", "--bad value"},
                                                           {"port", 25565},
                                                           {"name", "bad"},
                                                           {"current_players", 0},
                                                           {"max_players", 32}}}));
        check(servers.size() == 1 && servers[0]["name"] == "Community",
              "master server validation skips malformed entries");
        auto data = root / "data";
        plugin(data / "Morrowind.esm");
        plugin(data / "A.esp", {"Morrowind.esm"});
        plugin(data / "B.esp", {"A.esp"});
        auto order = Backend::loadOrder({"B.esp", "Morrowind.esm", "A.esp"}, {data});
        check(order == std::vector<std::string>({"Morrowind.esm", "A.esp", "B.esp"}),
              "plugin masters order before dependents");
        rejects([&] { Backend::loadOrder({"B.esp"}, {data}); }, "missing plugin master refused");
        plugin(data / "A.esp", {"B.esp"});
        rejects([&] { Backend::loadOrder({"A.esp", "B.esp"}, {data}); }, "master cycle refused");
        auto install = root / "install", stage = root / "client";
        fs::create_directories(stage);
        writeAtomic(stage / "openmw.exe", "new client");
        writeAtomic(install / "openmw.exe", "old client");
        writeAtomic(install / "openmw.cfg", "personal config");
        writeAtomic(install / "profiles/keep.bin", "personal data");
        Json inventory = {{"schemaVersion", 1},
                          {"files", Json::array({{{"path", "openmw.exe"},
                                                  {"size", fs::file_size(stage / "openmw.exe")},
                                                  {"sha256", sha256(stage / "openmw.exe")}}})}};
        writeAtomic(stage / "fetcher-client-files.json", inventory.dump());
        Backend::installClient(install, stage, Json::object());
        check(read(install / "openmw.exe") == "new client", "native client inventory installation");
        check(read(install / "openmw.cfg") == "personal config" &&
                  read(install / "profiles/keep.bin") == "personal data",
              "native client update preserves config/profile");
        writeAtomic(stage / "openmw.exe", "corrupted");
        rejects([&] { Backend::installClient(install, stage, Json::object()); },
                "corrupt client inventory refused");
        check(read(install / "openmw.exe") == "new client",
              "failed validation leaves installed executable");
        writeAtomic(stage / "openmw.exe", "another client");
        writeAtomic(stage / "settings.cfg", "unwanted settings");
        inventory["files"] = Json::array();
        for (auto name : {"openmw.exe", "settings.cfg"})
            inventory["files"].push_back({{"path", name},
                                          {"size", fs::file_size(stage / name)},
                                          {"sha256", sha256(stage / name)}});
        writeAtomic(stage / "fetcher-client-files.json", inventory.dump());
        rejects([&] { Backend::installClient(install, stage, Json::object()); },
                "protected client payload rejected");
        check(read(install / "openmw.exe") == "new client",
              "partial native client install rolls back earlier replacement");
        auto tools = root / "tools";
        writeAtomic(tools / "FetcherLauncher.exe", "native launcher");
        writeAtomic(tools / "ui/index.html", "native interface");
        Json toolsInventory = {{"schemaVersion", 1}, {"files", Json::array()}};
        for (auto name : {"FetcherLauncher.exe", "ui/index.html"})
            toolsInventory["files"].push_back({{"path", name},
                                               {"size", fs::file_size(tools / name)},
                                               {"sha256", sha256(tools / name)}});
        writeAtomic(tools / "fetcher-tester-tools.json", toolsInventory.dump());
        Backend::installTools(install, tools);
        check(read(install / "FetcherLauncher.exe") == "native launcher" &&
                  read(install / "openmw.exe") == "new client",
              "native launcher/tools update preserves game client");
        writeAtomic(tools / "openmw.exe", "wrong owner");
        toolsInventory["files"].push_back({{"path", "openmw.exe"},
                                           {"size", fs::file_size(tools / "openmw.exe")},
                                           {"sha256", sha256(tools / "openmw.exe")}});
        writeAtomic(tools / "fetcher-tester-tools.json", toolsInventory.dump());
        rejects([&] { Backend::installTools(install, tools); },
                "tools cannot overwrite client-owned executable");
        check(read(install / "openmw.exe") == "new client" &&
                  read(install / "openmw.cfg") == "personal config",
              "tool ownership failure preserves client and configuration");
        Backend backend(install, {}, [](const std::string &, std::uint64_t, bool) { return "[]"; });
        auto s = backend.command({{"op", "init"}});
        check(s["selectedProfile"] == "installed",
              "existing installations default to their existing mod setup");
        backend.command({{"op", "selectServer"}, {"host", "163.192.106.225"}, {"port", 25569}});
        check(backend.state()["selectedProfile"] == "fetcher-vanilla",
              "known server selects dedicated profile");
        const auto profiles = backend.state().at("profiles");
        const auto &vanilla = profiles[1];
        check(vanilla.at("packages").size() == 1 && vanilla.at("content").size() == 10 &&
                  vanilla.at("content").back() == "FetcherVehicles.omwaddon" &&
                  vanilla.at("requiredFiles").size() == 4,
              "Vanilla cutover selects the complete surf/vehicle/Studio content and attribution");
        backend.command({{"op", "favorite"}, {"host", "example.org"}, {"port", 25565}});
        Backend reload(install);
        check(reload.state()["favorites"].size() == 1, "favorites persist");
        check(reload.state()["theme"] == "graphite", "existing installs default to Graphite");
        for (const auto &theme : {"graphite", "obsidian", "space", "ember"}) {
            reload.command({{"op", "theme"}, {"id", theme}});
            Backend themedReload(install);
            check(themedReload.state()["theme"] == theme &&
                      themedReload.state()["favorites"].size() == 1 &&
                      themedReload.state()["selectedProfile"] == "fetcher-vanilla" &&
                      read(install / "openmw.cfg") == "personal config",
                  "appearance survives a new launcher process without changing game setup");
        }
        const auto savedAppearance = read(install / "_fetcher_launcher/settings.json");
        rejects([&] { reload.command({{"op", "theme"}, {"id", "https://example.org/theme"}}); },
                "only built-in themes accepted");
        check(read(install / "_fetcher_launcher/settings.json") == savedAppearance,
              "invalid theme leaves saved preferences intact");
        removeManaged(fs::temp_directory_path(), root);
        std::cout << "PASS native backend: " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "\n";
        if (fs::exists(root))
            removeManaged(fs::temp_directory_path(), root);
        return 1;
    }
}
