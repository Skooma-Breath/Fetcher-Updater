#pragma once
#include "native_platform.hpp"
namespace fetcher {
// All calls are serialized on the launcher's worker thread. UI sees snapshots
// only.
class Backend {
  public:
    explicit Backend(fs::path root, Log log = {});
    Json command(const Json &request);
    Json state() const;
    Json check(bool repair = false, bool install = false);
    static Json parseServers(const Json &value);
    static std::vector<std::string> loadOrder(const std::vector<std::string> &content,
                                              const std::vector<fs::path> &roots);
    static void installClient(const fs::path &root, const fs::path &stage, const Json &policy);
    static void installTools(const fs::path &root, const fs::path &stage);

  private:
    fs::path mRoot, mManaged;
    Json mSettings, mCatalog, mServers = Json::array(), mStatus, mReview;
    Log mLog;
    void save();
    Json profile() const;
    Json resolve(const Json &definition);
    fs::path package(const Json &definition, bool repair);
    fs::path layout(const fs::path &raw, const Json &definition, const fs::path &gameData);
    fs::path prepare();
    fs::path gameData() const;
    Json identity() const;
    void saveIdentity(const Json &request);
    void validateProfile(const Json &profile) const;
};
} // namespace fetcher
