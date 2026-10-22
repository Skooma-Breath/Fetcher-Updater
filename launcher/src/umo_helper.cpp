#include <winsock2.h>
#include <ws2tcpip.h>
#include "umo_helper.hpp"
#include "umo_embedded.hpp"
#include <algorithm>
#include <chrono>
#include <map>
#include <regex>
#include <set>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>

namespace fetcher {
namespace {
constexpr char version[] = "0.11.2";
constexpr char binaryHash[] = "18dcd4a33171b6ca5b8afd8286e4917906f4a108df52f60cb2e718914ec057e4";
constexpr char sourceHash[] = "7420931c2748b55f2425867764e116487ef1d3b2f3b632f59ca3042ccd87f30f";
constexpr char binaryUrl[] =
    "https://gitlab.com/modding-openmw/umo/-/package_files/366287976/download";
constexpr char sourceUrl[] = "https://gitlab.com/api/v4/projects/59048515/repository/"
                             "archive.zip?sha=2805e59ca4b627768fdd8e409a3d70a920225afc";
constexpr wchar_t protocolKey[] = L"Software\\Classes\\nxm";
constexpr wchar_t commandKey[] = L"Software\\Classes\\nxm\\shell\\open\\command";
void need(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Handle {
    HANDLE h = nullptr;
    ~Handle() {
        if (h && h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};
Handle &cancelEvent() {
    static Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    need(event.h != nullptr, "Cannot supervise UMO cancellation.");
    return event;
}
struct Key {
    HKEY h = nullptr;
    ~Key() {
        if (h)
            RegCloseKey(h);
    }
};
fs::path statePath(const fs::path &root) { return longPath(root / "_fetcher_launcher" / "umo"); }
fs::path toolPath(const fs::path &root) {
    return statePath(root) / (std::string("tool-") + version);
}
std::uint64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
std::wstring quote(const std::wstring &s) {
    std::wstring out = L"\"";
    unsigned slash = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++slash;
            continue;
        }
        out.append(c == L'"' ? slash * 2 + 1 : slash, L'\\');
        slash = 0;
        out += c;
    }
    out.append(slash * 2, L'\\');
    return out + L'"';
}
Json registryValue(const wchar_t *path, const wchar_t *name) {
    Key key;
    auto status = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &key.h);
    if (status == ERROR_FILE_NOT_FOUND)
        return nullptr;
    need(status == ERROR_SUCCESS, "Cannot inspect the existing NXM handler.");
    DWORD type = 0, size = 0;
    status = RegQueryValueExW(key.h, name, nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND)
        return nullptr;
    need(status == ERROR_SUCCESS && size <= 32768, "Cannot read the existing NXM handler.");
    std::vector<unsigned char> data(size);
    need(RegQueryValueExW(key.h, name, nullptr, &type, data.data(), &size) == ERROR_SUCCESS,
         "Cannot read the existing NXM handler.");
    data.resize(size);
    return {{"type", type}, {"data", data}};
}
void registrySet(const wchar_t *path, const wchar_t *name, const Json &value) {
    Key key;
    need(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key.h,
                         nullptr) == ERROR_SUCCESS,
         "Cannot configure the NXM handler.");
    if (value.is_null()) {
        auto status = RegDeleteValueW(key.h, name);
        need(status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND,
             "Cannot restore the previous NXM handler.");
    } else {
        auto data = value.at("data").get<std::vector<unsigned char>>();
        need(data.size() <= 32768 &&
                 RegSetValueExW(key.h, name, 0, value.at("type").get<DWORD>(), data.data(),
                                DWORD(data.size())) == ERROR_SUCCESS,
             "Cannot write the NXM handler.");
    }
}
Json registryString(const std::wstring &value) {
    const auto *bytes = reinterpret_cast<const unsigned char *>(value.c_str());
    return {
        {"type", REG_SZ},
        {"data", std::vector<unsigned char>(bytes, bytes + (value.size() + 1) * sizeof(wchar_t))}};
}
std::wstring handlerCommand(const fs::path &root) {
    auto path = ordinaryPath(longPath(root));
    return quote((path / "FetcherLauncher.exe").wstring()) + L" --install-root " +
           quote(path.wstring()) + L" --umo-nxm \"%1\"";
}
bool handlerOwned(const fs::path &root) {
    return registryValue(commandKey, nullptr) == registryString(handlerCommand(root));
}
bool installed(const fs::path &root) {
    auto tool = toolPath(root);
    return fs::is_regular_file(tool / "receipt.json") &&
           fs::is_regular_file(tool / "upstream" / "umo.exe") &&
           fs::is_regular_file(tool / "upstream" / "umo" / "python.exe");
}
void verifyTool(const fs::path &root) {
    need(installed(root), "Install the UMO helper in Mods before connecting Nexus.");
    auto tool = toolPath(root);
    noLinks(tool);
    auto receipt = readJson(tool / "receipt.json", 4 * 1024 * 1024);
    need(receipt.at("binarySha256") == binaryHash && receipt.at("sourceSha256") == sourceHash &&
             receipt.at("version") == version,
         "UMO helper receipt is invalid; reinstall the helper.");
    std::set<std::string> required{"umo.exe",      "umo/python.exe",  "umo/config.py",
                                   "umo/sso.py",   "umo/handlers.py", "umo/dl.py",
                                   "umo/umonxm.py"};
    for (const auto &entry : receipt.at("files")) {
        auto name = entry.at("path").get<std::string>();
        auto file = tool / "upstream" / safeRelative(name);
        noLinks(file);
        need(fs::is_regular_file(file) && sha256(file) == entry.at("sha256"),
             "UMO helper changed or is incomplete; reinstall the helper.");
        required.erase(lower(name));
    }
    need(required.empty() && sha256(tool / "adapter.py") == textSha256(umoAdapterSource) &&
             sha256(tool / "LICENSE") == textSha256(umoLicenseSource) &&
             sha256(tool / "upstream-source.zip") == sourceHash,
         "UMO helper/source files changed; reinstall the helper.");
}
std::vector<wchar_t> environment(const fs::path &state) {
    // Keep injected Python/UMO variables and other installations' credentials out of this process.
    std::map<std::wstring, std::wstring> variables;
    for (auto name : {L"SystemRoot", L"WINDIR", L"USERPROFILE", L"LOCALAPPDATA", L"APPDATA",
                      L"TEMP", L"TMP", L"HOMEDRIVE", L"HOMEPATH", L"PROGRAMDATA"}) {
        auto size = GetEnvironmentVariableW(name, nullptr, 0);
        if (!size)
            continue;
        std::wstring value(size, 0);
        GetEnvironmentVariableW(name, value.data(), size);
        value.resize(size - 1);
        variables[name] = value;
    }
    variables[L"PATH"] = variables[L"SystemRoot"] + L"\\System32;" + variables[L"SystemRoot"];
    variables[L"UMO_CONF_DIR"] = ordinaryPath(state / "config").wstring();
    variables[L"UMO_CONF_FILE"] = ordinaryPath(state / "config" / "config.json").wstring();
    variables[L"UMO_CACHE_DIR"] = ordinaryPath(state / "cache").wstring();
    variables[L"UMO_NO_NOTIFICATIONS"] = L"1";
    variables[L"PYTHONUTF8"] = L"1";
    variables[L"PYTHONDONTWRITEBYTECODE"] = L"1";
    std::vector<wchar_t> block;
    for (const auto &[name, value] : variables) {
        auto line = name + L"=" + value;
        block.insert(block.end(), line.begin(), line.end());
        block.push_back(0);
    }
    block.push_back(0);
    return block;
}
Json runAdapter(const fs::path &root, const std::string &operation, const fs::path &request = {}) {
    verifyTool(root);
    auto state = statePath(root), tool = toolPath(root);
    noLinks(state / "result.json");
    fs::remove(state / "result.json");
    auto exe = ordinaryPath(tool / "upstream" / "umo" / "python.exe");
    auto cmd = quote(exe.wstring()) + L" -I -B " +
               quote(ordinaryPath(tool / "adapter.py").wstring()) + L" " + wide(operation) +
               L" --state " + quote(ordinaryPath(state).wstring());
    if (!request.empty())
        cmd += L" --request " + quote(ordinaryPath(request).wstring());
    auto env = environment(state);
    Handle job{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    need(job.h && SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits,
                                          sizeof(limits)),
         "Cannot supervise the UMO helper.");
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const bool interactive = operation == "connect";
    need(CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED |
                            (interactive ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW),
                        env.data(), ordinaryPath(state).c_str(), &si, &pi),
         "Windows could not start the UMO helper.");
    Handle process{pi.hProcess}, thread{pi.hThread};
    if (!AssignProcessToJobObject(job.h, process.h)) {
        TerminateProcess(process.h, 1);
        throw std::runtime_error("Cannot supervise the UMO helper process.");
    }
    need(ResumeThread(thread.h) != DWORD(-1), "Cannot resume the UMO helper.");
    HANDLE handles[] = {process.h, cancelEvent().h};
    auto waited = WaitForMultipleObjects(2, handles, FALSE, 20 * 60 * 1000);
    if (waited == WAIT_OBJECT_0 + 1)
        throw std::runtime_error(
            "UMO operation cancelled. You can retry or import the exact archive.");
    need(waited == WAIT_OBJECT_0,
         "UMO timed out. Retry or download/import the exact file manually.");
    DWORD code = 1;
    GetExitCodeProcess(process.h, &code);
    Json result = fs::is_regular_file(state / "result.json") ? readJson(state / "result.json", 4096)
                                                             : Json::object();
    if (code != 0 || !result.value("ok", false)) {
        auto error = result.value("code", "failed");
        if (error == "auth" || error == "credential")
            throw std::runtime_error(
                "Connect Nexus through UMO first (or reconnect your account).");
        if (error == "browser-links")
            throw std::runtime_error(
                "Free Nexus downloads need the opt-in browser link handler in Mods, "
                "or download/import the exact file manually.");
        if (error == "hash")
            throw std::runtime_error("UMO archive does not match the server's pinned SHA-256.");
        throw std::runtime_error("UMO did not finish. Retry, reconnect Nexus, or download/import "
                                 "the exact file manually.");
    }
    return result;
}
} // namespace
Json umoStatus(const fs::path &root) {
    auto state = statePath(root);
    return {{"version", version},
            {"installed", installed(root)},
            {"connected", fs::is_regular_file(state / "credential.bin")},
            {"browserLinks", handlerOwned(root)},
            {"sourceUrl", sourceUrl}};
}
void installUmo(const fs::path &root, const Log &log) {
    auto state = statePath(root), tool = toolPath(root);
    noLinks(state);
    fs::create_directories(state);
    auto binary = state / "umo-win.zip", source = state / "upstream-source.zip";
    download(binaryUrl, binary, binaryHash, log);
    download(sourceUrl, source, sourceHash, log);
    auto stage = state / ("stage-" + std::to_string(now()));
    need(!fs::exists(stage), "UMO staging directory already exists; retry later.");
    try {
        auto names = extractZip(binary, stage / "upstream");
        Json files = Json::array();
        for (const auto &name : names)
            files.push_back(
                {{"path", name}, {"sha256", sha256(stage / "upstream" / safeRelative(name))}});
        writeAtomic(stage / "adapter.py", umoAdapterSource);
        writeAtomic(stage / "LICENSE", umoLicenseSource);
        fs::copy_file(source, stage / "upstream-source.zip");
        writeAtomic(stage / "SOURCE.txt",
                    "UMO 0.11.2, upstream distribution unchanged. AGPL-3.0-or-later.\n"
                    "Upstream: https://gitlab.com/modding-openmw/umo\n"
                    "Commit: 2805e59ca4b627768fdd8e409a3d70a920225afc\n"
                    "Complete corresponding UMO source: upstream-source.zip\n"
                    "Fetcher adapter source: adapter.py (AGPL-3.0-or-later), license: LICENSE\n"
                    "UMO dependency notices are retained in upstream/umo/*dist-info/.\n");
        writeAtomic(stage / "receipt.json", Json{{"version", version},
                                                 {"binarySha256", binaryHash},
                                                 {"sourceSha256", sourceHash},
                                                 {"files", files}}
                                                .dump(2));
        // Keep the working copy until the entire replacement is prepared.
        auto previous = state / "previous-tool";
        if (fs::exists(previous))
            removeManaged(state, previous);
        if (fs::exists(tool))
            fs::rename(tool, previous);
        try {
            fs::rename(stage, tool);
        } catch (...) {
            if (fs::exists(previous))
                fs::rename(previous, tool);
            throw;
        }
        try {
            auto probe = runAdapter(root, "probe");
            need(probe.value("version", "") == version, "Unexpected UMO runtime version.");
        } catch (...) {
            removeManaged(state, tool);
            if (fs::exists(previous))
                fs::rename(previous, tool);
            throw;
        }
        if (fs::exists(previous))
            removeManaged(state, previous);
        if (log)
            log("Verified UMO helper installed with license and complete source.\n");
    } catch (...) {
        if (fs::exists(stage))
            removeManaged(state, stage);
        throw;
    }
}
void connectUmo(const fs::path &root, const Log &log) {
    if (log)
        log("Opening UMO sign-in. Authorise UMO in the Nexus browser page; its console can be "
            "closed to cancel.\n");
    runAdapter(root, "connect");
}
void cancelUmo() { SetEvent(cancelEvent().h); }
void resetUmoCancellation() { ResetEvent(cancelEvent().h); }
void forgetUmo(const fs::path &root) {
    for (const auto &name : {"credential.bin", "account.json", "credential.tmp"}) {
        auto file = statePath(root) / name;
        noLinks(file);
        fs::remove(file);
    }
}
void downloadUmo(const fs::path &root, const Json &nexus, const std::string &hash,
                 const fs::path &destination, const Log &log) {
    need(std::regex_match(hash, std::regex("[0-9a-f]{64}")), "Invalid Nexus archive hash.");
    auto request = nexus;
    request["sha256"] = hash;
    request["browserLinks"] = handlerOwned(root);
    auto state = statePath(root);
    noLinks(state);
    fs::create_directories(state);
    auto file = state / "request.json";
    writeAtomic(file, request.dump());
    if (log)
        log("UMO is fetching the pinned Nexus archive. Free accounts approve each browser "
            "download.\n");
    try {
        runAdapter(root, "download", file);
    } catch (...) {
        fs::remove(state / "pending.json");
        throw;
    }
    auto archive =
        state / "cache" / "archive-cache" / ("Fetcher" + hash) / ("Archive_" + hash + ".archive");
    noLinks(archive);
    need(fs::is_regular_file(archive) && fs::file_size(archive) <= 4ull * 1024 * 1024 * 1024 && sha256(archive) == hash,
         "UMO archive does not match the server's pinned SHA-256.");
    noLinks(destination);
    fs::create_directories(destination.parent_path());
    auto temporary = destination;
    temporary += L".umo-import";
    noLinks(temporary);
    fs::copy_file(archive, temporary, fs::copy_options::overwrite_existing);
    need(sha256(temporary) == hash, "UMO archive changed during import.");
    need(MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
         "Could not import the verified UMO archive.");
}
void umoProtocol(const fs::path &root, bool enable) {
    auto backup = statePath(root) / "nxm-backup.json";
    noLinks(backup);
    if (enable) {
        verifyTool(root);
        need(fs::is_regular_file(longPath(root / "FetcherLauncher.exe")),
             "Install this launcher in its destination before enabling browser links.");
        if (handlerOwned(root)) {
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            return;
        }
        // Only these three values are changed. Never replace unrelated registry keys or MOMW links.
        writeAtomic(backup, Json{{"description", registryValue(protocolKey, nullptr)},
                                 {"protocol", registryValue(protocolKey, L"URL Protocol")},
                                 {"command", registryValue(commandKey, nullptr)}}
                                .dump(2));
        auto prior = readJson(backup, 256 * 1024);
        try {
            registrySet(commandKey, nullptr, registryString(handlerCommand(root)));
            registrySet(protocolKey, nullptr, registryString(L"URL:Fetcher UMO Nexus download"));
            registrySet(protocolKey, L"URL Protocol", registryString(L""));
        } catch (...) {
            // Retain the saved values if a permission/storage error also prevents recovery.
            try {
                registrySet(protocolKey, nullptr, prior.at("description"));
                registrySet(protocolKey, L"URL Protocol", prior.at("protocol"));
                registrySet(commandKey, nullptr, prior.at("command"));
                fs::remove(backup);
            } catch (...) {
            }
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            throw;
        }
    } else {
        if (!fs::is_regular_file(backup))
            return;
        need(handlerOwned(root),
             "Another application now handles NXM links; its setting was kept.");
        auto prior = readJson(backup, 256 * 1024);
        registrySet(protocolKey, nullptr, prior.at("description"));
        registrySet(protocolKey, L"URL Protocol", prior.at("protocol"));
        registrySet(commandKey, nullptr, prior.at("command"));
        fs::remove(backup);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
Json parseUmoLink(const std::string &uri) {
    need(uri.size() <= 4096, "Invalid Nexus browser link.");
    std::smatch match;
    need(
        std::regex_match(
            uri, match,
            std::regex(
                R"(^nxm://([a-z0-9-]{1,64})/mods/([1-9][0-9]{0,19})/files/([1-9][0-9]{0,19})\?(.+)$)")),
        "Invalid Nexus browser link.");
    Json query = Json::object();
    std::string tail = match[4];
    for (std::size_t start = 0; start < tail.size();) {
        auto end = tail.find('&', start), equal = tail.find('=', start);
        if (end == std::string::npos)
            end = tail.size();
        need(equal != std::string::npos && equal < end, "Invalid Nexus browser link.");
        auto name = tail.substr(start, equal - start),
             value = tail.substr(equal + 1, end - equal - 1);
        need((name == "key" || name == "expires" || name == "user_id") && !query.contains(name) &&
                 std::regex_match(value, std::regex("[A-Za-z0-9._~%+=-]{1,1024}")),
             "Invalid Nexus browser link.");
        query[name] = value;
        start = end + 1;
    }
    need(query.contains("key") && query.contains("expires") && query.contains("user_id") &&
             std::regex_match(query.at("expires").get<std::string>(), std::regex("[0-9]{1,12}")) &&
             std::regex_match(query.at("user_id").get<std::string>(), std::regex("[0-9]{1,20}")),
         "Invalid Nexus browser link.");
    return {{"game", match[1].str()},
            {"modId", std::stoull(match[2].str())},
            {"fileId", std::stoull(match[3].str())},
            {"query", query}};
}
void forwardUmoLink(const fs::path &root, const std::string &uri) {
    auto link = parseUmoLink(uri);
    auto pendingPath = statePath(root) / "pending.json";
    noLinks(pendingPath);
    auto pending = readJson(pendingPath, 4096);
    for (auto key : {"game", "modId", "fileId"})
        need(link.at(key) == pending.at(key),
             "This browser link is not for the pending Nexus file.");
    need(pending.at("expiresAt").get<std::uint64_t>() >= now() &&
             pending.at("expiresAt").get<std::uint64_t>() <= now() + 1200 &&
             std::stoull(link.at("query").at("expires").get<std::string>()) > now(),
         "Nexus browser link expired; retry the download.");
    auto port = pending.at("port").get<int>();
    need(port > 0 && port <= 65535, "Invalid UMO callback port.");
    WSADATA data{};
    need(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Cannot open the UMO callback.");
    struct Socket {
        SOCKET h = INVALID_SOCKET;
        ~Socket() {
            if (h != INVALID_SOCKET)
                closesocket(h);
            WSACleanup();
        }
    } sock{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    need(sock.h != INVALID_SOCKET, "Cannot open the UMO callback.");
    DWORD timeout = 3000;
    setsockopt(sock.h, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout),
               sizeof(timeout));
    setsockopt(sock.h, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout),
               sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));
    need(connect(sock.h, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0,
         "UMO is no longer waiting for this download.");
    // Upstream expects this query order. Do not log or persist callback access tokens.
    auto q = link.at("query");
    auto canonical = "nxm://" + link.at("game").get<std::string>() + "/mods/" +
                     std::to_string(link.at("modId").get<std::uint64_t>()) + "/files/" +
                     std::to_string(link.at("fileId").get<std::uint64_t>()) +
                     "?key=" + q.at("key").get<std::string>() +
                     "&expires=" + q.at("expires").get<std::string>() +
                     "&user_id=" + q.at("user_id").get<std::string>();
    std::size_t sent = 0;
    while (sent < canonical.size()) {
        int size = send(sock.h, canonical.data() + sent, int(canonical.size() - sent), 0);
        need(size > 0, "Cannot forward the Nexus browser link.");
        sent += size;
    }
    shutdown(sock.h, SD_SEND);
    char buffer[64];
    std::string reply;
    for (int n; (n = recv(sock.h, buffer, sizeof(buffer), 0)) > 0 && reply.size() < 1024;)
        reply.append(buffer, n);
    need(reply.find("OK") != std::string::npos, "UMO rejected this browser link.");
}
} // namespace fetcher
