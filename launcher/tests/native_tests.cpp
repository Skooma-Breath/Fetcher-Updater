#include "native_backend.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
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
    check(mz_zip_writer_add_mem(&z, name.c_str(), "hello", 5, MZ_BEST_COMPRESSION) != 0, "add zip entry");
    check(mz_zip_writer_finalize_archive(&z) != 0, "finalize zip");
    mz_zip_writer_end(&z);
}
} // namespace
int main() {
    auto root =
        fs::temp_directory_path() / (L"fetcher-native-tests-" + std::to_wstring(GetCurrentProcessId()));
    try {
        noLinks(root);
        fs::create_directories(root);
        check(lower("\xc3\x89") == "\xc3\xa9", "Windows Unicode filename case folding");
        rejects([&] { parseJson(std::string(512, '[') + std::string(512, ']')); },
                "deep JSON metadata rejected");
        for (auto path : {"../outside", "/absolute", "C:/outside", "folder/../x", "AUX.txt", "dir/file.",
                          "dir\\file", "file:stream", "x\nfile"})
            rejects([&] { safeRelative(path); }, "unsafe relative path rejected");
        check(safeRelative("Data Files/meshes/keyboard.nif").filename() == L"keyboard.nif",
              "ordinary asset path");
        writeAtomic(root / L"test.txt", "hello");
        check(sha256(root / L"test.txt") ==
                  "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824",
              "SHA-256 known answer");
        writeAtomic(root / L"test.txt", "updated");
        check(read(root / L"test.txt") == "updated", "atomic replacement");
        zip(root / "good.zip", "Data Files/test.txt");
        auto names = extractZip(root / "good.zip", root / "good");
        check(names.size() == 1 && read(root / "good/Data Files/test.txt") == "hello",
              "native ZIP extraction/CRC");
        zip(root / "bad.zip", "../escaped.txt");
        rejects([&] { extractZip(root / "bad.zip", root / "bad"); }, "ZIP traversal rejected");
        check(!fs::exists(root.parent_path() / "escaped.txt"), "ZIP traversal wrote nothing outside stage");
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
        check(read(install / "openmw.exe") == "new client", "failed validation leaves installed executable");
        writeAtomic(stage / "openmw.exe", "another client");
        writeAtomic(stage / "settings.cfg", "unwanted settings");
        inventory["files"] = Json::array();
        for (auto name : {"openmw.exe", "settings.cfg"})
            inventory["files"].push_back(
                {{"path", name}, {"size", fs::file_size(stage / name)}, {"sha256", sha256(stage / name)}});
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
            toolsInventory["files"].push_back(
                {{"path", name}, {"size", fs::file_size(tools / name)}, {"sha256", sha256(tools / name)}});
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
        Backend backend(install);
        auto s = backend.command({{"op", "init"}});
        check(s["selectedProfile"] == "installed",
              "existing installations default to their existing mod setup");
        backend.command({{"op", "selectServer"}, {"host", "163.192.106.225"}, {"port", 25569}});
        check(backend.state()["selectedProfile"] == "fetcher-vanilla",
              "known server selects dedicated profile");
        backend.command({{"op", "favorite"}, {"host", "example.org"}, {"port", 25565}});
        Backend reload(install);
        check(reload.state()["favorites"].size() == 1, "favorites persist");
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
