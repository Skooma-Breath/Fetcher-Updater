#include "legacy_migration.hpp"
#include "server_ping.hpp"
#include <iostream>
#include <windows.h>
using namespace fetcher;
namespace
{
    int checks = 0;
    void check(bool b, const char* m)
    {
        ++checks;
        if (!b)
            throw std::runtime_error(m);
    }
    template <class F>
    void rejects(F f, const char* m)
    {
        bool rejected = false;
        try
        {
            f();
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        check(rejected, m);
    }
}
int main()
{
    auto root = fs::temp_directory_path() / (L"fetcher-migration-" + std::to_wstring(GetCurrentProcessId()));
    try
    {
        fs::create_directories(root);
        check(fs::weakly_canonical(root.parent_path()) == fs::weakly_canonical(fs::temp_directory_path()),
            "fixture confined to temp directory");
        Json inventory = { { "packages", Json::array() } };
        for (auto name : { "TamrielData", "TamrielRebuilt" })
        {
            std::string relative = std::string("Data Files/fetcher-simulator/")
                + (name == std::string("TamrielData") ? "ModdingResources/" : "Landmasses/") + name;
            auto path = root / relative / "keep.txt";
            writeAtomic(path, "original asset");
            inventory["packages"].push_back({ { "oldRoot", relative }, { "sha256", textSha256(name) },
                { "files", Json::array({ Json::array({ "keep.txt", fs::file_size(path), sha256(path) }) }) } });
        }
        Json replacements = Json::object();
        for (auto name : { "td", "core", "factions" })
        {
            auto path = root / "_fetcher_launcher/packages/current" / name;
            fs::create_directories(path);
            replacements[name] = utf8(path.wstring());
            writeAtomic(path
                    / (name == std::string("td")          ? "Tamriel_Data.esm"
                            : name == std::string("core") ? "TR_Mainland.esm"
                                                          : "TR_Factions.esp"),
                "new master");
        }
        auto td = root / safeRelative(inventory["packages"][0]["oldRoot"]),
             tr = root / safeRelative(inventory["packages"][1]["oldRoot"]);
        auto original = "data=\"" + utf8((td / "00 Data Files").wstring())
            + "\"\r\ncontent=personal.esp\r\nfallback=custom,value\r\n";
        writeAtomic(root / "openmw.cfg", original);
        writeAtomic(root / "profiles/player/settings.cfg", "user settings");
        writeAtomic(root / "local/openmw.cfg", "data=\"" + utf8((tr / "00 Core").wstring()) + "\"\n");
        auto preview = legacyMigration(root, inventory, replacements, "preview");
        check(preview["configs"].size() == 2 && preview["folders"].size() == 2,
            "preview covers main and saved configurations");
        auto external = root.parent_path() / (root.filename().wstring() + L"-external.cfg");
        writeAtomic(external, "data=\"" + utf8((td / "00 Data Files").wstring()) + "\"\n");
        writeAtomic(root / "openmw.cfg", original + "config=\"" + utf8(external.wstring()) + "\"\n");
        rejects([&] { legacyMigration(root, inventory, replacements, "preview"); },
            "external referenced configurations block automatic mutation");
        fs::remove(external);
        writeAtomic(root / "openmw.cfg", original);
        auto outside = root.parent_path() / (root.filename().wstring() + L"-outside");
        fs::create_directories(outside);
        writeAtomic(outside / "keep.txt", "external data");
        if (CreateSymbolicLinkW((td / "alias").c_str(), outside.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 2))
        {
            rejects([&] { legacyMigration(root, inventory, replacements, "preview"); },
                "directory links block legacy migration");
            check(read(outside / "keep.txt") == "external data", "external symlink target preserved");
            fs::remove(td / "alias");
        }
        removeManaged(fs::temp_directory_path(), outside);
        writeAtomic(td / "personal.txt", "personal file");
        rejects([&] { legacyMigration(root, inventory, replacements, "stage", preview["token"]); },
            "unknown files block migration before writes");
        check(read(root / "openmw.cfg") == original && fs::exists(td / "keep.txt"),
            "blocked migration preserves files and config");
        fs::remove(td / "personal.txt");
        auto time = fs::last_write_time(td / "keep.txt");
        writeAtomic(td / "keep.txt", "modified asset");
        fs::last_write_time(td / "keep.txt", time);
        rejects([&] { legacyMigration(root, inventory, replacements, "stage", preview["token"]); },
            "same-size timestamp-preserving edits block migration");
        writeAtomic(td / "keep.txt", "original asset");
        preview = legacyMigration(root, inventory, replacements, "preview");
        writeAtomic(root / "openmw.cfg", original + "# later edit\n");
        rejects([&] { legacyMigration(root, inventory, replacements, "stage", preview["token"]); },
            "stale preview refuses configuration changes");
        writeAtomic(root / "openmw.cfg", original);
        preview = legacyMigration(root, inventory, replacements, "preview");
        legacyMigration(root, inventory, replacements, "stage", preview["token"]);
        check(!fs::exists(td) && !fs::exists(tr), "old paths absent during game acceptance");
        auto migrated = read(root / "openmw.cfg");
        check(migrated.find("content=personal.esp") != std::string::npos
                && migrated.find("fallback=custom,value") != std::string::npos,
            "load order and unrelated settings preserved");
        writeAtomic(root / "openmw.cfg", migrated + "# custom edit\n");
        rejects([&] { legacyMigration(root, inventory, replacements, "restore"); },
            "restore refuses to overwrite a user edit");
        rejects([&] { legacyMigration(root, inventory, replacements, "finish"); },
            "finish refuses changed migrated configs");
        writeAtomic(root / "openmw.cfg", migrated);
        // An interrupted stage can be restored using the same persisted journal.
        auto record = root / "_fetcher_launcher/migrations/trtd-v1/record.json";
        auto value = readJson(record);
        value["phase"] = "staging";
        writeAtomic(record, value.dump());
        legacyMigration(root, inventory, replacements, "restore");
        check(read(root / "openmw.cfg") == original && read(td / "keep.txt") == "original asset",
            "restore recovers original bytes and directories");
        preview = legacyMigration(root, inventory, replacements, "preview");
        legacyMigration(root, inventory, replacements, "stage", preview["token"]);
        legacyMigration(root, inventory, replacements, "finish");
        check(legacyMigrationStatus(root)["phase"] == "finished" && !fs::exists(td), "verified retirement finishes");
        check(read(root / "profiles/player/settings.cfg") == "user settings"
                && fs::exists(fs::path(wide(replacements["td"])) / "Tamriel_Data.esm"),
            "characters/settings and new assets retained");
        check(
            fs::exists(root / "_fetcher_launcher/migrations/trtd-v1/configs/0.txt"), "configuration backups retained");
        auto pings = FetcherNet::measureServerPings({ "127.0.0.1", "127.0.0.1", "not-a-numeric-address" });
        check(pings.size() == 3 && pings[0].milliseconds >= 0 && pings[0].milliseconds == pings[1].milliseconds
                && pings[2].milliseconds == -1 && !pings[0].tcp,
            "numeric loopback probe, deduplication and invalid-host state");
        auto cancelled = FetcherNet::measureServerPings({ "127.0.0.1" }, [] { return true; });
        check(cancelled[0].milliseconds == -1, "cancelled ping batch does not probe");
        removeManaged(fs::temp_directory_path(), root);
        std::cout << checks << " migration/ping checks passed.\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\nFixture retained: " << root << "\n";
        return 1;
    }
}
