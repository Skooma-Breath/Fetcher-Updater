#include "game_assets.hpp"
#include "compatibility_patch.hpp"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <set>
namespace fetcher {
namespace {
constexpr std::uint64_t blobLimit = 64ull * 1024 * 1024;
void need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void keys(const Json &value, const std::set<std::string> &allowed) {
    need(value.is_object(), "Invalid owned-game asset object.");
    for (auto it = value.begin(); it != value.end(); ++it)
        need(allowed.count(it.key()) != 0, "Unknown owned-game asset field.");
}
std::string assetPath(const Json &value) {
    auto name = value.get<std::string>();
    auto path = safeRelative(name);
    static const std::set<std::string> extensions = {".dds", ".nif", ".kf", ".tga", ".wav"};
    need(extensions.count(lower(path.extension().string())) != 0,
         "Owned-game restoration supports only model, texture, animation and sound assets.");
    for (const auto &part : path)
        need(lower(utf8(part.wstring())).rfind(".fetcher-", 0) != 0,
             "Owned-game assets cannot address launcher metadata.");
    return name;
}
std::string archiveName(const Json &value) {
    auto name = value.get<std::string>();
    need(name.empty() || name == "Morrowind.bsa" || name == "Tribunal.bsa" ||
             name == "Bloodmoon.bsa",
         "Owned-game archives are restricted to the three base-game BSA filenames.");
    return name;
}
std::uint32_t u32(const char *bytes) {
    auto p = reinterpret_cast<const unsigned char *>(bytes);
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}
std::string legacyName(const std::string &bytes) {
    int length = MultiByteToWideChar(1252, 0, bytes.data(), int(bytes.size()), nullptr, 0);
    need(length > 0, "Invalid BSA asset name.");
    std::wstring name(length, L'\0');
    need(MultiByteToWideChar(1252, 0, bytes.data(), int(bytes.size()), name.data(), length) ==
             length,
         "Could not decode BSA asset name.");
    auto result = lower(utf8(name));
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}
class Bsa {
    struct Entry {
        std::uint64_t offset, size;
    };
    std::ifstream mInput;
    std::map<std::string, Entry> mEntries;
    void read(char *bytes, std::size_t size) {
        mInput.read(bytes, static_cast<std::streamsize>(size));
        need(bool(mInput), "Truncated owned-game BSA.");
    }

