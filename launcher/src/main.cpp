#include "webview/webview.h"
#include "resource.h"

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

namespace
{
    class WinHandle
    {
    public:
        WinHandle() = default;
        explicit WinHandle(HANDLE handle) : mHandle(handle) {}
        ~WinHandle() { reset(); }

        WinHandle(const WinHandle&) = delete;
        WinHandle& operator=(const WinHandle&) = delete;

        WinHandle(WinHandle&& other) noexcept : mHandle(other.release()) {}
        WinHandle& operator=(WinHandle&& other) noexcept
        {
            if (this != &other)
            {
                reset(other.release());
            }
            return *this;
        }

        HANDLE get() const { return mHandle; }

        HANDLE release()
        {
            HANDLE value = mHandle;
            mHandle = nullptr;
            return value;
        }

        void reset(HANDLE handle = nullptr)
        {
            if (mHandle != nullptr && mHandle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(mHandle);
            }
            mHandle = handle;
        }

        explicit operator bool() const
        {
            return mHandle != nullptr && mHandle != INVALID_HANDLE_VALUE;
        }

    private:
        HANDLE mHandle = nullptr;
    };

    std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return {};
        }

        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = MultiByteToWideChar(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (size <= 0)
        {
            return {};
        }

        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            result.data(), size);
        return result;
    }

    std::string BytesToUtf8(const char* data, std::size_t size)
    {
        if (size == 0)
        {
            return {};
        }

        int wideSize = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(size), nullptr, 0);
        UINT codePage = CP_UTF8;
        DWORD flags = MB_ERR_INVALID_CHARS;
        if (wideSize <= 0)
        {
            codePage = CP_OEMCP;
            flags = 0;
            wideSize = MultiByteToWideChar(
                codePage, flags, data, static_cast<int>(size), nullptr, 0);
        }
        if (wideSize <= 0)
        {
            return {};
        }

        std::wstring wide(static_cast<std::size_t>(wideSize), L'\0');
        MultiByteToWideChar(codePage, flags, data, static_cast<int>(size),
            wide.data(), wideSize);
        return WideToUtf8(wide);
    }

    std::string ReadTextFile(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return {};
        }

        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }

    std::string JsonEscape(const std::string& value)
    {
        static constexpr char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(value.size() + 16);

        for (const unsigned char ch : value)
        {
            switch (ch)
            {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (ch < 0x20)
                    {
                        result += "\\u00";
                        result += hex[(ch >> 4) & 0x0f];
                        result += hex[ch & 0x0f];
                    }
                    else
                    {
                        result.push_back(static_cast<char>(ch));
                    }
                    break;
            }
        }
        return result;
    }

    std::string JsonString(const std::string& value)
    {
        return "\"" + JsonEscape(value) + "\"";
    }

    std::string StripAnsi(const std::string& value)
    {
        static const std::regex ansiPattern("\\x1b\\[[0-9;?]*[ -/]*[@-~]");
        return std::regex_replace(value, ansiPattern, "");
    }

    std::string ExtractLastJsonObject(const std::string& value)
    {
        std::size_t end = value.find_last_not_of(" \t\r\n");
        while (end != std::string::npos)
        {
            const std::size_t lineStart = value.rfind('\n', end);
            const std::size_t start = lineStart == std::string::npos ? 0 : lineStart + 1;
            std::string line = value.substr(start, end - start + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            {
                line.pop_back();
            }
            const std::size_t first = line.find_first_not_of(" \t");
            if (first != std::string::npos && line[first] == '{' && line.back() == '}')
            {
                return line.substr(first);
            }
            if (lineStart == std::string::npos)
            {
                break;
            }
            end = value.find_last_not_of(" \t\r\n", lineStart);
        }
        return {};
    }

    std::string ExtractJsonString(const std::string& json, const std::string& key)
    {
        const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
        std::smatch match;
        if (std::regex_search(json, match, pattern) && match.size() > 1)
        {
            return match[1].str();
        }
        return {};
    }

    fs::path GetExecutablePath()
    {
        std::vector<wchar_t> buffer(32768);
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            return {};
        }
        return fs::path(std::wstring(buffer.data(), length));
    }

    void SetLauncherWindowIcon(webview::webview& window)
    {
        const auto nativeWindow = window.window();
        if (!nativeWindow.ok())
        {
            return;
        }

        const HWND handle = static_cast<HWND>(nativeWindow.value());
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        if (handle == nullptr || instance == nullptr)
        {
            return;
        }

        const auto loadIcon = [instance](int width, int height) {
            return static_cast<HICON>(LoadImageW(instance,
                MAKEINTRESOURCEW(IDI_FETCHER_LAUNCHER), IMAGE_ICON,
                width, height, LR_DEFAULTCOLOR | LR_SHARED));
        };

        if (const HICON largeIcon = loadIcon(
                GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)))
        {
            SendMessageW(handle, WM_SETICON, ICON_BIG,
                reinterpret_cast<LPARAM>(largeIcon));
        }
        if (const HICON smallIcon = loadIcon(
                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)))
        {
            SendMessageW(handle, WM_SETICON, ICON_SMALL,
                reinterpret_cast<LPARAM>(smallIcon));
        }
    }

    std::wstring QuoteArgument(const std::wstring& value)
    {
        std::wstring result = L"\"";
        std::size_t backslashes = 0;

        for (const wchar_t ch : value)
        {
            if (ch == L'\\')
            {
                ++backslashes;
                continue;
            }
            if (ch == L'\"')
            {
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

    fs::path GetPowerShellPath()
    {
        std::vector<wchar_t> buffer(32768);
        const UINT length = GetWindowsDirectoryW(
            buffer.data(), static_cast<UINT>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            return L"powershell.exe";
        }

        return fs::path(std::wstring(buffer.data(), length)) /
            L"System32" / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
    }

    struct UpdaterProcessResult
    {
        DWORD exitCode = 1;
        std::string errorMessage;
    };

    UpdaterProcessResult RunUpdaterProcess(
        const fs::path& installRoot,
        bool quickCheck,
        bool statusOnly,
        const std::function<void(const std::string&)>& onOutput)
    {
        UpdaterProcessResult result;
        fs::path stagedScript;
        fs::path stagedRunner;

        try
        {
            const fs::path updaterScript = installRoot / L"Update-Fetcher-Simulator.ps1";
            if (!fs::exists(updaterScript))
            {
                throw std::runtime_error(
                    "Update-Fetcher-Simulator.ps1 was not found in the installation folder.");
            }

            const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            stagedScript = fs::temp_directory_path() /
                (L"Fetcher-Simulator-Updater-" + std::to_wstring(GetCurrentProcessId()) +
                    L"-" + std::to_wstring(ticks) + L".ps1");
            fs::copy_file(updaterScript, stagedScript, fs::copy_options::overwrite_existing);

            stagedRunner = fs::temp_directory_path() /
                (L"Fetcher-Simulator-Updater-Runner-" + std::to_wstring(GetCurrentProcessId()) +
                    L"-" + std::to_wstring(ticks) + L".ps1");
            std::ofstream runner(stagedRunner, std::ios::binary | std::ios::trunc);
            runner << R"(param(
    [Parameter(Mandatory = $true)][string] $UpdaterScript,
    [Parameter(Mandatory = $true)][string] $InstallRoot,
    [switch] $QuickCheck,
    [switch] $StatusOnly
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$ProgressPreference = "SilentlyContinue"

$parameters = @{ InstallRoot = $InstallRoot }
if ($QuickCheck) { $parameters.QuickCheck = $true }
if ($StatusOnly) { $parameters.StatusOnly = $true }

& $UpdaterScript @parameters
exit $LASTEXITCODE
)";
            runner.close();
            if (!runner)
            {
                throw std::runtime_error("Could not create the updater runner script.");
            }

            SECURITY_ATTRIBUTES security{};
            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;

            HANDLE readPipeRaw = nullptr;
            HANDLE writePipeRaw = nullptr;
            if (!CreatePipe(&readPipeRaw, &writePipeRaw, &security, 0))
            {
                throw std::runtime_error("Could not create the updater output pipe.");
            }

            WinHandle readPipe(readPipeRaw);
            WinHandle writePipe(writePipeRaw);
            if (!SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0))
            {
                throw std::runtime_error("Could not configure the updater output pipe.");
            }

            WinHandle nullInput(CreateFileW(L"NUL", GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr));

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = writePipe.get();
            startup.hStdError = writePipe.get();
            startup.hStdInput = nullInput ? nullInput.get() : GetStdHandle(STD_INPUT_HANDLE);

            PROCESS_INFORMATION processInfo{};
            const fs::path powerShell = GetPowerShellPath();
            std::wstring commandLine = QuoteArgument(powerShell.wstring()) +
                L" -NoLogo -NoProfile -ExecutionPolicy Bypass -File " +
                QuoteArgument(stagedRunner.wstring()) +
                L" -UpdaterScript " + QuoteArgument(stagedScript.wstring()) +
                L" -InstallRoot " + QuoteArgument(installRoot.wstring());
            if (statusOnly)
            {
                commandLine += L" -StatusOnly";
            }
            else if (quickCheck)
            {
                commandLine += L" -QuickCheck";
            }
            std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
            mutableCommand.push_back(L'\0');

            const BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr,
                TRUE, CREATE_NO_WINDOW, nullptr, installRoot.c_str(), &startup, &processInfo);
            if (!created)
            {
                throw std::runtime_error("Windows could not start PowerShell.");
            }

            WinHandle process(processInfo.hProcess);
            WinHandle processThread(processInfo.hThread);
            writePipe.reset();

            std::vector<char> buffer(4096);
            DWORD bytesRead = 0;
            while (ReadFile(readPipe.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
                &bytesRead, nullptr) && bytesRead > 0)
            {
                if (onOutput)
                {
                    onOutput(StripAnsi(BytesToUtf8(buffer.data(), bytesRead)));
                }
            }

            WaitForSingleObject(process.get(), INFINITE);
            if (!GetExitCodeProcess(process.get(), &result.exitCode))
            {
                result.exitCode = 1;
            }
        }
        catch (const std::exception& exception)
        {
            result.errorMessage = exception.what();
            result.exitCode = 1;
            if (onOutput)
            {
                onOutput("\nLauncher error: " + result.errorMessage + "\n");
            }
        }

        if (!stagedScript.empty())
        {
            std::error_code removeError;
            fs::remove(stagedScript, removeError);
        }
        if (!stagedRunner.empty())
        {
            std::error_code removeError;
            fs::remove(stagedRunner, removeError);
        }
        return result;
    }

    struct Options
    {
        fs::path installRoot;
        fs::path logFile;
        bool selfTest = false;
        bool runUpdater = false;
        bool quickCheck = false;
        bool statusOnly = false;
        bool staged = false;
        bool debug = false;
    };

    Options ParseOptions()
    {
        Options options;
        options.installRoot = GetExecutablePath().parent_path();

        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr)
        {
            return options;
        }

        for (int index = 1; index < argumentCount; ++index)
        {
            const std::wstring argument = arguments[index];
            if (argument == L"--install-root" && index + 1 < argumentCount)
            {
                options.installRoot = fs::path(arguments[++index]);
            }
            else if (argument == L"--log-file" && index + 1 < argumentCount)
            {
                options.logFile = fs::path(arguments[++index]);
            }
            else if (argument == L"--self-test")
            {
                options.selfTest = true;
            }
            else if (argument == L"--run-updater")
            {
                options.runUpdater = true;
            }
            else if (argument == L"--quick-check")
            {
                options.quickCheck = true;
            }
            else if (argument == L"--status-only")
            {
                options.statusOnly = true;
            }
            else if (argument == L"--staged")
            {
                options.staged = true;
            }
            else if (argument == L"--debug")
            {
                options.debug = true;
            }
        }
        LocalFree(arguments);

        std::error_code error;
        const fs::path absolute = fs::absolute(options.installRoot, error);
        if (!error)
        {
            options.installRoot = absolute.lexically_normal();
        }
        return options;
    }

    bool OpenPath(const fs::path& path, const fs::path& workingDirectory)
    {
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(), SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    fs::path FindUiFile(const fs::path& executablePath)
    {
        const fs::path executableDirectory = executablePath.parent_path();
        const std::vector<fs::path> candidates = {
            executableDirectory / L"ui" / L"index.html",
            executableDirectory.parent_path() / L"ui" / L"index.html",
        };

        for (const fs::path& candidate : candidates)
        {
            if (fs::exists(candidate))
            {
                return candidate;
            }
        }
        return {};
    }

    bool PathsEquivalent(const fs::path& left, const fs::path& right)
    {
        std::error_code leftError;
        std::error_code rightError;
        const fs::path normalizedLeft = fs::weakly_canonical(left, leftError);
        const fs::path normalizedRight = fs::weakly_canonical(right, rightError);
        const std::wstring leftText = (leftError ? fs::absolute(left) : normalizedLeft).wstring();
        const std::wstring rightText = (rightError ? fs::absolute(right) : normalizedRight).wstring();
        return _wcsicmp(leftText.c_str(), rightText.c_str()) == 0;
    }

    fs::path GetLauncherStageBase()
    {
        PWSTR rawPath = nullptr;
        const HRESULT result = SHGetKnownFolderPath(
            FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &rawPath);
        if (FAILED(result) || rawPath == nullptr)
        {
            if (rawPath != nullptr)
            {
                CoTaskMemFree(rawPath);
            }
            throw std::runtime_error("Windows could not resolve the LocalAppData folder.");
        }

        const fs::path localAppData(rawPath);
        CoTaskMemFree(rawPath);
        return localAppData / L"FetcherSimulator" / L"LauncherStaging";
    }

    void CleanupOldStagedLaunchers(const fs::path& currentExecutable)
    {
        std::error_code error;
        const fs::path stageBase = GetLauncherStageBase();
        if (!fs::exists(stageBase, error) || error)
        {
            return;
        }

        for (const fs::directory_entry& entry : fs::directory_iterator(stageBase, error))
        {
            if (error)
            {
                return;
            }
            if (!entry.is_directory())
            {
                continue;
            }

            const std::wstring name = entry.path().filename().wstring();
            if (name.rfind(L"stage-", 0) != 0)
            {
                continue;
            }
            if (!currentExecutable.empty() &&
                PathsEquivalent(entry.path(), currentExecutable.parent_path()))
            {
                continue;
            }

            std::error_code removeError;
            fs::remove_all(entry.path(), removeError);
        }
    }

    bool StartStagedLauncher(const Options& options, const fs::path& executablePath)
    {
        if (options.staged || options.selfTest || options.runUpdater ||
            !PathsEquivalent(executablePath.parent_path(), options.installRoot))
        {
            return false;
        }

        CleanupOldStagedLaunchers({});

        const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const fs::path stageRoot = GetLauncherStageBase() /
            (L"stage-" + std::to_wstring(GetCurrentProcessId()) +
                L"-" + std::to_wstring(ticks));
        const fs::path stagedExecutable = stageRoot / L"FetcherLauncher.exe";
        const fs::path sourceUiRoot = options.installRoot / L"ui";
        const fs::path stagedUiRoot = stageRoot / L"ui";

        fs::create_directories(stageRoot);
        fs::copy_file(executablePath, stagedExecutable, fs::copy_options::overwrite_existing);
        if (fs::exists(sourceUiRoot))
        {
            fs::copy(sourceUiRoot, stagedUiRoot,
                fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }

        std::wstring commandLine = QuoteArgument(stagedExecutable.wstring()) +
            L" --staged --install-root " + QuoteArgument(options.installRoot.wstring());
        if (options.debug)
        {
            commandLine += L" --debug";
        }
        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION processInfo{};
        const BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr,
            FALSE, 0, nullptr, options.installRoot.c_str(), &startup, &processInfo);
        if (!created)
        {
            throw std::runtime_error("Windows could not start the staged Fetcher Launcher.");
        }

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return true;
    }

    std::string PathToFileUrl(const fs::path& path)
    {
        std::string value = WideToUtf8(fs::absolute(path).wstring());
        std::replace(value.begin(), value.end(), '\\', '/');

        static constexpr char hex[] = "0123456789ABCDEF";
        std::string result = "file:///";
        result.reserve(value.size() + 16);
        for (const unsigned char ch : value)
        {
            if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' ||
                ch == '~' || ch == '/' || ch == ':')
            {
                result.push_back(static_cast<char>(ch));
            }
            else
            {
                result.push_back('%');
                result.push_back(hex[(ch >> 4) & 0x0f]);
                result.push_back(hex[ch & 0x0f]);
            }
        }
        return result;
    }

    std::string FallbackHtml()
    {
        return R"html(<!doctype html><html><body style="background:#111;color:#eee;font-family:sans-serif;padding:2rem">
<h1>Fetcher Launcher</h1><p>The launcher UI file could not be found.</p>
<p>Expected <code>ui/index.html</code> beside FetcherLauncher.exe.</p></body></html>)html";
    }

    class LauncherApp
    {
    public:
        LauncherApp(webview::webview& window, fs::path installRoot)
            : mWindow(window), mInstallRoot(std::move(installRoot))
        {
        }

        ~LauncherApp()
        {
            mClosing.store(true);
            std::lock_guard<std::mutex> lock(mWorkerMutex);
            if (mWorker.joinable())
            {
                mWorker.join();
            }
        }

        std::string GetStatusJson() const
        {
            const fs::path updaterPath = mInstallRoot / L"Update-Fetcher-Simulator.ps1";
            const fs::path channelPath = mInstallRoot / L"fetcher-update-channel.json";
            const fs::path statePath = mInstallRoot / L"fetcher-update-state.json";

            const std::string channelJson = ReadTextFile(channelPath);
            const std::string stateJson = ReadTextFile(statePath);
            std::string channel = ExtractJsonString(channelJson, "channel");
            std::string releaseTag = ExtractJsonString(stateJson, "releaseTag");
            std::string commit = ExtractJsonString(stateJson, "commit");

            if (channel.empty())
            {
                channel = "default";
            }
            if (releaseTag.empty())
            {
                releaseTag = ExtractJsonString(channelJson, "clientReleaseTag");
            }

            std::ostringstream json;
            json << "{"
                 << "\"installRoot\":" << JsonString(WideToUtf8(mInstallRoot.wstring())) << ","
                 << "\"channel\":" << JsonString(channel) << ","
                 << "\"releaseTag\":" << JsonString(releaseTag) << ","
                 << "\"commit\":" << JsonString(commit) << ","
                 << "\"updaterAvailable\":" << (fs::exists(updaterPath) ? "true" : "false") << ","
                 << "\"openmwAvailable\":" << (fs::exists(mInstallRoot / L"openmw.exe") ? "true" : "false") << ","
                 << "\"wizardAvailable\":" << (fs::exists(mInstallRoot / L"openmw-wizard.exe") ? "true" : "false") << ","
                 << "\"busy\":" << (mBusy.load() ? "true" : "false")
                 << "}";
            return json.str();
        }

        std::string StartUpdate(bool quickCheck)
        {
            if (!fs::exists(mInstallRoot / L"Update-Fetcher-Simulator.ps1"))
            {
                return "{\"started\":false,\"error\":\"Update-Fetcher-Simulator.ps1 was not found in the installation folder.\"}";
            }
            if (mBusy.exchange(true))
            {
                return "{\"started\":false,\"error\":\"An update is already running.\"}";
            }

            std::lock_guard<std::mutex> lock(mWorkerMutex);
            if (mWorker.joinable())
            {
                mWorker.join();
            }
            mWorker = std::thread([this, quickCheck]() { RunUpdate(quickCheck); });
            return "{\"started\":true}";
        }

        std::string StartStatusCheck()
        {
            if (!fs::exists(mInstallRoot / L"Update-Fetcher-Simulator.ps1"))
            {
                return "{\"started\":false,\"error\":\"Update-Fetcher-Simulator.ps1 was not found in the installation folder.\"}";
            }
            if (mBusy.exchange(true))
            {
                return "{\"started\":false,\"error\":\"The launcher is already checking or updating.\"}";
            }

            std::lock_guard<std::mutex> lock(mWorkerMutex);
            if (mWorker.joinable())
            {
                mWorker.join();
            }
            mWorker = std::thread([this]() { RunStatusCheck(); });
            return "{\"started\":true}";
        }

        std::string LaunchOpenMw()
        {
            return LaunchExecutable(L"openmw.exe", "OpenMW was not found in the installation folder.");
        }

        std::string LaunchWizard()
        {
            return LaunchExecutable(L"openmw-wizard.exe", "openmw-wizard.exe was not found in the installation folder.");
        }

        std::string OpenInstallFolder()
        {
            if (OpenPath(mInstallRoot, mInstallRoot))
            {
                return "{\"ok\":true}";
            }
            return "{\"ok\":false,\"error\":\"Windows could not open the installation folder.\"}";
        }

        void Close()
        {
            mClosing.store(true);
        }

    private:
        std::string LaunchExecutable(const fs::path& filename, const std::string& missingMessage)
        {
            const fs::path executable = mInstallRoot / filename;
            if (!fs::exists(executable))
            {
                return "{\"ok\":false,\"error\":" + JsonString(missingMessage) + "}";
            }
            if (!OpenPath(executable, mInstallRoot))
            {
                return "{\"ok\":false,\"error\":\"Windows could not launch the selected program.\"}";
            }
            return "{\"ok\":true}";
        }

        void PostEvent(const std::string& eventJson)
        {
            if (mClosing.load())
            {
                return;
            }

            mWindow.dispatch([this, eventJson]() {
                if (!mClosing.load())
                {
                    mWindow.eval("window.fetcherReceive && window.fetcherReceive(" + eventJson + ");");
                }
            });
        }

        void PostLog(const std::string& text)
        {
            if (!text.empty())
            {
                PostEvent("{\"type\":\"log\",\"text\":" + JsonString(text) + "}");
            }
        }

        void RunStatusCheck()
        {
            PostEvent("{\"type\":\"status-check-started\"}");

            std::string output;
            const UpdaterProcessResult result = RunUpdaterProcess(
                mInstallRoot,
                false,
                true,
                [&output](const std::string& chunk) { output += chunk; });

            mBusy.store(false);
            std::string statusJson = ExtractLastJsonObject(output);
            const bool success = result.exitCode == 0 && !statusJson.empty();
            if (statusJson.empty())
            {
                std::string message = result.errorMessage;
                if (message.empty())
                {
                    message = "The update service did not return a valid status response.";
                }
                statusJson = "{\"schemaVersion\":1,\"status\":\"error\",\"upToDate\":false,\"message\":" +
                    JsonString(message) + "}";
            }

            std::ostringstream event;
            event << "{\"type\":\"status-check-complete\",\"success\":"
                  << (success ? "true" : "false")
                  << ",\"result\":" << statusJson
                  << ",\"localStatus\":" << GetStatusJson() << "}";
            PostEvent(event.str());
        }

        void RunUpdate(bool quickCheck)
        {
            PostEvent("{\"type\":\"update-started\"}");
            PostLog(quickCheck
                ? "Starting Fetcher Simulator quick update check...\n"
                : "Starting Fetcher Simulator full mod check and repair...\n");

            const UpdaterProcessResult result = RunUpdaterProcess(
                mInstallRoot,
                quickCheck,
                false,
                [this](const std::string& output) { PostLog(output); });

            mBusy.store(false);
            const bool success = result.exitCode == 0;
            std::ostringstream event;
            event << "{\"type\":\"update-complete\",\"success\":"
                  << (success ? "true" : "false")
                  << ",\"exitCode\":" << result.exitCode
                  << ",\"error\":" << JsonString(result.errorMessage)
                  << ",\"status\":" << GetStatusJson() << "}";
            PostEvent(event.str());
        }

        webview::webview& mWindow;
        fs::path mInstallRoot;
        std::atomic<bool> mBusy{false};
        std::atomic<bool> mClosing{false};
        std::thread mWorker;
        std::mutex mWorkerMutex;
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const Options options = ParseOptions();
    const fs::path executablePath = GetExecutablePath();
    const fs::path uiPath = FindUiFile(executablePath);

    if (options.runUpdater)
    {
        std::ofstream log;
        if (!options.logFile.empty())
        {
            log.open(options.logFile, std::ios::binary | std::ios::trunc);
        }

        const UpdaterProcessResult result = RunUpdaterProcess(
            options.installRoot,
            options.quickCheck,
            options.statusOnly,
            [&log](const std::string& output) {
                if (log)
                {
                    log.write(output.data(), static_cast<std::streamsize>(output.size()));
                    log.flush();
                }
            });
        return static_cast<int>(result.exitCode);
    }

    if (options.selfTest)
    {
        const bool updaterExists = fs::exists(options.installRoot / L"Update-Fetcher-Simulator.ps1");
        const bool uiExists = !uiPath.empty() && fs::exists(uiPath);
        return updaterExists && uiExists ? 0 : 1;
    }

    try
    {
        if (StartStagedLauncher(options, executablePath))
        {
            return 0;
        }
        CleanupOldStagedLaunchers(executablePath);

        webview::webview window(options.debug, nullptr);
        LauncherApp app(window, options.installRoot);

        window.set_title("Fetcher Simulator Launcher");
        SetLauncherWindowIcon(window);
        window.set_size(1120, 760, WEBVIEW_HINT_NONE);
        window.set_size(860, 620, WEBVIEW_HINT_MIN);

        window.bind("fetcherGetStatus", [&app](const std::string&) {
            return app.GetStatusJson();
        });
        window.bind("fetcherStartUpdate", [&app](const std::string&) {
            return app.StartUpdate(true);
        });
        window.bind("fetcherStartFullUpdate", [&app](const std::string&) {
            return app.StartUpdate(false);
        });
        window.bind("fetcherCheckUpdateStatus", [&app](const std::string&) {
            return app.StartStatusCheck();
        });
        window.bind("fetcherLaunchOpenMw", [&app](const std::string&) {
            return app.LaunchOpenMw();
        });
        window.bind("fetcherLaunchWizard", [&app](const std::string&) {
            return app.LaunchWizard();
        });
        window.bind("fetcherOpenInstallFolder", [&app](const std::string&) {
            return app.OpenInstallFolder();
        });

        if (uiPath.empty())
        {
            window.set_html(FallbackHtml());
        }
        else
        {
            window.navigate(PathToFileUrl(uiPath));
        }
        window.run();
        app.Close();
    }
    catch (const webview::exception& exception)
    {
        const std::wstring message = L"Fetcher Launcher could not start.\n\n" +
            Utf8ToWide(exception.what()) +
            L"\n\nMicrosoft Edge WebView2 Runtime may need to be installed.";
        MessageBoxW(nullptr, message.c_str(), L"Fetcher Simulator Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (const std::exception& exception)
    {
        const std::wstring message = Utf8ToWide(exception.what());
        MessageBoxW(nullptr, message.c_str(), L"Fetcher Simulator Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }

    return 0;
}
