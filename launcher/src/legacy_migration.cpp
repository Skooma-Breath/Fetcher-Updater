#include "legacy_migration.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
namespace fetcher
{
    namespace
    {
        void need(bool value, const std::string& message)
        {
            if (!value)
                throw std::runtime_error(message);
        }
        fs::path area(const fs::path& root)
        {
            auto p = root / "_fetcher_launcher/migrations/trtd-v1";
            noLinks(p);
            return p;
        }
        std::string key(const fs::path& path)
        {
            return lower(utf8(fs::weakly_canonical(path).generic_wstring()));
        }
        struct Config
        {
            fs::path path;
            std::string before, after;
        };
        struct Folder
        {
            fs::path path;
            Json definition;
            std::vector<fs::path> files, directories;
            std::uint64_t bytes = 0;
            std::string snapshot;
        };
        Folder inspect(const fs::path& path, const Json& definition, bool verify, const Log& log)
        {
            noLinks(path);
            need(fs::is_directory(path), "Legacy folder is missing.");
            Folder folder;
            folder.path = path;
            folder.definition = definition;
            std::map<std::string, Json> expected;
            for (const auto& file : definition.at("files"))
            {
                auto name = file.at(0).get<std::string>();
                safeRelative(name);
                need(expected.emplace(lower(name), file).second, "Duplicate legacy inventory entry.");
            }
            Json stamps = Json::object();
            std::size_t count = 0;
            // A pre-order directory walk checks every parent before its children.
            for (const auto& entry : fs::recursive_directory_iterator(longPath(path)))
            {
                need(++count <= 100000, "Legacy folder exceeds the migration limit.");
                if (entry.is_symlink())
                    throw std::runtime_error("Legacy migration refuses links.");
                if (entry.is_directory())
                {
                    noLinks(entry.path());
                    folder.directories.push_back(entry.path());
                    continue;
                }
                need(entry.is_regular_file(), "Legacy folder contains a special file.");
                auto name = utf8(entry.path().lexically_relative(longPath(path)).generic_wstring());
                safeRelative(name);
                auto found = expected.find(lower(name));
                need(found != expected.end(), "Unrecognized legacy file; preserve it before migrating: " + name);
                const auto& record = found->second;
                auto size = entry.file_size();
                need(size == record.at(1).get<std::uint64_t>(), "Modified legacy file: " + name);
                if (verify)
                    need(sha256(entry.path()) == record.at(2), "Modified legacy file; nothing removed: " + name);
                folder.bytes += size;
                folder.files.push_back(entry.path());
                stamps[name]
                    = std::to_string(size) + ":" + std::to_string(entry.last_write_time().time_since_epoch().count());
                if (log && verify && folder.files.size() % 10000 == 0)
                    log("Verified " + std::to_string(folder.files.size()) + " legacy files.\n");
            }
            folder.snapshot = textSha256(stamps.dump());
            return folder;
        }
        std::vector<Config> configurations(const fs::path& root, const Json& inventory, const Json& replacements)
        {
            std::map<std::string, fs::path> paths;
            auto td = root / safeRelative(inventory.at("packages").at(0).at("oldRoot"));
            auto tr = root / safeRelative(inventory.at("packages").at(1).at("oldRoot"));
            paths[key(td / "00 Data Files")] = wide(replacements.at("td"));
            paths[key(tr / "00 Core")] = wide(replacements.at("core"));
            paths[key(tr / "01 Faction Integration")] = wide(replacements.at("factions"));
            std::vector<Config> result;
            std::set<std::string> visited;
            std::size_t total = 0;
            std::function<void(const fs::path&)> scan = [&](const fs::path& path) {
                noLinks(path);
                if (!fs::is_regular_file(path) || !visited.insert(key(path)).second)
                    return;
                need(visited.size() <= 10000, "Too many configuration files for migration.");
                auto before = read(path, 8 * 1024 * 1024);
                total += before.size();
                need(total <= 64 * 1024 * 1024, "Configuration scan is too large.");
                std::istringstream input(before);
                std::ostringstream output;
                std::string line;
                bool changed = false;
                static const std::regex directive("^(.*?)(data|config)\\s*=\\s*(.*?)\\s*$");
                while (std::getline(input, line))
                {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    std::smatch match;
                    if (std::regex_match(line, match, directive)
                        && (match[1].str().find_first_not_of(" \t") == std::string::npos
                            || match[1].str().back() == '/'))
                    {
                        auto value = match[3].str();
                        if (!value.empty() && value.front() == '"')
                        {
                            std::istringstream q(value);
                            q >> std::quoted(value, '"', '&');
                            need(bool(q), "Invalid quoted configuration path.");
                        }
                        else
                        {
                            value = value.substr(0, value.find('#'));
                            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                                value.pop_back();
                        }
                        if (!value.empty())
                        {
                            fs::path target = wide(value);
                            const bool relative = target.is_relative();
                            if (relative)
                                target = (match[2] == "config" ? path.parent_path() : root) / target;
                            if (match[2] == "config")
                            {
                                need(value.find_first_of("?%") == std::string::npos,
                                    "Unresolved configuration include blocks migration.");
                                scan(fs::is_directory(target) ? target / "openmw.cfg" : target);
                            }
                            else
                            {
                                auto found = paths.find(key(target));
                                if (found == paths.end() && relative)
                                    found = paths.find(key(path.parent_path() / fs::path(wide(value))));
                                if (found != paths.end())
                                {
                                    need(key(path).rfind(key(root) + "/", 0) == 0,
                                        "An external configuration still references legacy TR/TD. "
                                        "Migrate it manually first.");
                                    std::ostringstream quoted;
                                    quoted << std::quoted(utf8(found->second.generic_wstring()), '"', '&');
                                    line = match[1].str() + "data=" + quoted.str();
                                    changed = true;
                                }
                            }
                        }
                    }
                    output << line << "\n";
                }
                if (changed)
                    result.push_back({ path, before, output.str() });
            };
            scan(root / "openmw.cfg");
            for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it)
            {
                auto name = lower(utf8(it->path().filename().wstring()));
                if (it->is_directory())
                {
                    if (name == "data files" || name == "_fetcher_launcher" || name == "_fetcher_umo"
                        || name == "_fetcher_update" || name == "resources" || name == "logs" || name == "screenshots"
                        || name == "content-data" || name == ".git" || it->is_symlink())
                        it.disable_recursion_pending();
                }
                else if (lower(it->path().extension().string()) == ".cfg")
                    scan(it->path());
            }
            return result;
        }
        void replacementsReady(const fs::path& root, const Json& replacements)
        {
            for (auto name : { "td", "core", "factions" })
            {
                fs::path p = wide(replacements.at(name));
                noLinks(p);
                need(key(p).rfind(key(root / "_fetcher_launcher") + "/", 0) == 0,
                    "Replacement is outside the managed cache.");
                need(fs::is_directory(p), "Prepare the current experimental profile before migrating.");
            }
            need(fs::is_regular_file(fs::path(wide(replacements.at("td"))) / "Tamriel_Data.esm")
                    && fs::is_regular_file(fs::path(wide(replacements.at("core"))) / "TR_Mainland.esm")
                    && fs::is_regular_file(fs::path(wide(replacements.at("factions"))) / "TR_Factions.esp"),
                "Replacement TR/TD masters are missing.");
        }
        Json readRecord(const fs::path& root)
        {
            auto path = area(root) / "record.json";
            return fs::exists(path) ? readJson(path) : Json::object();
        }
    }
    Json loadLegacyInventory(const fs::path& interfaceFile)
    {
        noLinks(interfaceFile);
        auto html = read(interfaceFile, 16 * 1024 * 1024);
        const std::string marker = "<script type=\"application/json\" id=\"fetcherLegacyInventory\">";
        auto begin = html.find(marker);
        need(begin != std::string::npos, "Legacy verification inventory is missing. Update the launcher first.");
        begin += marker.size();
        if (begin < html.size() && html[begin] == '\r')
            ++begin;
        need(begin < html.size() && html[begin] == '\n', "Legacy verification inventory has an invalid header.");
        ++begin;
        auto end = html.find("</script>", begin);
        need(end != std::string::npos, "Legacy verification inventory is incomplete. Update the launcher first.");
        auto bytes = html.substr(begin, end - begin);
        // configure_file and Windows checkouts may use CRLF. Pin canonical LF bytes.
        bytes.erase(std::remove(bytes.begin(), bytes.end(), '\r'), bytes.end());
        need(textSha256(bytes) == "09eac1458c4360b0f7c965c72bab9fdb177b7f91053b6cc7fcbc4a0271df1b09",
            "Legacy verification inventory is modified. Repair the launcher first.");
        return parseJson(bytes, 16 * 1024 * 1024);
    }
    Json legacyMigrationStatus(const fs::path& root)
    {
        auto record = readRecord(root);
        return { { "phase", record.value("phase", "none") }, { "bytes", record.value("bytes", std::uint64_t(0)) } };
    }
    Json legacyMigration(const fs::path& root, const Json& inventory, const Json& replacements,
        const std::string& operation, const std::string& token, const Log& log)
    {
        InstallLock lock(root);
        auto base = area(root);
        auto record = readRecord(root);
        if (operation == "restore")
        {
            assertGameStopped(root);
            need(record.value("phase", "") == "staged" || record.value("phase", "") == "staging",
                "No staged migration to restore.");
            for (const auto& config : record.at("configs"))
            {
                auto path = root / safeRelative(config.at("path"));
                noLinks(path);
                auto hash = sha256(path);
                need(hash == config.at("after") || hash == config.at("before"),
                    "A migrated configuration was edited; refusing to overwrite it.");
            }
            for (const auto& item : record.at("folders"))
            {
                auto source = base / "retired" / safeRelative(item.at("sha256"));
                auto dest = root / safeRelative(item.at("oldRoot"));
                noLinks(source);
                noLinks(dest);
                need(fs::exists(source) != fs::exists(dest),
                    "Legacy folder state is ambiguous; preserve both folders for manual "
                    "recovery.");
            }
            for (const auto& config : record.at("configs"))
                writeAtomic(root / safeRelative(config.at("path")), read(base / safeRelative(config.at("backup"))));
            for (const auto& item : record.at("folders"))
            {
                auto source = base / "retired" / safeRelative(item.at("sha256"));
                auto dest = root / safeRelative(item.at("oldRoot"));
                noLinks(source);
                noLinks(dest);
                if (fs::exists(source))
                    fs::rename(source, dest);
            }
            record["phase"] = "restored";
            writeAtomic(base / "record.json", record.dump(2));
            return legacyMigrationStatus(root);
        }
        replacementsReady(root, replacements);
        const bool finish = operation == "finish";
        if (finish)
            need(record.value("phase", "") == "staged" || record.value("phase", "") == "removing",
                "No verified staged migration to finish.");
        else
            need(record.value("phase", "") != "staged" && record.value("phase", "") != "staging"
                    && record.value("phase", "") != "removing",
                "Finish or restore the existing migration first.");
        auto configs = configurations(root, inventory, replacements);
        if (finish)
            need(configs.empty(),
                "A configuration again references legacy "
                "directories; restore or migrate it before removing "
                "files.");
        std::vector<Folder> folders;
        Json summary
            = { { "configs", Json::array() }, { "folders", Json::array() }, { "bytes", 0 }, { "canMigrate", false } };
        for (const auto& config : configs)
            summary["configs"].push_back({ { "path", utf8(config.path.lexically_relative(root).generic_wstring()) },
                { "before", textSha256(config.before) }, { "after", textSha256(config.after) } });
        for (const auto& definition : inventory.at("packages"))
        {
            auto path = finish ? base / "retired" / safeRelative(definition.at("sha256"))
                               : root / safeRelative(definition.at("oldRoot"));
            noLinks(path);
            if (!fs::exists(path))
                continue;
            auto folder = inspect(path, definition, operation != "preview", log);
            summary["folders"].push_back({ { "oldRoot", definition.at("oldRoot") },
                { "sha256", definition.at("sha256") }, { "snapshot", folder.snapshot }, { "bytes", folder.bytes } });
            summary["bytes"] = summary["bytes"].get<std::uint64_t>() + folder.bytes;
            folders.push_back(std::move(folder));
        }
        summary["canMigrate"] = !folders.empty();
        summary["token"] = textSha256(summary.dump());
        if (operation == "preview")
            return summary;
        assertGameStopped(root);
        if (finish)
        {
            // All content was checked before the first deletion. Check migrated configs
            // too.
            for (const auto& config : record.at("configs"))
                need(sha256(root / safeRelative(config.at("path"))) == config.at("after"),
                    "A migrated configuration changed; nothing removed.");
            record["phase"] = "removing";
            writeAtomic(base / "record.json", record.dump(2));
            for (const auto& folder : folders)
            {
                need(key(folder.path).rfind(key(base / "retired") + "/", 0) == 0,
                    "Retired folder escaped its migration area.");
                for (const auto& file : folder.files)
                {
                    noLinks(file);
                    fs::remove(file);
                }
                auto dirs = folder.directories;
                std::sort(dirs.begin(), dirs.end(),
                    [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
                for (const auto& dir : dirs)
                {
                    noLinks(dir);
                    fs::remove(dir);
                }
                noLinks(folder.path);
                fs::remove(folder.path);
            }
            record["phase"] = "finished";
            writeAtomic(base / "record.json", record.dump(2));
            return legacyMigrationStatus(root);
        }
        need(operation == "stage" && summary["canMigrate"] && summary["token"] == token,
            "Migration preview changed. Preview again.");
        record = { { "phase", "staging" }, { "bytes", summary["bytes"] }, { "folders", summary["folders"] },
            { "configs", summary["configs"] } };
        for (std::size_t i = 0; i < configs.size(); ++i)
        {
            auto backup = "configs/" + std::to_string(i) + ".txt";
            writeAtomic(base / backup, configs[i].before);
            record["configs"][i]["backup"] = backup;
        }
        writeAtomic(base / "record.json", record.dump(2));
        std::size_t moved = 0, written = 0;
        try
        {
            for (const auto& config : configs)
                need(read(config.path) == config.before, "Configuration changed during migration.");
            for (const auto& config : configs)
            {
                writeAtomic(config.path, config.after);
                ++written;
            }
            for (const auto& folder : folders)
            {
                auto dest = base / "retired" / safeRelative(folder.definition.at("sha256"));
                noLinks(dest);
                need(!fs::exists(dest), "Retirement destination exists.");
                fs::create_directories(dest.parent_path());
                fs::rename(folder.path, dest);
                ++moved;
            }
            record["phase"] = "staged";
            writeAtomic(base / "record.json", record.dump(2));
        }
        catch (...)
        {
            for (std::size_t i = 0; i < moved; ++i)
                fs::rename(base / "retired" / safeRelative(folders[i].definition.at("sha256")), folders[i].path);
            for (std::size_t i = 0; i < written; ++i)
            {
                need(read(configs[i].path) == configs[i].after,
                    "A configuration was edited; use the retained migration backups for "
                    "recovery.");
                writeAtomic(configs[i].path, configs[i].before);
            }
            record["phase"] = "restored";
            writeAtomic(base / "record.json", record.dump(2));
            throw;
        }
        return legacyMigrationStatus(root);
    }
}
