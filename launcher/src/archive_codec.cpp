#include "native_platform.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <set>
namespace fetcher {
namespace {
void need(bool ok, const std::string &text) {
    if (!ok)
        throw std::runtime_error(text);
}
std::string error(struct archive *a) {
    const char *message = archive_error_string(a);
    return std::string("Archive decoding failed: ") +
           (message ? message : "invalid or unsupported archive");
}
}
std::string detectArchiveFormat(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    std::array<unsigned char, 8> magic{};
    file.read(reinterpret_cast<char *>(magic.data()), magic.size());
    auto count = file.gcount();
    if (count >= 4 && magic[0] == 'P' && magic[1] == 'K' &&
        ((magic[2] == 3 && magic[3] == 4) || (magic[2] == 5 && magic[3] == 6)))
        return "zip";
    if (count >= 6 &&
        std::equal(magic.begin(), magic.begin() + 6,
                   std::array<unsigned char, 6>{'7', 'z', 0xbc, 0xaf, 0x27, 0x1c}.begin()))
        return "7z";
    if (count >= 7 &&
        std::equal(magic.begin(), magic.begin() + 6,
                   std::array<unsigned char, 6>{'R', 'a', 'r', '!', 0x1a, 0x07}.begin()) &&
        (magic[6] == 0 || (count >= 8 && magic[6] == 1 && magic[7] == 0)))
        return "rar";
    throw std::runtime_error(
        "Unsupported archive signature: expected ZIP, 7z or RAR (no executable/SFX archives).");
}
std::vector<std::string> extractArchive(const fs::path &path, const fs::path &stage,
                                        const std::string &format) {
    auto kind = detectArchiveFormat(path);
    need(format == "auto" || format == kind, "Archive bytes do not match the declared format.");
    if (kind == "zip")
        return extractZip(path, stage);
    noLinks(stage);
    need(!fs::exists(stage), "Extraction stage must be new.");
    fs::create_directories(stage);
    std::unique_ptr<struct archive, decltype(&archive_read_free)> a(archive_read_new(),
                                                                    archive_read_free);
    need(bool(a), "Could not create archive decoder.");
    // No program filters, archive scripts, disk writers or auto-discovered formats.
    need(archive_read_support_filter_none(a.get()) == ARCHIVE_OK,
         "Archive filter initialization failed.");
    if (kind == "7z")
        need(archive_read_support_format_7zip(a.get()) == ARCHIVE_OK, "7z decoder unavailable.");
    else {
        need(archive_read_support_format_rar(a.get()) == ARCHIVE_OK, "RAR decoder unavailable.");
        need(archive_read_support_format_rar5(a.get()) == ARCHIVE_OK, "RAR5 decoder unavailable.");
    }
    need(archive_read_open_filename_w(a.get(), path.c_str(), 65536) == ARCHIVE_OK, error(a.get()));
    struct archive_entry *entry = nullptr;
    std::set<std::string> names;
    std::vector<std::string> files;
    std::uint64_t total = 0;
    int result;
    while ((result = archive_read_next_header(a.get(), &entry)) != ARCHIVE_EOF) {
        need(result == ARCHIVE_OK, error(a.get()));
        need(names.size() < 100000, "Too many archive entries.");
        need(archive_entry_is_encrypted(entry) != 1,
             "Encrypted archives are unsupported; choose an unencrypted mod archive.");
        need(!archive_entry_symlink(entry) && !archive_entry_hardlink(entry),
             "Archive links are forbidden.");
        auto type = archive_entry_filetype(entry);
        // Some RAR Unix headers omit file-type bits. We only materialize regular files,
        // and reject recognized links before reaching this compatibility case.
        if (kind == "rar" && type == 0)
            type = AE_IFREG;
        need(type == AE_IFREG || type == AE_IFDIR,
             "Archive special files are forbidden (type " + std::to_string(type) + ").");
        const wchar_t *wideName = archive_entry_pathname_w(entry);
        const char *utf8Name = archive_entry_pathname_utf8(entry);
        need(wideName || utf8Name, "Archive filename cannot be decoded.");
        std::string name = wideName ? utf8(wideName) : utf8Name;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (type == AE_IFDIR && !name.empty() && name.back() == '/')
            name.pop_back();
        auto relative = safeRelative(name);
        need(lower(name) != ".fetcher-package.json", "Archive uses reserved launcher metadata.");
        need(names.insert(lower(name)).second, "Duplicate archive path.");
        if (type == AE_IFDIR) {
            fs::create_directories(stage / relative);
            continue;
        }
        need(archive_entry_size_is_set(entry) && archive_entry_size(entry) >= 0,
             "Archive file size is unavailable.");
        auto expected = static_cast<std::uint64_t>(archive_entry_size(entry));
        need(expected <= 2ull * 1024 * 1024 * 1024 && expected <= 8ull * 1024 * 1024 * 1024 - total,
             "Archive expansion exceeds limit.");
        need(archive_entry_sparse_count(entry) == 0, "Sparse archive entries are unsupported.");
        requireFreeSpace(stage, expected);
        fs::create_directories((stage / relative).parent_path());
        std::ofstream out(stage / relative, std::ios::binary | std::ios::trunc);
        need(bool(out), "Could not create extracted file.");
        std::array<char, 65536> buffer{};
        std::uint64_t written = 0;
        for (;;) {
            auto count = archive_read_data(a.get(), buffer.data(), buffer.size());
            need(count >= 0, error(a.get()));
            if (!count)
                break;
            need(static_cast<std::uint64_t>(count) <= expected - written,
                 "Archive file exceeds its declared size.");
            written += count;
            out.write(buffer.data(), count);
            need(bool(out), "Archive write failed.");
        }
        out.close();
        need(bool(out) && written == expected,
             "Archive file is truncated or could not be written.");
        total += written;
        files.push_back(name);
    }
    need(archive_read_has_encrypted_entries(a.get()) != 1, "Encrypted archives are unsupported.");
    need(archive_read_close(a.get()) == ARCHIVE_OK, error(a.get()));
    return files;
}
} // namespace fetcher
