#include "game_assets.hpp"
#include "server_manifest.hpp"
#include <windows.h>
#include <iostream>
using namespace fetcher;
namespace {
int assertions = 0;
void check(bool ok, const char *message) {
    ++assertions;
    if (!ok)
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
void integer(std::string &out, std::uint32_t n) {
    for (int i = 0; i < 4; ++i)
        out.push_back(char((n >> (i * 8)) & 255));
}
std::string bsa(const std::vector<std::pair<std::string, std::string>> &files) {
    std::string names, content, entries, pointers;
    for (const auto &file : files) {
        integer(entries, std::uint32_t(file.second.size()));
        integer(entries, std::uint32_t(content.size()));
        integer(pointers, std::uint32_t(names.size()));
        names += file.first;
        names.push_back('\0');
        content += file.second;
    }
    std::string result;
    integer(result, 0x100);
    integer(result, std::uint32_t(files.size() * 12 + names.size()));
    integer(result, std::uint32_t(files.size()));
    return result + entries + pointers + names + std::string(files.size() * 8, '\0') + content;
}
Json patch(const std::string &path, const std::string &source, const std::string &output) {
    return {{"action", "patch"},
            {"path", path},
            {"sourceSha256", textSha256(source)},
            {"sourceSize", source.size()},
            {"outputSha256", textSha256(output)},
            {"outputSize", output.size()},
            {"operations", Json::array({{{"copyOffset", 0}, {"copyLength", source.size()}}})}};
}
}
int main() {
    auto root = fs::temp_directory_path() /
                (L"fetcher-game-assets-tests-" + std::to_wstring(GetCurrentProcessId()));
    try {
        auto data = root / "owned", stage = root / "stage";
        fs::create_directories(stage);
        auto archive =
            bsa({{"meshes\\test.nif", "hello old world"}, {"textures\\\xe9.dds", "owned texture"}});
        writeAtomic(data / "Morrowind.bsa", archive);
        writeAtomic(data / "Sound/loose.wav", "owned sound");
        auto nif = patch("Meshes/restored.nif", "hello old world", "hello new world");
        nif["operations"] = Json::array({{{"copyOffset", 0}, {"copyLength", 6}},
                                         {{"data", "bmV3"}},
                                         {{"copyOffset", 9}, {"copyLength", 6}}});
        Json action = {
            {"action", "restoreGameAssets"},
            {"files",
             Json::array(
                 {{{"archive", "Morrowind.bsa"}, {"sourcePath", "MESHES/TEST.NIF"}, {"patch", nif}},
                  {{"archive", "Morrowind.bsa"},
                   {"sourcePath", "textures/\xc3\xa9.dds"},
                   {"patch", patch("Textures/copied.dds", "owned texture", "owned texture")}},
                  {{"sourcePath", "Sound/loose.wav"},
                   {"patch", patch("Sound/restored.wav", "owned sound", "owned sound")}}})}};
        applyModActions(stage, Json::array({action}), data);
        check(read(stage / "Meshes/restored.nif") == "hello new world", "BSA asset reconstruction");
        check(read(stage / "Textures/copied.dds") == "owned texture",
              "BSA legacy encoding and case folding");
        check(read(stage / "Sound/restored.wav") == "owned sound", "owned loose sound copying");
        check(sha256(data / "Morrowind.bsa") == textSha256(archive) &&
                  read(data / "Sound/loose.wav") == "owned sound",
              "owned files never changed");
        fs::rename(data / "Morrowind.bsa", data / "temporarily unavailable.bsa");
        applyModActions(stage, Json::array({action}), data);
        check(read(stage / "Meshes/restored.nif") == "hello new world", "restoration idempotence");
        fs::rename(data / "temporarily unavailable.bsa", data / "Morrowind.bsa");
        writeAtomic(stage / "Meshes/restored.nif", "local edit");
        rejects([&] { applyModActions(stage, Json::array({action}), data); },
                "replacement requires explicit force");
        check(read(stage / "Meshes/restored.nif") == "local edit",
              "unapproved replacement preserved");
        action["force"] = true;
        auto bad = action;
        bad["files"][0]["patch"]["sourceSha256"] = std::string(64, '0');
        rejects([&] { applyModActions(stage, Json::array({bad}), data); },
                "unknown owned bytes rejected");
        check(read(stage / "Meshes/restored.nif") == "local edit",
              "source mismatch preserves destination");
        applyModActions(stage, Json::array({action}), data);
        check(read(stage / "Meshes/restored.nif") == "hello new world",
              "explicit replacement restores pinned result");
        bad = action;
        bad["files"][0]["archive"] = "Other.bsa";
        rejects([&] { normalizeModActions(Json::array({bad})); }, "arbitrary BSA names forbidden");
        bad = action;
        bad["files"][0]["sourcePath"] = "../test.nif";
        rejects([&] { normalizeModActions(Json::array({bad})); }, "source traversal forbidden");
        bad = action;
        bad["files"][0]["sourcePath"] = "settings.cfg";
        rejects([&] { normalizeModActions(Json::array({bad})); },
                "non-asset loose sources forbidden");
        bad = action;
        bad["files"][0]["patch"]["path"] = "scripts/actor.lua";
        rejects([&] { normalizeModActions(Json::array({bad})); },
                "restoration cannot install scripts");
        bad = action;
        bad["files"][0]["patch"]["path"] = ".fetcher-hidden/result.nif";
        rejects([&] { normalizeModActions(Json::array({bad})); },
                "metadata destinations forbidden");
        bad = action;
        bad["files"].push_back(bad["files"][0]);
        rejects([&] { normalizeModActions(Json::array({bad})); },
                "duplicate destinations forbidden");
        bad = action;
        bad["command"] = "external tool";
        rejects([&] { normalizeModActions(Json::array({bad})); },
                "restoration cannot execute tools");
        auto single = action;
        single["files"] = Json::array({action["files"][0]});
        single["files"][0]["patch"]["path"] = "Meshes/new.nif";
        rejects([&] { applyModActions(stage, Json::array({single})); },
                "owned game data context required");
        auto malformed = archive;
        malformed[0] = char(1);
        writeAtomic(data / "Morrowind.bsa", malformed);
        rejects([&] { applyModActions(stage, Json::array({single}), data); },
                "unsupported BSA header rejected");
        malformed = archive;
        for (int i = 0; i < 4; ++i)
            malformed[16 + i] = char(255);
        writeAtomic(data / "Morrowind.bsa", malformed);
        rejects([&] { applyModActions(stage, Json::array({single}), data); },
                "out-of-bounds BSA offsets rejected");
        writeAtomic(data / "Morrowind.bsa", bsa({{"Meshes/test.nif", "hello old world"},
                                                 {"meshes/TEST.NIF", "hello old world"}}));
        rejects([&] { applyModActions(stage, Json::array({single}), data); },
                "duplicate BSA names rejected");
        writeAtomic(data / "Morrowind.bsa", archive.substr(0, 15));
        rejects([&] { applyModActions(stage, Json::array({single}), data); },
                "truncated BSA directory rejected");
        check(!fs::exists(stage / "Meshes/new.nif"), "invalid archives do not write output");
        Json removes = Json::array();
        for (int index = 0; index < 512; ++index)
            removes.push_back({{"action", "remove"},
                               {"path", "Textures/unused" + std::to_string(index) + ".dds"}});
        check(normalizeModActions(removes).size() == 512,
              "large explicit cleanup recipes are bounded");
        removes.push_back({{"action", "remove"}, {"path", "Textures/one-too-many.dds"}});
        rejects([&] { normalizeModActions(removes); }, "layout action cap enforced");
        removeManaged(fs::temp_directory_path(), root);
        std::cout << "PASS owned-game assets: " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
