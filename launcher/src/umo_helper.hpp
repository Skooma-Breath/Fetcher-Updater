#pragma once
#include "native_platform.hpp"
namespace fetcher {
Json umoStatus(const fs::path &root);
void installUmo(const fs::path &root, const Log &log);
void connectUmo(const fs::path &root, const Log &log);
void forgetUmo(const fs::path &root);
void cancelUmo();
void resetUmoCancellation();
void downloadUmo(const fs::path &root, const Json &nexus, const std::string &hash,
                 const fs::path &destination, const Log &log);
void umoProtocol(const fs::path &root, bool enable);
void forwardUmoLink(const fs::path &root, const std::string &uri);
Json parseUmoLink(const std::string &uri);
} // namespace fetcher
