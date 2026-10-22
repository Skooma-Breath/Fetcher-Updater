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
std::string read(const fs::path &path, std::uintmax_t limit = 8 * 1024 * 1024);
Json readJson(const fs::path &path);
Json parseJson(const std::string &text);
void writeAtomic(const fs::path &path, const std::string &text);
std::string lower(std::string value);
fs::path safeRelative(const std::string &value);
void noLinks(const fs::path &path);
void removeManaged(const fs::path &root, const fs::path &child);
std::string sha256(const fs::path &path);
std::string httpGet(const std::string &url);
void download(const std::string &url, const fs::path &path, const std::string &hash, const Log &log);
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
