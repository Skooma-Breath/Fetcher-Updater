#pragma once
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace fetcher {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Log = std::function<void(const std::string &)>;
std::wstring wide(const std::string &value);
std::string utf8(const std::wstring &value);
fs::path longPath(const fs::path &path);
fs::path ordinaryPath(const fs::path &path);
std::string read(const fs::path &path, std::uintmax_t limit = 8 * 1024 * 1024);
Json readJson(const fs::path &path, std::uintmax_t limit = 8 * 1024 * 1024);
Json parseJson(const std::string &text, std::uintmax_t limit = 8 * 1024 * 1024);
void writeAtomic(const fs::path &path, const std::string &text);
std::string lower(std::string value);
fs::path safeRelative(const std::string &value);
void noLinks(const fs::path &path);
void removeManaged(const fs::path &root, const fs::path &child);
std::string sha256(const fs::path &path);
void allowLocalFixtures(bool allow);
void nexusFixtureUrl(const std::string &url);
void validateDownloadUrl(const std::string &url);
std::string displayUrl(const std::string &url);
std::string textSha256(const std::string &text);
std::string protectSecret(const std::string &text);
std::string unprotectSecret(const std::string &text);
std::string httpGet(const std::string &url, std::uint64_t limit = 8 * 1024 * 1024,
                    bool allowRedirects = true);
std::string nexusGet(const std::string &path, const std::string &key);
void download(const std::string &url, const fs::path &path, const std::string &hash,
              const Log &log);
std::string detectArchiveFormat(const fs::path &archive);
std::vector<std::string> extractArchive(const fs::path &archive, const fs::path &stage,
                                        const std::string &format = "auto");
std::vector<std::string> extractZip(const fs::path &archive, const fs::path &stage);
void launch(const fs::path &exe, const std::vector<std::wstring> &args, const fs::path &cwd);
void assertGameStopped(const fs::path &root);
class InstallLock {
  public:
    explicit InstallLock(const fs::path &root);
    ~InstallLock();
    InstallLock(const InstallLock &) = delete;
    InstallLock &operator=(const InstallLock &) = delete;

  private:
    void *mHandle = nullptr;
};
std::string endpoint(const std::string &host, int port);
} // namespace fetcher
