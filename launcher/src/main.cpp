#include "webview/webview.h"
#include "resource.h"
#include "native_backend.hpp"
#include "install_transaction.hpp"
#include "launcher_runtime.hpp"
#include "umo_helper.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
class WinHandle {
  public:
    WinHandle() = default;
    explicit WinHandle(HANDLE handle) : mHandle(handle) {}
    ~WinHandle() { reset(); }

    WinHandle(const WinHandle &) = delete;
    WinHandle &operator=(const WinHandle &) = delete;

    WinHandle(WinHandle &&other) noexcept : mHandle(other.release()) {}
    WinHandle &operator=(WinHandle &&other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    HANDLE get() const { return mHandle; }

    HANDLE release() {
        HANDLE value = mHandle;
        mHandle = nullptr;
        return value;
    }

    void reset(HANDLE handle = nullptr) {
        if (mHandle != nullptr && mHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(mHandle);
        }
        mHandle = handle;
    }

    explicit operator bool() const { return mHandle != nullptr && mHandle != INVALID_HANDLE_VALUE; }

  private:
    HANDLE mHandle = nullptr;
};

std::string WideToUtf8(const std::wstring &value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string &value) {
    if (value.empty()) {
        return {};
    }

    const int size =
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string BytesToUtf8(const char *data, std::size_t size) {
    if (size == 0) {
        return {};
    }

    int wideSize =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(size), nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (wideSize <= 0) {
        codePage = CP_OEMCP;
        flags = 0;
        wideSize = MultiByteToWideChar(codePage, flags, data, static_cast<int>(size), nullptr, 0);
    }
    if (wideSize <= 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(wideSize), L'\0');
    MultiByteToWideChar(codePage, flags, data, static_cast<int>(size), wide.data(), wideSize);
    return WideToUtf8(wide);
}

std::string ReadTextFile(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::string JsonEscape(const std::string &value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() + 16);

    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (ch < 0x20) {
                result += "\\u00";
                result += hex[(ch >> 4) & 0x0f];
                result += hex[ch & 0x0f];
            } else {
                result.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return result;
}

std::string JsonString(const std::string &value) { return "\"" + JsonEscape(value) + "\""; }

std::string StripAnsi(const std::string &value) {
    static const std::regex ansiPattern("\\x1b\\[[0-9;?]*[ -/]*[@-~]");
    return std::regex_replace(value, ansiPattern, "");
}

std::string ExtractLastJsonObject(const std::string &value) {
    std::size_t end = value.find_last_not_of(" \t\r\n");
    while (end != std::string::npos) {
        const std::size_t lineStart = value.rfind('\n', end);
        const std::size_t start = lineStart == std::string::npos ? 0 : lineStart + 1;
        std::string line = value.substr(start, end - start + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] == '{' && line.back() == '}') {
            return line.substr(first);
        }
        if (lineStart == std::string::npos) {
            break;
        }
        end = value.find_last_not_of(" \t\r\n", lineStart);
    }
    return {};
}

std::string ExtractJsonString(const std::string &json, const std::string &key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (std::regex_search(json, match, pattern) && match.size() > 1) {
        return match[1].str();
    }
    return {};
}

fs::path GetExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return fs::path(std::wstring(buffer.data(), length));
}

void SetLauncherWindowIcon(webview::webview &window) {
    const auto nativeWindow = window.window();
    if (!nativeWindow.ok()) {
        return;
    }

    const HWND handle = static_cast<HWND>(nativeWindow.value());
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (handle == nullptr || instance == nullptr) {
        return;
    }

    const auto loadIcon = [instance](int width, int height) {
        return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_FETCHER_LAUNCHER), IMAGE_ICON,
                                             width, height, LR_DEFAULTCOLOR | LR_SHARED));
    };

    if (const HICON largeIcon = loadIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))) {
        SendMessageW(handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
    }
    if (const HICON smallIcon = loadIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))) {
        SendMessageW(handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }
}

std::wstring QuoteArgument(const std::wstring &value) {
    std::wstring result = L"\"";
    std::size_t backslashes = 0;

    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }

        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }

    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

