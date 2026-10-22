#include "bootstrap.hpp"
#include "install_transaction.hpp"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <memory>
#include <fstream>
#include <regex>
#include <thread>
using namespace fetcher;
namespace {
constexpr UINT logMessage = WM_APP + 1, finishedMessage = WM_APP + 2;
std::wstring displayText(const std::string &text) {
    if (text.empty())
        return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
    return result;
}
struct Options {
    fs::path root, result;
    bool install = false, fixtures = false;
    int crashAfter = 0;
    std::string tools = toolsReleaseUrl, client;
};
void options(Options &result) {
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
        throw std::runtime_error("Could not read setup arguments.");
    std::unique_ptr<wchar_t *, decltype(&LocalFree)> cleanup(arguments, LocalFree);
    for (int i = 1; i < count; ++i)
        if (std::wstring(arguments[i]) == L"--install")
            result.install = true;
    for (int i = 1; i < count; ++i) {
        std::wstring argument = arguments[i];
        auto value = [&]() -> std::wstring {
            if (++i >= count)
                throw std::runtime_error("Missing setup argument value.");
            return arguments[i];
        };
        if (argument == L"--install-root")
            result.root = value();
        else if (argument == L"--log-file")
            result.result = value();
        else if (argument == L"--install")
            result.install = true;
        else if (argument == L"--allow-local-fixtures")
            result.fixtures = true;
        else if (argument == L"--tools-release-url")
            result.tools = utf8(value());
        else if (argument == L"--client-release-url")
            result.client = utf8(value());
        else if (argument == L"--fixture-crash-after")
            result.crashAfter = std::stoi(value());
        else
            throw std::runtime_error("Unknown setup argument.");
    }
    if (result.fixtures && !result.install)
        throw std::runtime_error("Fixture endpoints require headless setup.");
    auto loopback = [](const std::string &url) {
        return std::regex_match(url, std::regex("http://127\\.0\\.0\\.1:[0-9]+/[^?#]*"));
    };
    if ((result.tools != toolsReleaseUrl || !result.client.empty() || result.crashAfter) &&
        (!result.fixtures || !loopback(result.tools) || !loopback(result.client)))
        throw std::runtime_error(
            "Setup endpoint/crash overrides are restricted to explicit loopback tests.");
}
struct SetupWindow {
    HWND window = nullptr, folder = nullptr, browse = nullptr, install = nullptr, open = nullptr,
         log = nullptr;
    bool busy = false;
    std::thread worker;
    fs::path installedRoot;
    HFONT font = nullptr;
    ~SetupWindow() {
        if (font)
            DeleteObject(font);
    }
    int scale(int value) const { return MulDiv(value, GetDpiForSystem(), 96); }
    HWND control(const wchar_t *type, const wchar_t *text, DWORD style, int x, int y, int width,
                 int height, int id) {
        auto handle = CreateWindowExW(
            type == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, text,
            WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(width), scale(height), window,
            reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return handle;
    }
    void append(const std::string &text) {
        auto value = displayText(text);
        SendMessageW(log, EM_SETSEL, -1, -1);
        SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(value.c_str()));
    }
    void choose() {
        IFileDialog *dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&dialog)))) {
            DWORD flags;
            dialog->GetOptions(&flags);
            dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            dialog->SetTitle(L"Choose a dedicated Fetcher installation folder");
            if (SUCCEEDED(dialog->Show(window))) {
                IShellItem *item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item))) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                        SetWindowTextW(folder, path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dialog->Release();
        }
    }
    void start() {
        auto length = GetWindowTextLengthW(folder);
        std::wstring value(length + 1, L'\0');
        GetWindowTextW(folder, value.data(), int(value.size()));
        value.resize(length);
        installedRoot = value;
        if (installedRoot.empty()) {
            append("Choose an installation folder.\r\n");
            return;
        }
        if (worker.joinable())
            worker.join();
        busy = true;
        EnableWindow(folder, FALSE);
        EnableWindow(browse, FALSE);
        EnableWindow(install, FALSE);
        EnableMenuItem(GetSystemMenu(window, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
        SetWindowTextW(log, L"");
        worker = std::thread([this] {
            auto post = [this](UINT message, const std::string &text, bool failed = false) {
                auto value = std::make_unique<std::string>(text);
                if (PostMessageW(window, message, failed, reinterpret_cast<LPARAM>(value.get())))
                    value.release();
            };
            try {
                bootstrapInstall(installedRoot, [&](const auto &text) { post(logMessage, text); });
                post(finishedMessage, "Setup complete.\r\n");
            } catch (const std::exception &error) {
                post(finishedMessage, std::string("Setup stopped: ") + error.what() +
                                          "\r\nRetry setup to resume.\r\n",
                     true);
            }
        });
    }
    LRESULT message(UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_CTLCOLORSTATIC) {
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        if (message == WM_COMMAND) {
            if (LOWORD(wParam) == 1 && !busy)
                choose();
            if (LOWORD(wParam) == 2 && !busy)
                start();
            if (LOWORD(wParam) == 3)
                ShellExecuteW(window, L"open",
                              L"https://developer.microsoft.com/microsoft-edge/webview2/", nullptr,
                              nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (message == logMessage || message == finishedMessage) {
            std::unique_ptr<std::string> text(reinterpret_cast<std::string *>(lParam));
            append(*text);
            if (message == finishedMessage) {
                busy = false;
                if (worker.joinable())
                    worker.join();
                EnableWindow(folder, TRUE);
                EnableWindow(browse, TRUE);
                EnableWindow(install, TRUE);
                EnableMenuItem(GetSystemMenu(window, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
                if (!wParam && SendMessageW(open, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    try {
                        launch(installedRoot / "FetcherLauncher.exe",
                               {L"--install-root", installedRoot.wstring()}, installedRoot);
                    } catch (const std::exception &error) {
                        append(std::string(error.what()) + "\r\n");
                    }
                }
            }
            return 0;
        }
        if (message == WM_CLOSE) {
            if (!busy)
                DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    static LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
        auto self = reinterpret_cast<SetupWindow *>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<SetupWindow *>(
                reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
            self->window = handle;
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->message(message, wParam, lParam)
                    : DefWindowProcW(handle, message, wParam, lParam);
    }
    int run(fs::path initial) {
        SetProcessDPIAware();
        font = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
        WNDCLASSW type{};
        type.lpfnWndProc = procedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.hIcon = LoadIconW(type.hInstance, MAKEINTRESOURCEW(101));
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        type.lpszClassName = L"FetcherNativeSetup";
        RegisterClassW(&type);
        auto handle = CreateWindowW(type.lpszClassName, L"Fetcher Setup",
                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                    CW_USEDEFAULT, CW_USEDEFAULT, scale(700), scale(475), nullptr,
                                    nullptr, type.hInstance, this);
        if (!handle)
            throw std::runtime_error("Could not open the setup window.");
        control(L"STATIC", L"Install Fetcher Launcher and the multiplayer client", 0, 20, 18, 640,
                25, 0);
        control(
            L"STATIC",
            L"Your owned Morrowind files and server mods are selected in the launcher after setup.",
            0, 20, 48, 640, 35, 0);
        control(L"STATIC", L"Installation folder", 0, 20, 91, 640, 20, 0);
        folder =
            control(L"EDIT", initial.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, 20, 115, 535, 26, 0);
        browse = control(L"BUTTON", L"Browse...", WS_TABSTOP, 565, 115, 95, 26, 1);
        open = control(L"BUTTON", L"Open launcher when setup completes",
                       BS_AUTOCHECKBOX | WS_TABSTOP, 20, 154, 400, 25, 0);
        SendMessageW(open, BM_SETCHECK, BST_CHECKED, 0);
        install = control(L"BUTTON", L"Install / repair", BS_DEFPUSHBUTTON | WS_TABSTOP, 510, 152,
                          150, 30, 2);
        log = control(L"EDIT", L"Ready. Setup uses verified Fetcher release downloads.\r\n",
                      ES_MULTILINE | ES_READONLY | WS_VSCROLL, 20, 195, 640, 180, 0);
        control(L"BUTTON", L"WebView2 Runtime download", WS_TABSTOP, 20, 390, 230, 27, 3);
        control(L"STATIC", L"Install this Microsoft runtime if the launcher cannot open.", 0, 265,
                394, 395, 30, 0);
        ShowWindow(handle, SW_SHOWNORMAL);
        MSG event;
        while (GetMessageW(&event, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(handle, &event)) {
                TranslateMessage(&event);
                DispatchMessageW(&event);
            }
        }
        if (worker.joinable())
            worker.join();
        return 0;
    }
};
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    Options args;
    try {
        options(args);
        allowLocalFixtures(args.fixtures && args.install);
        fixtureInstallCrashAfter(args.crashAfter);
        if (args.install) {
            std::ofstream progress;
            if (!args.result.empty()) {
                auto path = longPath(args.result);
                path += L".progress.txt";
                noLinks(path);
                fs::create_directories(path.parent_path());
                progress.open(path, std::ios::binary | std::ios::trunc);
                if (!progress)
                    throw std::runtime_error("Could not create the setup progress log.");
            }
            auto result = bootstrapInstall(args.root, [&](const auto &text) {
                if (progress) {
                    progress << text;
                    progress.flush();
                }
            }, args.tools, args.client);
            if (!args.result.empty())
                writeAtomic(args.result, result.dump(2));
            return 0;
        }
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (args.root.empty()) {
            PWSTR path = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
                throw std::runtime_error("Could not locate your local application folder.");
            args.root = fs::path(path) / "Fetcher";
            CoTaskMemFree(path);
        }
        SetupWindow window;
        auto result = window.run(args.root);
        CoUninitialize();
        return result;
    } catch (const std::exception &error) {
        try {
            if (!args.result.empty())
                writeAtomic(args.result, Json{{"error", error.what()}}.dump(
                                             2, ' ', false, Json::error_handler_t::replace));
        } catch (
            ...) { /* The nonzero process exit remains available when the log path is unwritable. */
        }
        if (!args.install)
            MessageBoxW(nullptr, displayText(error.what()).c_str(), L"Fetcher Setup",
                        MB_OK | MB_ICONERROR);
        return 1;
    }
}
