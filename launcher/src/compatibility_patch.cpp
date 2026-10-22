#include "compatibility_patch.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <fstream>
#include <regex>
#include <set>
namespace fetcher {
namespace {
constexpr std::uint64_t limit = 64ull * 1024 * 1024;
void need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::uint64_t size(const Json &value) {
    need(value.is_number_integer(), "Patch sizes/offsets must be integers.");
    auto n = value.get<std::int64_t>();
    need(n >= 0 && static_cast<std::uint64_t>(n) <= limit, "Patch size/offset exceeds limit.");
    return n;
}
std::string hash(const Json &value) {
    auto s = value.get<std::string>();
    need(std::regex_match(s, std::regex("[a-fA-F0-9]{64}")),
         "Patch must pin source and output SHA-256.");
    return lower(s);
}
std::string decode(const std::string &value) {
    need(value.size() % 4 == 0 && value.size() <= limit * 2, "Invalid patch literal size.");
    auto padding = value.find('=');
    if (padding == std::string::npos)
        padding = value.size();
    need(value.size() - padding <= 2, "Invalid patch Base64 padding.");
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto c = value[i];
        need(i >= padding ? c == '=' : ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                        (c >= '0' && c <= '9') || c == '+' || c == '/'),
             "Invalid patch Base64 literal.");
    }
    if (value.empty())
        return "";
    DWORD bytes = 0;
    need(CryptStringToBinaryA(value.data(), DWORD(value.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &bytes, nullptr,
                              nullptr) != 0 &&
             bytes <= limit,
         "Invalid patch Base64 literal.");
    std::string out(bytes, '\0');
    need(CryptStringToBinaryA(value.data(), DWORD(value.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                              reinterpret_cast<BYTE *>(out.data()), &bytes, nullptr, nullptr) != 0,
         "Could not decode patch literal.");
    return out;
}
}
Json normalizeFilePatch(const Json &value) {
    const std::set<std::string> keys = {"action",       "path",       "sourceSha256", "sourceSize",
                                        "outputSha256", "outputSize", "operations",   "create"};
    need(value.is_object(), "Invalid compatibility patch.");
    for (auto it = value.begin(); it != value.end(); ++it)
        need(keys.count(it.key()), "Unknown compatibility patch field.");
    need(value.at("action") == "patch", "Invalid compatibility action.");
    auto path = value.at("path").get<std::string>();
    safeRelative(path);
    Json result = {{"action", "patch"},
                   {"path", path},
                   {"sourceSha256", hash(value.at("sourceSha256"))},
                   {"sourceSize", size(value.at("sourceSize"))},
                   {"outputSha256", hash(value.at("outputSha256"))},
                   {"outputSize", size(value.at("outputSize"))},
                   {"create", value.value("create", false)},
                   {"operations", Json::array()}};
    need(result.at("create").is_boolean(), "Invalid patch create flag.");
    if (result.at("create") == true)
        need(result.at("sourceSize") == 0 && result.at("sourceSha256") == textSha256(""),
             "New patch files require the empty-source fingerprint.");
    const auto &operations = value.at("operations");
    need(operations.is_array() && !operations.empty() && operations.size() <= 100000,
         "Invalid patch operation count.");
    std::uint64_t produced = 0;
    for (const auto &op : operations) {
        need(op.is_object(), "Invalid patch operation.");
        if (op.size() == 2 && op.contains("copyOffset") && op.contains("copyLength")) {
            auto offset = size(op.at("copyOffset")), length = size(op.at("copyLength")),
                 source = result.at("sourceSize").get<std::uint64_t>();
            need(offset <= source && length <= source - offset && length <= limit - produced,
                 "Patch copy exceeds source/output bounds.");
            produced += length;
            result["operations"].push_back({{"copyOffset", offset}, {"copyLength", length}});
        } else {
            need(op.size() == 1 && op.contains("data") && op.at("data").is_string(),
                 "Patch operations support only bounded copies or Base64 literals.");
            auto literal = op.at("data").get<std::string>();
            auto bytes = decode(literal);
            need(bytes.size() <= limit - produced, "Patch output exceeds limit.");
            produced += bytes.size();
            result["operations"].push_back({{"data", literal}});
        }
    }
    need(produced == result.at("outputSize").get<std::uint64_t>(),
         "Patch output size does not match its recipe.");
    return result;
}
void applyFilePatch(const fs::path &path, const Json &definition) {
    auto target = longPath(path);
    auto patch = normalizeFilePatch(definition);
    noLinks(target);
    if (fs::exists(target) && fs::is_regular_file(target) &&
        fs::file_size(target) == patch.at("outputSize") &&
        sha256(target) == patch.at("outputSha256"))
        return;
    need(fs::exists(target) || patch.at("create") == true,
         "Compatibility patch source is missing.");
    std::string source;
    if (fs::exists(target)) {
        need(fs::is_regular_file(target) && fs::file_size(target) <= limit,
             "Compatibility source exceeds limit or is not a regular file.");
        source.resize(static_cast<std::size_t>(fs::file_size(target)));
        std::ifstream input(target, std::ios::binary);
        input.read(source.data(), static_cast<std::streamsize>(source.size()));
        need(bool(input), "Could not read compatibility source bytes.");
    }
    need(source.size() == patch.at("sourceSize") && textSha256(source) == patch.at("sourceSha256"),
         "Compatibility patch refused an unknown/local-edited source.");
    std::string output;
    output.reserve(patch.at("outputSize"));
    for (const auto &op : patch.at("operations"))
        if (op.contains("data"))
            output += decode(op.at("data"));
        else
            output.append(source, op.at("copyOffset").get<std::size_t>(),
                          op.at("copyLength").get<std::size_t>());
    need(output.size() == patch.at("outputSize") && textSha256(output) == patch.at("outputSha256"),
         "Reconstructed compatibility file failed output verification.");
    writeAtomic(target, output);
}
}