struct UpdaterProcessResult {
    DWORD exitCode = 1;
    std::string errorMessage;
};
UpdaterProcessResult RunNativeUpdater(const fs::path &root, bool quick, bool status,
                                      const std::function<void(const std::string &)> &log) {
    try {
        fetcher::Backend backend(root, log);
        const auto result = backend.check(!quick, !status);
        log(result.dump() + "\n");
        return {0, {}};
    } catch (const std::exception &e) {
        log(std::string("Native updater: ") + e.what() + "\n");
        return {1, e.what()};
    }
}

struct Options {
    fs::path installRoot;
    fs::path logFile;
    fs::path requestFile;
    bool selfTest = false;
    bool uiSmokeTest = false;
    bool runUpdater = false;
    bool quickCheck = false;
    bool statusOnly = false;
    bool staged = false;
    bool debug = false;
    bool allowLocalFixtures = false;
    std::string nexusFixture;
    std::string umoNxm;
    std::wstring restartReadyEvent;
};

Options ParseOptions() {
    Options options;
    options.installRoot = GetExecutablePath().parent_path();

    int argumentCount = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        return options;
    }

    for (int index = 1; index < argumentCount; ++index) {
        const std::wstring argument = arguments[index];
        if (argument == L"--install-root" && index + 1 < argumentCount) {
            options.installRoot = fs::path(arguments[++index]);
        } else if (argument == L"--log-file" && index + 1 < argumentCount) {
            options.logFile = fs::path(arguments[++index]);
        } else if (argument == L"--request-file" && index + 1 < argumentCount) {
            options.requestFile = arguments[++index];
        } else if (argument == L"--self-test") {
            options.selfTest = true;
        } else if (argument == L"--smoke-test-ui") {
            options.uiSmokeTest = true;
        } else if (argument == L"--run-updater") {
            options.runUpdater = true;
        } else if (argument == L"--quick-check") {
            options.quickCheck = true;
        } else if (argument == L"--status-only") {
            options.statusOnly = true;
        } else if (argument == L"--staged") {
            options.staged = true;
        } else if (argument == L"--allow-local-fixtures") {
            options.allowLocalFixtures = true;
        } else if (argument == L"--nexus-fixture-url" && index + 1 < argumentCount) {
            options.nexusFixture = WideToUtf8(arguments[++index]);
        } else if (argument == L"--umo-nxm" && index + 1 < argumentCount) {
            options.umoNxm = WideToUtf8(arguments[++index]);
        } else if (argument == L"--debug") {
            options.debug = true;
        } else if (argument == L"--restart-ready-event" && index + 1 < argumentCount) {
            std::wstring name = arguments[++index];
            if (name.size() < 128 && std::regex_match(name,
                    std::wregex(L"Local\\\\FetcherLauncherRestart-[0-9-]+")))
                options.restartReadyEvent = std::move(name);
        }
    }
    LocalFree(arguments);

    std::error_code error;
    const fs::path absolute = fs::absolute(options.installRoot, error);
    if (!error) {
        options.installRoot = absolute.lexically_normal();
    }
    return options;
}

