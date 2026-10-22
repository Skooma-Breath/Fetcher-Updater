#include "native_platform.hpp"
#include <windows.h>

#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <miniz.h>
#include <regex>
#include <set>
#include <sstream>
#include <tlhelp32.h>
#include <winhttp.h>
namespace fetcher {
namespace {
struct Internet {
    HINTERNET h = nullptr;
    explicit Internet(HINTERNET v) : h(v) {
        if (!h)
            throw std::runtime_error("Windows HTTP initialization failed.");
    }
    ~Internet() { WinHttpCloseHandle(h); }
    Internet(const Internet &) = delete;
};
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class Sink> void request(const std::string &url, std::uint64_t limit, Sink sink) {
    const auto u = wide(url);
    URL_COMPONENTS c{};
    c.dwStructSize = sizeof(c);
    c.dwHostNameLength = c.dwUrlPathLength = c.dwExtraInfoLength = c.dwSchemeLength = DWORD(-1);
    c.dwUserNameLength = c.dwPasswordLength = DWORD(-1);
    require(WinHttpCrackUrl(u.c_str(), 0, 0, &c) != 0, "Invalid download URL.");
    const std::wstring host(c.lpszHostName, c.dwHostNameLength);
    require(c.nScheme == INTERNET_SCHEME_HTTPS ||
                (c.nScheme == INTERNET_SCHEME_HTTP && (host == L"127.0.0.1" || host == L"localhost")),
            "HTTPS is required except for local test servers.");
    require(c.dwUserNameLength == 0 && c.dwPasswordLength == 0, "Credentials in URLs are unsupported.");
    const std::wstring path =
        (c.dwUrlPathLength ? std::wstring(c.lpszUrlPath, c.dwUrlPathLength) : L"") +
        (c.dwExtraInfoLength ? std::wstring(c.lpszExtraInfo, c.dwExtraInfoLength) : L"");
    Internet session(WinHttpOpen(L"FetcherLauncher/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    require(WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 30000) != 0, "Could not set HTTP timeouts.");
    Internet connection(WinHttpConnect(session.h, host.c_str(), c.nPort, 0));
    Internet req(WinHttpOpenRequest(connection.h, L"GET", path.empty() ? L"/" : path.c_str(), nullptr,
                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                    c.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    require(WinHttpSendRequest(req.h, L"Accept: application/json\r\n", DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0,
                               0, 0) != 0,
            "Download connection failed.");
    require(WinHttpReceiveResponse(req.h, nullptr) != 0, "Download response failed.");
    DWORD status = 0, size = sizeof(status);
    require(WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
                                &status, &size, nullptr) != 0,
            "Missing HTTP status.");
    if (status != 200)
        throw std::runtime_error("HTTP " + std::to_string(status) + " fetching " + url);
    std::array<char, 65536> data{};
    std::uint64_t total = 0;
    DWORD got = 0;
    do {
        require(WinHttpReadData(req.h, data.data(), DWORD(data.size()), &got) != 0, "Download interrupted.");
        total += got;
        require(total <= limit, "Download exceeds the supported size limit.");
        if (got)
            sink(data.data(), got);
    } while (got);
}
std::wstring quote(const std::wstring &s) {
    std::wstring out = L"\"";
    unsigned slash = 0;
    for (auto c : s) {
        if (c == L'\\') {
            ++slash;
            continue;
        }
        if (c == L'\"')
            out.append(slash * 2 + 1, L'\\');
        else
            out.append(slash, L'\\');
        slash = 0;
        out += c;
    }
    out.append(slash * 2, L'\\');
    return out + L'\"';
}
} // namespace
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    require(n > 0, "Invalid UTF-8 text.");
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string utf8(const std::wstring &w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    require(n > 0, "Invalid text.");
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::string lower(std::string s) {
    if (s.empty())
        return s;
    const auto w = wide(s);
    const int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.data(), int(w.size()), nullptr, 0,
                                nullptr, nullptr, 0);
    require(n > 0, "Filename case mapping failed.");
    std::wstring mapped(n, 0);
    require(LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.data(), int(w.size()), mapped.data(), n,
                          nullptr, nullptr, 0) > 0,
            "Filename case mapping failed.");
    return utf8(mapped);
}
std::string read(const fs::path &p, std::uintmax_t limit) {
    require(fs::is_regular_file(p), "Required file is missing.");
    require(fs::file_size(p) <= limit, "Metadata file is too large.");
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    require(bool(f), "File read failed.");
    auto text = s.str();
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0)
        text.erase(0, 3);
    return text;
}
Json parseJson(const std::string &text) {
    require(text.size() <= 8 * 1024 * 1024, "JSON metadata exceeds size limit.");
    return Json::parse(text, [](int depth, Json::parse_event_t, Json &) {
        require(depth <= 64, "JSON nesting exceeds limit.");
        return true;
    });
}
Json readJson(const fs::path &p) { return parseJson(read(p)); }
void noLinks(const fs::path &p) {
    auto full = fs::absolute(p).lexically_normal();
    fs::path at;
    for (const auto &part : full) {
        at /= part;
        DWORD flags = GetFileAttributesW(at.c_str());
        if (flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Managed paths cannot contain symlinks/junctions: " +
                                     utf8(at.wstring()));
    }
}
fs::path safeRelative(const std::string &value) {
    require(!value.empty() && value.size() < 1024, "Invalid archive path length.");
    require(value.front() != '/' && value.find('\\') == std::string::npos &&
                value.find(':') == std::string::npos,
            "Absolute/alternate archive paths are forbidden.");
    std::istringstream in(value);
    std::string part;
    while (std::getline(in, part, '/')) {
        require(!part.empty() && part != "." && part != ".." && part.back() != '.' && part.back() != ' ',
                "Unsafe archive path segment.");
        for (unsigned char c : part)
            require(c >= 32 && c != 127 && std::string("<>\"|?*").find(char(c)) == std::string::npos,
                    "Unsafe filename.");
        require(!std::regex_match(
                    part, std::regex("(con|prn|aux|nul|com[1-9]|lpt[1-9])(\\..*)?", std::regex::icase)),
                "Reserved Windows filename.");
    }
    return fs::path(wide(value));
}
void writeAtomic(const fs::path &p, const std::string &text) {
    noLinks(p);
    if (!p.parent_path().empty())
        fs::create_directories(p.parent_path());
    auto tmp = p;
    tmp += L".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(text.data(), text.size());
        require(bool(f), "File write failed.");
    }
    require(MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
            "Atomic metadata replacement failed.");
}
void removeManaged(const fs::path &root, const fs::path &child) {
    noLinks(root);
    noLinks(child);
    auto r = fs::absolute(root).lexically_normal(), c = fs::absolute(child).lexically_normal();
    auto rel = c.lexically_relative(r);
    require(!rel.empty() && rel != L"." && *rel.begin() != L"..",
            "Refusing deletion outside managed staging.");
    fs::remove_all(c);
}
std::string sha256(const fs::path &p) {
    BCRYPT_ALG_HANDLE a = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    require(BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
            "SHA-256 provider failed.");
    struct Cleanup {
        BCRYPT_ALG_HANDLE &a;
        BCRYPT_HASH_HANDLE &h;
        ~Cleanup() {
            if (h)
                BCryptDestroyHash(h);
            if (a)
                BCryptCloseAlgorithmProvider(a, 0);
        }
    } cleanup{a, h};
    require(BCryptCreateHash(a, &h, nullptr, 0, nullptr, 0, 0) >= 0, "SHA-256 initialization failed.");
    std::ifstream f(p, std::ios::binary);
    require(bool(f), "Hash input missing.");
    std::array<char, 65536> b{};
    while (f) {
        f.read(b.data(), b.size());
        require(BCryptHashData(h, reinterpret_cast<PUCHAR>(b.data()), ULONG(f.gcount()), 0) >= 0,
                "SHA-256 update failed.");
    }
    require(f.eof(), "Hash read failed.");
    std::array<unsigned char, 32> digest{};
    require(BCryptFinishHash(h, digest.data(), ULONG(digest.size()), 0) >= 0, "SHA-256 failed.");
    std::ostringstream out;
    for (auto n : digest)
        out << std::hex << std::setfill('0') << std::setw(2) << int(n);
    return out.str();
}
std::string httpGet(const std::string &url) {
    std::string text;
    request(url, 8 * 1024 * 1024, [&](const char *p, DWORD n) { text.append(p, n); });
    return text;
}
void download(const std::string &url, const fs::path &p, const std::string &hash, const Log &log) {
    require(std::regex_match(hash, std::regex("[0-9a-f]{64}")), "Missing or invalid SHA-256 digest.");
    noLinks(p);
    fs::create_directories(p.parent_path());
    if (fs::exists(p) && sha256(p) == hash) {
        if (log)
            log("Using verified download cache.\n");
        return;
    }
    auto temp = p;
    temp += L".partial";
    if (log)
        log("Downloading " + url + "\n");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        require(bool(out), "Cannot create download.");
        request(url, 4ull * 1024 * 1024 * 1024, [&](const char *b, DWORD n) {
            out.write(b, n);
            require(bool(out), "Download write failed.");
        });
    }
    if (sha256(temp) != hash) {
        fs::remove(temp);
        throw std::runtime_error("Downloaded archive SHA-256 mismatch.");
    }
    require(MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
            "Could not commit verified archive.");
}
std::vector<std::string> extractZip(const fs::path &p, const fs::path &stage) {
    noLinks(stage);
    require(!fs::exists(stage), "Extraction stage must be new.");
    fs::create_directories(stage);
    mz_zip_archive z{};
    FILE *file = nullptr;
    require(_wfopen_s(&file, p.c_str(), L"rb") == 0, "Cannot open ZIP.");
    struct Cleanup {
        mz_zip_archive &z;
        FILE *f;
        ~Cleanup() {
            mz_zip_reader_end(&z);
            fclose(f);
        }
    } cleanup{z, file};
    require(mz_zip_reader_init_cfile(&z, file, 0, 0) != 0, "Invalid ZIP archive.");
    require(mz_zip_reader_get_num_files(&z) <= 100000, "Too many ZIP entries.");
    std::set<std::string> seen;
    std::vector<std::string> files;
    std::uint64_t total = 0;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); ++i) {
        mz_zip_archive_file_stat s{};
        require(mz_zip_reader_file_stat(&z, i, &s) != 0, "Invalid ZIP entry.");
        require(mz_zip_reader_get_filename(&z, i, nullptr, 0) <= sizeof(s.m_filename),
                "ZIP filename exceeds the supported length.");
        std::string name = s.m_filename;
        if (s.m_is_directory && !name.empty() && name.back() == '/')
            name.pop_back();
        auto rel = safeRelative(name);
        require(seen.insert(lower(name)).second, "Duplicate ZIP path.");
        require(((s.m_external_attr >> 16) & 0170000) != 0120000, "ZIP symlinks are forbidden.");
        total += s.m_uncomp_size;
        require(total <= 8ull * 1024 * 1024 * 1024 && s.m_uncomp_size <= 2ull * 1024 * 1024 * 1024,
                "ZIP expansion exceeds limit.");
        if (s.m_is_directory) {
            fs::create_directories(stage / rel);
            continue;
        }
        fs::create_directories((stage / rel).parent_path());
        FILE *out = nullptr;
        require(_wfopen_s(&out, (stage / rel).c_str(), L"wb") == 0, "Cannot create extracted file.");
        bool ok = mz_zip_reader_extract_to_cfile(&z, i, out, 0) != 0;
        fclose(out);
        require(ok, "ZIP CRC/decompression failed.");
        files.push_back(name);
    }
    return files;
}
void launch(const fs::path &exe, const std::vector<std::wstring> &args, const fs::path &cwd) {
    require(fs::is_regular_file(exe), "Executable is missing.");
    std::wstring cmd = quote(exe.wstring());
    for (const auto &arg : args)
        cmd += L" " + quote(arg);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    require(CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si,
                           &pi) != 0,
            "Windows could not launch the game.");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}
