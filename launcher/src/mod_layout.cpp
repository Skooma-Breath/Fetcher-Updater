#include "server_manifest.hpp"
#include <set>
namespace fetcher {
namespace {
void need(bool ok, const std::string &text) {
    if (!ok)
        throw std::runtime_error(text);
}
constexpr std::uint64_t maxBytes = 8ull * 1024 * 1024 * 1024;
constexpr std::uint64_t maxEntries = 100000;
std::string actionPath(const Json &value, const std::string &prefix) {
    auto name = value.get<std::string>();
    if (!prefix.empty()) {
        need(name.rfind(prefix + "/", 0) == 0,
             "UMO layout paths must stay inside this download's extract_to directory.");
        name.erase(0, prefix.size() + 1);
    }
    auto path = safeRelative(name);
    for (const auto &part : path)
        need(lower(utf8(part.wstring())).rfind(".fetcher-", 0) != 0,
             "Layout operations cannot address launcher metadata.");
    return name;
}
bool descendant(const fs::path &root, const fs::path &child) {
    auto relative = child.lexically_relative(root);
    return !relative.empty() && relative != L"." && *relative.begin() != L"..";
}
fs::path inside(const fs::path &root, const std::string &relative) {
    auto target = (root / safeRelative(relative)).lexically_normal();
    need(descendant(root, target), "Layout operation escaped its staging directory.");
    noLinks(target);
    return target;
}
struct Usage {
    std::uint64_t bytes = 0, entries = 0;
};
Usage usage(const fs::path &path) {
    Usage result;
    auto count = [&](const fs::path &p) {
        noLinks(p);
        ++result.entries;
        if (fs::is_regular_file(p)) {
            auto size = fs::file_size(p);
            need(size <= maxBytes - result.bytes, "Layout size exceeds limit.");
            result.bytes += size;
        } else
            need(fs::is_directory(p), "Layout contains a special file.");
        need(result.entries <= maxEntries, "Layout has too many entries.");
    };
    count(path);
    if (fs::is_directory(path))
        for (const auto &entry : fs::recursive_directory_iterator(path))
            count(entry.path());
    return result;
}
void copyTree(const fs::path &root, const fs::path &src, const fs::path &dst, bool force) {
    auto copyOne = [&](const fs::path &from, const fs::path &to) {
        need(descendant(root, to), "Copy target escaped its staging directory.");
        noLinks(from);
        noLinks(to);
        if (fs::is_directory(from)) {
            need(!fs::exists(to) || fs::is_directory(to), "Copy directory collides with a file.");
            fs::create_directories(to);
        } else {
            need(fs::is_regular_file(from), "Copy source is not a regular file.");
            need(!fs::exists(to) || force, "Copy collision: declare force=true to replace a file.");
            need(!fs::exists(to) || fs::is_regular_file(to),
                 "Copy file collides with a directory.");
            fs::create_directories(to.parent_path());
            fs::copy_file(from, to,
                          force ? fs::copy_options::overwrite_existing : fs::copy_options::none);
        }
    };
    copyOne(src, dst);
    if (fs::is_directory(src))
        for (const auto &entry : fs::recursive_directory_iterator(src))
            copyOne(entry.path(), dst / entry.path().lexically_relative(src));
}
}
Json normalizeModActions(const Json &actions, const std::string &prefix) {
    need(actions.is_array() && actions.size() <= 128,
         "A package supports at most 128 layout operations.");
    Json normalized = Json::array();
    for (const auto &item : actions) {
        need(item.is_object(), "Invalid layout operation.");
        auto type = item.at("action").get<std::string>();
        need(type == "copy" || type == "rename" || type == "move" || type == "remove",
             "Unsupported mod action: " + type +
                 ". Only copy, move/rename and remove are allowed.");
        std::set<std::string> keys =
            type == "remove" ? std::set<std::string>{"action", "path", "paths", "tes3cmd"}
                             : std::set<std::string>{"action", "src", "dst", "force", "tes3cmd"};
        for (auto it = item.begin(); it != item.end(); ++it)
            need(keys.count(it.key()) != 0, "Unsupported layout field: " + it.key());
        need(!item.contains("tes3cmd") || item.at("tes3cmd").is_null(),
             "External tools cannot be specified by layout actions.");
        if (type == "remove") {
            bool one = item.contains("path") && !item.at("path").is_null();
            bool many = item.contains("paths") && !item.at("paths").is_null();
            need(one != many, "Remove requires either path or paths.");
            auto paths = one ? Json::array({item.at("path")}) : item.at("paths");
            need(paths.is_array() && !paths.empty() && paths.size() <= 128,
                 "Invalid remove path list.");
            for (const auto &path : paths)
                normalized.push_back({{"action", "remove"}, {"path", actionPath(path, prefix)}});
        } else {
            bool force = item.contains("force") && !item.at("force").is_null()
                             ? item.at("force").get<bool>()
                             : false;
            normalized.push_back({{"action", type == "rename" ? "move" : type},
                                  {"src", actionPath(item.at("src"), prefix)},
                                  {"dst", actionPath(item.at("dst"), prefix)},
                                  {"force", force}});
        }
        need(normalized.size() <= 128, "Too many normalized layout operations.");
    }
    return normalized;
}
std::string modLayoutKey(const Json &definition, const std::string &sourceSha256) {
    auto actions = normalizeModActions(definition.value("actions", Json::array()));
    Json recipe = {{"layoutVersion", 1}, {"sourceSha256", sourceSha256}, {"actions", actions}};
    if (definition.contains("fomod")) {
        recipe["fomodVersion"] = 1;
        recipe["fomod"] = normalizeFomod(definition.at("fomod"));
    }
    return textSha256(recipe.dump());
}
void applyModActions(const fs::path &directory, const Json &actions) {
    auto root = fs::absolute(directory).lexically_normal();
    noLinks(root);
    need(fs::is_directory(root), "Layout staging directory is missing.");
    usage(root);
    for (const auto &item : normalizeModActions(actions)) {
        auto type = item.at("action").get<std::string>();
        if (type == "remove") {
            auto target = inside(root, item.at("path"));
            need(fs::exists(target),
                 "Remove source is missing: " + item.at("path").get<std::string>());
            removeManaged(root, target);
            continue;
        }
        auto src = inside(root, item.at("src")), dst = inside(root, item.at("dst"));
        need(fs::exists(src), "Layout source is missing: " + item.at("src").get<std::string>());
        if (fs::is_directory(dst) && (type == "move" || fs::is_regular_file(src)))
            dst /= src.filename();
        noLinks(dst);
        // Compare case-insensitively because these operations run on Windows.
        auto source = fs::path(wide(lower(utf8(src.generic_wstring()))));
        auto target = fs::path(wide(lower(utf8(dst.generic_wstring()))));
        need(source != target && !descendant(source, target) && !descendant(target, source),
             "Layout source/target may not overlap or contain one another.");
        bool force = item.at("force");
        if (type == "copy") {
            auto current = usage(root), extra = usage(src);
            need(extra.bytes <= maxBytes - current.bytes &&
                     extra.entries <= maxEntries - current.entries,
                 "Copy exceeds the layout expansion limit.");
            need(!fs::is_directory(src) || !fs::exists(dst) || force,
                 "Directory merge requires force=true.");
            copyTree(root, src, dst, force);
        } else {
            if (fs::exists(dst)) {
                need(force, "Move collision: declare force=true to replace a target.");
                removeManaged(root, dst);
            }
            fs::create_directories(dst.parent_path());
            fs::rename(src, dst);
        }
        usage(root);
    }
}
} // namespace fetcher