bool OpenPath(const fs::path &path, const fs::path &workingDirectory) {
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
                      workingDirectory.empty() ? nullptr : workingDirectory.c_str(), SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

fs::path FindUiFile(const fs::path &executablePath) {
    const fs::path executableDirectory = executablePath.parent_path();
    const std::vector<fs::path> candidates = {
        executableDirectory / L"ui" / L"index.html",
        executableDirectory.parent_path() / L"ui" / L"index.html",
    };

    for (const fs::path &candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool PathsEquivalent(const fs::path &left, const fs::path &right) {
    std::error_code leftError;
    std::error_code rightError;
    const fs::path normalizedLeft = fs::weakly_canonical(left, leftError);
    const fs::path normalizedRight = fs::weakly_canonical(right, rightError);
    const std::wstring leftText = (leftError ? fs::absolute(left) : normalizedLeft).wstring();
    const std::wstring rightText = (rightError ? fs::absolute(right) : normalizedRight).wstring();
    return _wcsicmp(leftText.c_str(), rightText.c_str()) == 0;
}

fs::path GetLauncherStageBase() {
    PWSTR rawPath = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &rawPath);
    if (FAILED(result) || rawPath == nullptr) {
        if (rawPath != nullptr) {
            CoTaskMemFree(rawPath);
        }
        throw std::runtime_error("Windows could not resolve the LocalAppData folder.");
    }

    const fs::path localAppData(rawPath);
    CoTaskMemFree(rawPath);
    return localAppData / L"FetcherSimulator" / L"LauncherStaging";
}

void CleanupOldStagedLaunchers(const fs::path &currentExecutable) {
    std::error_code error;
    const fs::path stageBase = GetLauncherStageBase();
    if (!fs::exists(stageBase, error) || error) {
        return;
    }

    for (const fs::directory_entry &entry : fs::directory_iterator(stageBase, error)) {
        if (error) {
            return;
        }
        if (!entry.is_directory()) {
            continue;
        }

        const std::wstring name = entry.path().filename().wstring();
        if (name.rfind(L"stage-", 0) != 0) {
            continue;
        }
        if (!currentExecutable.empty() && PathsEquivalent(entry.path(), currentExecutable.parent_path())) {
            continue;
        }

        try {
            fetcher::removeManaged(stageBase, entry.path());
        } catch (const std::exception &) {
            // An active launcher or a linked path is never removed during cleanup.
        }
    }
}

bool StartStagedLauncher(const Options &options, const fs::path &executablePath) {
    if (options.staged || options.selfTest || options.uiSmokeTest || options.runUpdater ||
        !PathsEquivalent(executablePath.parent_path(), options.installRoot)) {
        return false;
    }

    CleanupOldStagedLaunchers({});

    const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path stageRoot = GetLauncherStageBase() / (L"stage-" + std::to_wstring(GetCurrentProcessId()) +
                                                         L"-" + std::to_wstring(ticks));
    const fs::path stagedExecutable = stageRoot / L"FetcherLauncher.exe";
    const fs::path sourceUiRoot = options.installRoot / L"ui";
    const fs::path stagedUiRoot = stageRoot / L"ui";

    fs::create_directories(stageRoot);
    fs::copy_file(executablePath, stagedExecutable, fs::copy_options::overwrite_existing);
    if (fs::exists(sourceUiRoot)) {
        fs::copy(sourceUiRoot, stagedUiRoot,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }

    std::wstring commandLine = QuoteArgument(stagedExecutable.wstring()) + L" --staged --install-root " +
                               QuoteArgument(options.installRoot.wstring());
    if (options.debug) {
        commandLine += L" --debug";
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                        options.installRoot.c_str(), &startup, &processInfo);
    if (!created) {
        throw std::runtime_error("Windows could not start the staged Fetcher Launcher.");
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

void RestartLauncher(const fs::path &root, bool debug) {
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto eventName = L"Local\\FetcherLauncherRestart-" + suffix;
    WinHandle ready(CreateEventW(nullptr, TRUE, FALSE, eventName.c_str()));
    if (!ready) throw std::runtime_error("Could not create launcher restart acknowledgement.");
    const auto stage = GetLauncherStageBase() / (L"stage-" + suffix);
    bool handshake = false;
    {
        fetcher::InstallLock lock(root);
        handshake = fetcher::stageLauncherRuntime(root, stage);
    }
    auto executable = stage / L"FetcherLauncher.exe";
    auto command = QuoteArgument(executable.wstring()) + L" --staged --install-root " +
                   QuoteArgument(root.wstring()) + L" --restart-ready-event " + QuoteArgument(eventName);
    if (debug) command += L" --debug";
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.c_str(), buffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        root.c_str(), &startup, &child))
        throw std::runtime_error("The updated launcher could not start. This window remains open.");
    WinHandle process(child.hProcess), thread(child.hThread);
    if (handshake) {
        HANDLE handles[] = {ready.get(), process.get()};
        if (WaitForMultipleObjects(2, handles, FALSE, 15000) == WAIT_OBJECT_0) return;
    } else {
        // Older rollback targets cannot acknowledge the new bridge; require their GUI message loop.
        DWORD code = 0;
        if (WaitForInputIdle(process.get(), 15000) == 0 &&
            GetExitCodeProcess(process.get(), &code) && code == STILL_ACTIVE) return;
    }
    throw std::runtime_error("The updated launcher did not confirm startup. This window remains open; retry Restart launcher.");
}

std::string PathToFileUrl(const fs::path &path) {
    std::string value = WideToUtf8(fs::absolute(path).wstring());
    std::replace(value.begin(), value.end(), '\\', '/');

    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result = "file:///";
    result.reserve(value.size() + 16);
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/' || ch == ':') {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('%');
            result.push_back(hex[(ch >> 4) & 0x0f]);
            result.push_back(hex[ch & 0x0f]);
        }
    }
    return result;
}

std::string FallbackHtml() {
    return R"html(<!doctype html><html><body style="background:#111;color:#eee;font-family:sans-serif;padding:2rem">
<h1>Fetcher Launcher</h1><p>The launcher UI file could not be found.</p>
<p>Expected <code>ui/index.html</code> beside FetcherLauncher.exe.</p></body></html>)html";
}

