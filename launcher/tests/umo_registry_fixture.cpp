#include "umo_helper.hpp"
#include <windows.h>
#include <iostream>
using namespace fetcher;
namespace {
constexpr wchar_t command[] = L"Software\\Classes\\nxm\\shell\\open\\command";
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("UMO protocol fixture assertion failed.");
}
void put(const wchar_t *path, const wchar_t *name, const std::wstring &text) {
    HKEY key = nullptr;
    check(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key,
                          nullptr) == ERROR_SUCCESS);
    auto status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(text.c_str()),
                                 DWORD((text.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    check(status == ERROR_SUCCESS);
}
std::wstring get(const wchar_t *path, const wchar_t *name = nullptr) {
    wchar_t text[4096];
    DWORD size = sizeof(text);
    check(RegGetValueW(HKEY_CURRENT_USER, path, name, RRF_RT_REG_SZ, nullptr, text, &size) ==
          ERROR_SUCCESS);
    return text;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 2)
        return 2;
    // RegOverridePredefKey applies only to this test process. The real NXM registration is
    // untouched.
    const std::wstring testKey = L"Software\\FetcherLauncherTests\\UMO-" +
                                 std::to_wstring(GetCurrentProcessId()) + L"-" +
                                 std::to_wstring(GetTickCount64());
    HKEY hive = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, testKey.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                        &hive, &disposition) != ERROR_SUCCESS ||
        disposition != REG_CREATED_NEW_KEY)
        return 3;
    struct Cleanup {
        HKEY hive;
        std::wstring path;
        ~Cleanup() {
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            RegCloseKey(hive);
            RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        }
    } cleanup{hive, testKey};
    try {
        check(RegOverridePredefKey(HKEY_CURRENT_USER, hive) == ERROR_SUCCESS);
        auto root = fs::path(argv[1]);
        const auto nxm = L"Software\\Classes\\nxm";
        const auto momw = L"Software\\Classes\\momw\\shell\\open\\command";
        put(nxm, nullptr, L"Old NXM description");
        put(nxm, L"URL Protocol", L"old protocol value");
        put(L"Software\\Classes\\nxm\\DefaultIcon", nullptr, L"existing.ico");
        put(command, nullptr, L"old-manager.exe \"%1\"");
        put(momw, nullptr, L"leave-momw-alone.exe \"%1\"");
        check(!umoStatus(root).at("browserLinks").get<bool>());
        umoProtocol(root, true);
        check(umoStatus(root).at("browserLinks").get<bool>());
        check(get(command).find(L"--umo-nxm") != std::wstring::npos);
        auto backup = read(root / "_fetcher_launcher/umo/nxm-backup.json");
        umoProtocol(root, true);
        check(read(root / "_fetcher_launcher/umo/nxm-backup.json") == backup);
        umoProtocol(root, false);
        check(get(command) == L"old-manager.exe \"%1\"");
        check(get(nxm) == L"Old NXM description" &&
              get(nxm, L"URL Protocol") == L"old protocol value");
        check(get(L"Software\\Classes\\nxm\\DefaultIcon") == L"existing.ico");
        check(get(momw) == L"leave-momw-alone.exe \"%1\"");
        umoProtocol(root, true);
        put(command, nullptr, L"new-owner.exe \"%1\"");
        bool refused = false;
        try {
            umoProtocol(root, false);
        } catch (...) {
            refused = true;
        }
        check(refused && get(command) == L"new-owner.exe \"%1\"");
        umoProtocol(root, true);
        umoProtocol(root, false);
        check(get(command) == L"new-owner.exe \"%1\"");
        check(!fs::exists(root / "_fetcher_launcher/umo/nxm-backup.json"));
        std::cout << "PASS UMO protocol: opt-in registration, repeated enable, exact prior-value "
                     "restoration, unrelated values preserved, intervening owner protected "
                     "(isolated registry hive)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
