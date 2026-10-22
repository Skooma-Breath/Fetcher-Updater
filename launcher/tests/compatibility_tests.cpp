#include "compatibility_patch.hpp"
#include "server_manifest.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
using namespace fetcher;
namespace {
int count = 0;
void check(bool ok, const char *message) {
    ++count;
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
std::string bytes(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
}
int main() {
    auto root = fs::temp_directory_path() /
                (L"fetcher-compat-tests-" + std::to_wstring(GetCurrentProcessId()));
    try {
        auto file = root / "scripts/test.lua";
        writeAtomic(file, "hello old world");
        Json patch = {{"action", "patch"},
                      {"path", "scripts/test.lua"},
                      {"sourceSize", 15},
                      {"sourceSha256", textSha256("hello old world")},
                      {"outputSize", 15},
                      {"outputSha256", textSha256("hello new world")},
                      {"operations", Json::array({{{"copyOffset", 0}, {"copyLength", 6}},
                                                  {{"data", "bmV3"}},
                                                  {{"copyOffset", 9}, {"copyLength", 6}}})}};
        applyModActions(root, Json::array({patch}));
        check(read(file) == "hello new world", "native delta reconstructs copied/literal bytes");
        applyModActions(root, Json::array({patch}));
        check(read(file) == "hello new world", "native delta is idempotent");
        writeAtomic(file, "user edited");
        rejects([&] { applyModActions(root, Json::array({patch})); }, "unknown source rejected");
        check(read(file) == "user edited", "unknown source preserved");
        writeAtomic(file, "hello old world");
        auto bad = patch;
        bad["outputSha256"] = std::string(64, '0');
        rejects([&] { applyModActions(root, Json::array({bad})); }, "bad output digest rejected");
        check(read(file) == "hello old world", "failed output verification preserves source");
        bad = patch;
        bad["operations"][0]["copyOffset"] = 999;
        rejects([&] { normalizeModActions(Json::array({bad})); },
                "copy outside source rejected during review");
        bad = patch;
        bad["operations"][1]["data"] = "not base64!";
        rejects([&] { normalizeModActions(Json::array({bad})); },
                "invalid literal rejected during review");
        bad = patch;
        bad["operations"][0]["command"] = "run";
        rejects([&] { normalizeModActions(Json::array({bad})); }, "patch cannot invoke tools");
        bad = patch;
        bad["path"] = ".fetcher-package.json";
        rejects([&] { normalizeModActions(Json::array({bad})); },
                "patch cannot address launcher metadata");
        bad = patch;
        bad["path"] = "Other/test.lua";
        rejects([&] { normalizeModActions(Json::array({bad}), "Mine"); },
                "UMO patches scoped to download");
        bad = patch;
        bad["path"] = "Mine/scripts/test.lua";
        check(normalizeModActions(Json::array({bad}), "Mine")[0]["path"] == "scripts/test.lua",
              "UMO patch path normalized");
        auto created = patch;
        created["path"] = "scripts/new.lua";
        created["sourceSize"] = 0;
        created["sourceSha256"] = textSha256("");
        created["outputSize"] = 3;
        created["outputSha256"] = textSha256("new");
        created["operations"] = Json::array({{{"data", "bmV3"}}});
        rejects([&] { applyModActions(root, Json::array({created})); },
                "missing source requires explicit creation");
        created["create"] = true;
        applyModActions(root, Json::array({created}));
        check(read(root / "scripts/new.lua") == "new", "explicit bounded creation");
        created["sourceSize"] = 1;
        rejects([&] { normalizeModActions(Json::array({created})); },
                "creation requires empty-source fingerprint");
        // Binary/BOM input must not pass through the metadata reader's BOM stripping.
        auto source = std::string("\xef\xbb\xbfhello\0", 9);
        writeAtomic(file, source);
        auto binary = patch;
        binary["sourceSize"] = source.size();
        binary["sourceSha256"] = textSha256(source);
        binary["outputSize"] = 6;
        binary["outputSha256"] = textSha256(std::string("hello\0", 6));
        binary["operations"] = Json::array({{{"copyOffset", 3}, {"copyLength", 6}}});
        applyModActions(root, Json::array({binary}));
        check(bytes(file) == std::string("hello\0", 6),
              "BOM and null bytes preserved as binary data");
        removeManaged(fs::temp_directory_path(), root);
        std::cout << "PASS native compatibility: " << count << " assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