class LauncherApp {
  public:
    LauncherApp(webview::webview &window, fs::path root, const fs::path &executable,
                const fs::path &uiFile, bool debug)
        : mWindow(window), mRoot(std::move(root)), mDebug(debug), mBackend(mRoot, [this](const std::string &text) {
              PostEvent(fetcher::Json{{"type", "log"}, {"text", text}}.dump());
          }) {
        if (!uiFile.empty()) mLoadedRuntime = fetcher::launcherRuntime(executable, uiFile.parent_path());
        mSnapshot = mBackend.state().dump();
    }
    ~LauncherApp() {
        Close();
        if (mWorker.joinable())
            mWorker.join();
    }
    std::string GetStatusJson() {
        std::lock_guard<std::mutex> lock(mSnapshotMutex);
        return mSnapshot;
    }
    std::string Command(const std::string &arguments) {
        fetcher::Json request;
        try {
            request = fetcher::Json::parse(arguments).at(0);
            if (!request.is_object())
                throw std::runtime_error("Invalid action.");
        } catch (const std::exception &e) {
            return fetcher::Json{{"started", false}, {"error", e.what()}}.dump();
        }
        if (request.value("op", "") == "cancelUmo") {
            fetcher::cancelUmo();
            return "{\"accepted\":true}";
        }
        if (mBusy.exchange(true))
            return R"({"started":false,"error":"Wait for the current operation to finish."})";
        if (mWorker.joinable())
            mWorker.join();
        fetcher::resetUmoCancellation();
        mWorker = std::thread([this, request] {
            bool ok = true;
            std::string error;
            try {
                if (request.value("op", "") != "restartLauncher")
                    mBackend.command(request);
            } catch (const std::exception &e) {
                ok = false;
                error = e.what();
            }
            auto state = mBackend.state();
            bool changed = false;
            try {
                changed = !mLoadedRuntime.empty() && fetcher::launcherRuntime(
                    mRoot / "FetcherLauncher.exe", mRoot / "ui") != mLoadedRuntime;
            } catch (const std::exception &) {
                changed = true; // Missing/partial files must be verified before any handoff.
            }
            state["restartRequired"] = changed;
            const auto op = request.value("op", "");
            if (ok && changed && (op == "update" || op == "repair" || op == "rollbackUpdate" ||
                                  op == "restartLauncher")) {
                PostEvent(R"({"type":"restarting"})");
                try {
                    RestartLauncher(mRoot, mDebug);
                    mWindow.dispatch([this] {
                        if (!mClosing) { mClosing = true; mWindow.terminate(); }
                    });
                    return;
                } catch (const std::exception &e) {
                    ok = false;
                    error = e.what();
                }
            }
            {
                std::lock_guard<std::mutex> lock(mSnapshotMutex);
                mSnapshot = state.dump();
            }
            mBusy = false;
            PostEvent(fetcher::Json{
                {"type", "action-complete"},
                {"action", request.value("op", "")},
                {"ok", ok},
                {"error", error},
                {"state",
                 state}}.dump(-1, ' ', false, fetcher::Json::error_handler_t::replace));
        });
        return R"({"started":true})";
    }
    std::string OpenInstallFolder() {
        return OpenPath(mRoot, mRoot) ? R"({"ok":true})" : R"({"ok":false,"error":"Could not open folder."})";
    }
    void Close() { mClosing = true; fetcher::cancelUmo(); }