void assertGameStopped(const fs::path &root) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    require(snap != INVALID_HANDLE_VALUE, "Cannot inspect game processes.");
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    bool running = false;
    if (Process32FirstW(snap, &e))
        do {
            if (_wcsicmp(e.szExeFile, L"openmw.exe") == 0) {
                HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID);
                if (!p) {
                    CloseHandle(snap);
                    throw std::runtime_error("Cannot verify a running game process; "
                                             "close the game before updating.");
                }
                wchar_t path[32768];
                DWORD n = 32768;
                if (!QueryFullProcessImageNameW(p, 0, path, &n)) {
                    CloseHandle(p);
                    CloseHandle(snap);
                    throw std::runtime_error("Cannot verify running client path.");
                }
                running = lower(utf8(fs::path(path).parent_path().wstring())) ==
                          lower(utf8(fs::absolute(root).wstring()));
                CloseHandle(p);
                if (running)
                    break;
            }
        } while (Process32NextW(snap, &e));
    CloseHandle(snap);
    require(!running, "Close this installation's game before updating.");
}
std::string endpoint(const std::string &host, int port) {
    require(port > 0 && port <= 65535 && std::regex_match(host, std::regex("[A-Za-z0-9._:-]{1,253}")),
            "Invalid server address.");
    return (host.find(':') != std::string::npos ? "[" + host + "]" : host) + ":" + std::to_string(port);
}
InstallLock::InstallLock(const fs::path &root) {
    auto path = lower(utf8(fs::absolute(root).lexically_normal().wstring()));
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : path) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    auto name = L"Local\\FetcherLauncher-" + std::to_wstring(hash);
    mHandle = CreateMutexW(nullptr, FALSE, name.c_str());
    require(mHandle != nullptr, "Cannot create update lock.");
    auto status = WaitForSingleObject(mHandle, 0);
    if (status != WAIT_OBJECT_0 && status != WAIT_ABANDONED) {
        CloseHandle(mHandle);
        mHandle = nullptr;
        throw std::runtime_error("Another launcher is preparing/updating this installation.");
    }
}
InstallLock::~InstallLock() {
    if (mHandle) {
        ReleaseMutex(mHandle);
        CloseHandle(mHandle);
    }
}
} // namespace fetcher
