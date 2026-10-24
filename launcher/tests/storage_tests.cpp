#include "storage_management.hpp"
#include "server_manifest.hpp"
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace fetcher;
namespace {
int checks = 0;
void check(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F fn, const char *message) {
    bool rejected = false;
    try { fn(); } catch (const std::exception &) { rejected = true; }
    check(rejected, message);
}
Json definition(const std::string &hash) {
    return {{"id", "fixture"}, {"url", "https://example.org/archive.zip"}, {"sha256", hash}};
}
std::string seed(const fs::path &root, const std::string &name) {
    auto archive = "archive-" + name;
    auto key = textSha256(archive);
    auto path = root / "_fetcher_launcher/packages" / key;
    writeAtomic(path / "Data Files/test.esp", "payload-" + name);
    auto file = path / "Data Files/test.esp";
    writeAtomic(path / ".fetcher-package.json", Json{{"sha256", key}, {"files", Json::array({
        {{"path", "Data Files/test.esp"}, {"size", fs::file_size(file)}, {"sha256", sha256(file)}}})}}.dump());
    writeAtomic(root / "_fetcher_launcher/downloads" / (key + ".zip"), archive);
    return key;
}
void catalog(const fs::path &root, const std::vector<std::string> &keys) {
    Json packages = Json::array();
    for (const auto &key : keys) packages.push_back(definition(key));
    writeAtomic(root / "fetcher-launcher-profiles.json", Json{{"profiles", Json::array({{{"id", "kept"}, {"packages", packages}}})}}.dump());
}
}
int main() {
    auto base = fs::temp_directory_path() / (L"fetcher-storage-" + std::to_wstring(GetCurrentProcessId()));
    try {
        check(fs::weakly_canonical(base.parent_path()) == fs::weakly_canonical(fs::temp_directory_path()), "test root confined to temporary directory");
        fs::create_directories(base);
        auto root = base / L"inställ with spaces";
        fs::create_directories(root);
        auto resolver = [](const Json &) -> Json { throw std::runtime_error("offline fixture"); };
        auto active = seed(root, "active"), previous = seed(root, "previous"), rollback = seed(root, "rollback"), unused = seed(root, "unused");
        auto legacy = seed(root, "legacy-client");
        writeAtomic(root / "fetcher-update-state.json", Json{{"client", {{"assetDigest", "sha256:" + legacy}}}}.dump());
        catalog(root, {active});
        auto saved = definition(previous);
        saved["actions"] = Json::array({{{"action", "remove"}, {"path", "unused.txt"}}});
        writeAtomic(root / "_fetcher_launcher/profiles/old/previous-profile.json",
                    Json{{"packages", Json::array({saved})}}.dump());
        auto savedLayout = modLayoutKey(saved, previous);
        auto layoutPath = root / "_fetcher_launcher/layouts" / savedLayout;
        writeAtomic(layoutPath / "kept.esp", "retained derived payload");
        writeAtomic(layoutPath / ".fetcher-package.json", Json{{"layoutKey", savedLayout}, {"sourceSha256", previous},
            {"files", Json::array({{{"path", "kept.esp"}, {"size", fs::file_size(layoutPath / "kept.esp")}, {"sha256", sha256(layoutPath / "kept.esp")}}})}}.dump());
        auto rollbackPath = root / "_fetcher_launcher/packages" / rollback;
        auto included = base / "included";
        auto rollbackConfig = root / "_fetcher_launcher/rollback/tx/config";
        writeAtomic(rollbackConfig / "openmw.cfg", "config=\"" + utf8(included.wstring()) + "\"\n");
        writeAtomic(included / "openmw.cfg", "data=\"" + utf8(rollbackPath.wstring()) + "\"\nconfig=\"" + utf8(rollbackConfig.wstring()) + "\"\n");
        writeAtomic(root / "Data Files/TamrielRebuilt/manual.esp", "manual data");
        auto plan = scanUnusedCache(root, resolver);
        check(plan["canClean"] && plan["candidates"].size() == 2, "only unreferenced archive and package are candidates");
        auto token = plan.at("token").get<std::string>();
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
        fs::copy_file(fs::path(executable).parent_path() / "fetcher-launch-fixture.exe", root / "openmw.exe");
        writeAtomic(root / "hold-process", "hold");
        auto command = L"\"" + (root / "openmw.exe").wstring() + L"\"";
        STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION process{};
        check(CreateProcessW((root / "openmw.exe").c_str(), command.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, root.c_str(), &si, &process) != 0, "test game started without a window");
        for (int i = 0; i < 200 && !fs::exists(root / "launch-capture.json"); ++i) Sleep(10);
        rejects([&] { cleanUnusedCache(root, token, resolver); }, "running game blocks cleanup");
        fs::remove(root / "hold-process");
        check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "test game exited");
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        catalog(root, {active, unused});
        rejects([&] { cleanUnusedCache(root, token, resolver); }, "newly referenced cache invalidates preview");
        check(fs::exists(root / "_fetcher_launcher/packages" / unused), "stale cleanup removes nothing");
        catalog(root, {active});
        auto payload = root / "_fetcher_launcher/packages" / unused / "Data Files/test.esp";
        auto original = read(payload); auto time = fs::last_write_time(payload);
        auto edited = original; edited[0] ^= 1;
        writeAtomic(payload, edited); fs::last_write_time(payload, time);
        plan = scanUnusedCache(root, resolver);
        rejects([&] { cleanUnusedCache(root, plan.at("token"), resolver); }, "same-size timestamp-preserving edit blocks all deletion");
        check(fs::exists(root / "_fetcher_launcher/downloads" / (unused + ".zip")), "archive retained when package verification fails");
        writeAtomic(payload, original);
        plan = scanUnusedCache(root, resolver);
        auto removed = cleanUnusedCache(root, plan.at("token"), resolver);
        check(removed["removedEntries"] == 2, "verified unused entries removed");
        check(!fs::exists(root / "_fetcher_launcher/packages" / unused), "unused package removed");
        for (const auto &kept : {active, previous, rollback, legacy})
            check(fs::exists(root / "_fetcher_launcher/packages" / kept) && fs::exists(root / "_fetcher_launcher/downloads" / (kept + ".zip")), "current/saved/rollback sources retained");
        check(read(layoutPath / "kept.esp") == "retained derived payload", "unprepared saved recipe retains its layout");
        check(read(root / "Data Files/TamrielRebuilt/manual.esp") == "manual data", "manual legacy TR never removed");
        auto unknown = seed(root, "extra");
        writeAtomic(root / "_fetcher_launcher/packages" / unknown / "personal.txt", "user file");
        plan = scanUnusedCache(root, resolver);
        check(std::none_of(plan["candidates"].begin(), plan["candidates"].end(), [&](const Json &c) { return c["kind"] == "packages" && c["key"] == unknown; }), "package containing unlisted personal file retained");
        auto linked = textSha256("linked");
        auto outside = base / "outside"; writeAtomic(outside / "keep.txt", "outside file");
        auto link = root / "_fetcher_launcher/packages" / linked;
        if (CreateSymbolicLinkW(link.c_str(), outside.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 2)) {
            plan = scanUnusedCache(root, resolver);
            check(std::none_of(plan["candidates"].begin(), plan["candidates"].end(), [&](const Json &c) { return c["key"] == linked; }), "reparse cache entry excluded");
            check(read(outside / "keep.txt") == "outside file", "external link target preserved");
            fs::remove(link);
            auto aliasSource = seed(root, "alias-source");
            auto alias = base / L"cache & alias";
            auto target = root / "_fetcher_launcher/packages" / aliasSource;
            check(CreateSymbolicLinkW(alias.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 2) != 0, "configured cache alias created");
            std::ostringstream quoted; quoted << std::quoted(utf8(alias.wstring()), '"', '&');
            writeAtomic(root / "openmw.cfg", "data=" + quoted.str() + "\n");
            plan = scanUnusedCache(root, resolver);
            check(std::none_of(plan["candidates"].begin(), plan["candidates"].end(), [&](const Json &c) { return c["key"] == aliasSource; }), "quoted directory alias protects its actual cache source");
            fs::remove(alias);
        } else std::cout << "Directory symlink test unavailable (Windows privilege).\n";
        writeAtomic(root / "openmw.cfg", "data=\"" + utf8((root / "_fetcher_launcher").wstring()) + "\"\n");
        check(!scanUnusedCache(root, resolver).at("canClean").get<bool>(), "broad data root referencing all caches disables cleanup");
        for (auto kind : {"packages", "layouts", "downloads"}) {
            writeAtomic(root / "openmw.cfg", "data=\"" + utf8((root / "_fetcher_launcher" / kind).wstring()) + "\"\n");
            check(!scanUnusedCache(root, resolver).at("canClean").get<bool>(), "whole cache category data root disables cleanup");
        }
        fs::remove(root / "openmw.cfg");
        writeAtomic(root / "_fetcher_launcher/profiles/broken/previous-profile.json", "{invalid");
        check(!scanUnusedCache(root, resolver).at("canClean").get<bool>(), "unreadable saved configuration disables cleanup");
        auto hints = definition(textSha256("missing")); hints["downloadBytes"] = 100ull; hints["expandedBytes"] = 300ull;
        auto estimate = estimateStorage(root, Json{{"id", "size-test"}, {"packages", Json::array({hints, hints})}}, resolver);
        check(estimate["downloadBytes"] == 100 && estimate["extractionBytes"] == 300, "shared sources counted once");
        auto volume = diskSpace(root);
        check(volume["availableBytes"].is_number_unsigned() && !volume["volume"].get<std::string>().empty(), "actual install volume reports free space");
        rejects([&] { requireFreeSpace(root, ~std::uint64_t(0)); }, "insufficient space rejected without writing");
        hints["downloadBytes"] = -1;
        rejects([&] { validateModDefinition(hints); }, "negative owner size hint rejected");
        hints["downloadBytes"] = 9ull * 1024 * 1024 * 1024;
        rejects([&] { validateModDefinition(hints); }, "oversized hint rejected");
        fs::remove_all(base);
        std::cout << checks << " storage safety checks passed.\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << "\nFixture retained: " << base << '\n'; return 1; }
}