  private:
    void PostEvent(const std::string &event) {
        if (mClosing)
            return;
        mWindow.dispatch([this, event] {
            if (!mClosing)
                mWindow.eval("window.fetcherReceive && window.fetcherReceive(" + event + ");");
        });
    }
    webview::webview &mWindow;
    fs::path mRoot;
    bool mDebug = false;
    fetcher::Json mLoadedRuntime;
    fetcher::Backend mBackend;
    std::atomic<bool> mBusy{false}, mClosing{false};
    std::thread mWorker;
    std::mutex mSnapshotMutex;
    std::string mSnapshot;
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const Options options = ParseOptions();
    // Browser callbacks bypass the worker/update lock and never open another launcher window.
    if (!options.umoNxm.empty()) {
        const auto callbackResult = options.installRoot / "_fetcher_launcher" / "umo" / "callback-result.json";
        try {
            fetcher::writeAtomic(callbackResult, "{\"received\":true,\"forwarded\":false}");
            auto publicLink = fetcher::parseUmoLink(options.umoNxm);
            publicLink.erase("query");
            publicLink["received"] = true;
            publicLink["forwarded"] = false;
            fetcher::writeAtomic(callbackResult, publicLink.dump());
            fetcher::forwardUmoLink(options.installRoot, options.umoNxm);
            publicLink["forwarded"] = true;
            fetcher::writeAtomic(callbackResult, publicLink.dump());
            return 0;
        } catch (...) {
            // Tokens and signed URLs must never reach a log or error report.
            return 1;
        }
    }
    fetcher::allowLocalFixtures(options.allowLocalFixtures && !options.requestFile.empty());
    if (!options.nexusFixture.empty()) fetcher::nexusFixtureUrl(options.nexusFixture);
    const fs::path executablePath = GetExecutablePath();
    const fs::path uiPath = FindUiFile(executablePath);
    if (options.uiSmokeTest && options.logFile.empty())
        return 2;
    if (options.uiSmokeTest && uiPath.empty()) {
        fetcher::writeAtomic(options.logFile,
            R"({"ok":false,"webView2Loaded":false,"error":"Launcher UI files are missing."})");
        return 1;
    }

    if (!options.requestFile.empty()) {
        try {
            fetcher::Backend backend(options.installRoot);
            auto result = backend.command(fetcher::readJson(options.requestFile));
            if (!options.logFile.empty())
                fetcher::writeAtomic(options.logFile, result.dump(2));
            return 0;
        } catch (const std::exception &e) {
            if (!options.logFile.empty())
                fetcher::writeAtomic(options.logFile, fetcher::Json{{"error", e.what()}}.dump(-1, ' ', false, fetcher::Json::error_handler_t::replace));
            return 1;
        }
    }
    if (options.runUpdater) {
        std::ofstream log;
        if (!options.logFile.empty()) {
            log.open(options.logFile, std::ios::binary | std::ios::trunc);
        }

        const UpdaterProcessResult result = RunNativeUpdater(
            options.installRoot, options.quickCheck, options.statusOnly, [&log](const std::string &output) {
                if (log) {
                    log.write(output.data(), static_cast<std::streamsize>(output.size()));
                    log.flush();
                }
            });
        return static_cast<int>(result.exitCode);
    }

    if (options.selfTest) {
        const bool backendAvailable = true;
        const bool uiExists = !uiPath.empty() && fs::exists(uiPath);
        return backendAvailable && uiExists ? 0 : 1;
    }