  public:
    explicit Bsa(const fs::path &path) {
        noLinks(path);
        need(fs::is_regular_file(path), "An owned-game BSA is missing.");
        auto length = fs::file_size(path);
        need(length >= 12 && length <= 4ull * 1024 * 1024 * 1024,
             "Owned-game BSA size is unsupported.");
        mInput.open(longPath(path), std::ios::binary);
        need(bool(mInput), "Could not open owned-game BSA.");
        char header[12];
        read(header, 12);
        std::uint64_t directorySize = u32(header + 4), count = u32(header + 8);
        need(u32(header) == 0x100 && count > 0 && count <= 200000,
             "Expected an uncompressed Morrowind-format BSA.");
        auto recordsSize = count * 12;
        auto dataStart = 12 + directorySize + count * 8;
        need(directorySize >= recordsSize && directorySize <= blobLimit && dataStart <= length,
             "Owned-game BSA directory exceeds bounds.");
        std::string directory(static_cast<std::size_t>(directorySize), '\0');
        read(directory.data(), directory.size());
        auto nameSize = directorySize - recordsSize;
        for (std::size_t index = 0; index < count; ++index) {
            auto size = u32(directory.data() + index * 8);
            std::uint64_t offset = u32(directory.data() + index * 8 + 4);
            std::uint64_t nameOffset = u32(directory.data() + count * 8 + index * 4);
            need(offset <= length - dataStart && size <= length - dataStart - offset &&
                     nameOffset < nameSize,
                 "Owned-game BSA entry exceeds archive bounds.");
            auto start = recordsSize + nameOffset;
            auto end = directory.find('\0', static_cast<std::size_t>(start));
            need(end != std::string::npos && end > start && end - start < 1024,
                 "Owned-game BSA name is invalid.");
            auto name = legacyName(directory.substr(static_cast<std::size_t>(start), end - start));
            need(mEntries.emplace(name, Entry{dataStart + offset, size}).second,
                 "Ambiguous duplicate asset in owned-game BSA.");
        }
    }
    std::string get(const std::string &name, std::uint64_t expectedSize) {
        auto found = mEntries.find(lower(name));
        need(found != mEntries.end(), "Required asset is missing from owned-game BSA.");
        need(found->second.size == expectedSize && expectedSize <= blobLimit,
             "Owned-game BSA asset has an unexpected size.");
        mInput.clear();
        mInput.seekg(static_cast<std::streamoff>(found->second.offset));
        need(bool(mInput), "Could not seek owned-game BSA asset.");
        std::string bytes(static_cast<std::size_t>(expectedSize), '\0');
        read(bytes.data(), bytes.size());
        return bytes;
    }
};
}
Json normalizeGameAssets(const Json &definition) {
    keys(definition, {"action", "files", "force"});
    need(definition.at("action") == "restoreGameAssets", "Invalid owned-game restoration action.");
    const auto &files = definition.at("files");
    need(files.is_array() && !files.empty() && files.size() <= 4096,
         "Owned-game restoration requires 1 to 4096 explicit files.");
    Json result = {{"action", "restoreGameAssets"},
                   {"force", definition.value("force", false)},
                   {"files", Json::array()}};
    std::set<std::string> targets;
    std::uint64_t bytes = 0;
    for (const auto &file : files) {
        keys(file, {"archive", "sourcePath", "patch"});
        auto patch = normalizeFilePatch(file.at("patch"));
        auto path = assetPath(patch.at("path"));
        need(patch.at("create") == false,
             "Owned-game restoration cannot use empty-source creation.");
        need(targets.insert(lower(path)).second, "Duplicate owned-game asset target.");
        auto size = patch.at("outputSize").get<std::uint64_t>();
        need(size <= 8ull * 1024 * 1024 * 1024 - bytes,
             "Owned-game restoration exceeds the layout byte limit.");
        bytes += size;
        result["files"].push_back({{"archive", archiveName(file.value("archive", ""))},
                                   {"sourcePath", assetPath(file.at("sourcePath"))},
                                   {"patch", patch}});
    }
    return result;
}
void restoreGameAssets(const fs::path &stage, const fs::path &gameData, const Json &definition) {
    auto action = normalizeGameAssets(definition);
    need(!gameData.empty(), "Select owned Morrowind Data Files before restoring game assets.");
    auto root = longPath(gameData);
    noLinks(root);
    std::map<std::string, std::unique_ptr<Bsa>> archives;
    for (const auto &file : action.at("files")) {
        const auto &patch = file.at("patch");
        auto target = stage / safeRelative(patch.at("path"));
        noLinks(target);
        if (fs::is_regular_file(target) && fs::file_size(target) == patch.at("outputSize") &&
            sha256(target) == patch.at("outputSha256"))
            continue;
        need(!fs::exists(target) || action.at("force") == true,
             "Owned-game asset replacement requires explicit force=true.");
        auto archive = file.at("archive").get<std::string>();
        auto size = patch.at("sourceSize").get<std::uint64_t>();
        std::string bytes;
        if (!archive.empty()) {
            if (!archives.count(archive))
                archives[archive] = std::make_unique<Bsa>(root / safeRelative(archive));
            bytes = archives.at(archive)->get(file.at("sourcePath"), size);
        } else {
            auto source = root / safeRelative(file.at("sourcePath"));
            noLinks(source);
            need(fs::is_regular_file(source) && fs::file_size(source) == size,
                 "Owned loose asset is missing or has an unexpected size.");
            std::ifstream input(source, std::ios::binary);
            bytes.resize(static_cast<std::size_t>(size));
            input.read(bytes.data(), static_cast<std::streamsize>(size));
            need(bool(input), "Could not read owned loose asset.");
        }
        need(textSha256(bytes) == patch.at("sourceSha256"),
             "Owned-game asset bytes differ from the profile's pinned original.");
        writeAtomic(target, bytes);
        applyFilePatch(target, patch);
    }
}
}