    int uiSmokeResult = 1;
    try {
        if (StartStagedLauncher(options, executablePath)) {
            return 0;
        }
        CleanupOldStagedLaunchers(executablePath);
        if (!fetcher::recoverInstallTransactions(options.installRoot).empty()) {
            if (options.uiSmokeTest) {
                fetcher::writeAtomic(options.logFile,
                    R"({"ok":false,"error":"Recovered an interrupted install; rerun the UI smoke test."})");
                return 1;
            }
            if (!fs::is_regular_file(options.installRoot / L"FetcherLauncher.exe"))
                throw std::runtime_error(
                    "Interrupted first installation was restored. Run the setup/bootstrap again.");
            fetcher::launch(options.installRoot / L"FetcherLauncher.exe",
                            {L"--install-root", options.installRoot.wstring()}, options.installRoot);
            return 0;
        }

        webview::webview window(options.debug, nullptr);
        LauncherApp app(window, options.installRoot, executablePath, uiPath, options.debug);

        window.set_title("Fetcher Launcher");
        SetLauncherWindowIcon(window);
        window.set_size(1120, 760, WEBVIEW_HINT_NONE);
        window.set_size(860, 620, WEBVIEW_HINT_MIN);

        window.bind("fetcherGetStatus", [&app](const std::string &) { return app.GetStatusJson(); });
        window.bind("fetcherUiReady", [&options](const std::string &) {
            if (!options.restartReadyEvent.empty()) {
                WinHandle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, options.restartReadyEvent.c_str()));
                if (ready) SetEvent(ready.get());
            }
            return "{}";
        });
        window.bind("fetcherCommand", [&app](const std::string &request) { return app.Command(request); });
        window.bind("fetcherOpenInstallFolder",
                    [&app](const std::string &) { return app.OpenInstallFolder(); });
        if (options.uiSmokeTest) {
            window.init("window.fetcherUiSmokeTest = true;");
            window.bind("fetcherUiSmokeComplete", [&](const std::string &arguments) {
                const auto value = fetcher::Json::parse(arguments).at(0);
                const fetcher::Json report = {
                    {"ok", value.value("ok", false)},
                    {"webView2Loaded", true},
                    {"bridgeReady", value.value("bridgeReady", false)},
                    {"checks", value.value("checks", fetcher::Json::object())},
                    {"error", value.value("error", "")}};
                fetcher::writeAtomic(options.logFile, report.dump(2));
                uiSmokeResult = report.at("ok").get<bool>() ? 0 : 1;
                window.dispatch([&window] { window.terminate(); });
                return R"({"recorded":true})";
            });
        }

        if (uiPath.empty()) {
            window.set_html(FallbackHtml());
        } else {
            window.navigate(PathToFileUrl(uiPath));
        }
        window.run();
        app.Close();
        if (options.uiSmokeTest)
            return uiSmokeResult;
    } catch (const webview::exception &exception) {
        if (options.uiSmokeTest) {
            DWORD session = 0;
            ProcessIdToSessionId(GetCurrentProcessId(), &session);
            const std::string detail = exception.what();
            fetcher::writeAtomic(options.logFile, fetcher::Json{{"ok", false},
                {"webView2Loaded", false}, {"windowsSession", session},
                {"errorCode", exception.error().code()},
                {"error", detail.empty() ? "WebView2 initialization failed in this Windows session." : detail}}.dump());
            return 1;
        }
        const std::wstring message = L"Fetcher Launcher could not start.\n\n" + Utf8ToWide(exception.what()) +
                                     L"\n\nMicrosoft Edge WebView2 Runtime may need to be installed.";
        MessageBoxW(nullptr, message.c_str(), L"Fetcher Launcher", MB_OK | MB_ICONERROR);
        return 1;
    } catch (const std::exception &exception) {
        if (options.uiSmokeTest) {
            fetcher::writeAtomic(options.logFile, fetcher::Json{{"ok", false},
                {"error", exception.what()}}.dump(-1, ' ', false, fetcher::Json::error_handler_t::replace));
            return 1;
        }
        const std::wstring message = Utf8ToWide(exception.what());
        MessageBoxW(nullptr, message.c_str(), L"Fetcher Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }

    return 0;
}
