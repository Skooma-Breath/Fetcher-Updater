#include "native_backend.hpp"
#include "install_transaction.hpp"
#include <windows.h>
#include <iostream>
using namespace fetcher;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, const char *message) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, message);
}
void inventory(const fs::path &stage, bool tools, const std::vector<std::string> &files) {
    Json records = Json::array();
    for (const auto &name : files)
        records.push_back({{"path", name},
                           {"size", fs::file_size(stage / name)},
                           {"sha256", sha256(stage / name)}});
    writeAtomic(stage / (tools ? "fetcher-tester-tools.json" : "fetcher-client-files.json"),
                Json{{"schemaVersion", 1}, {"files", records}}.dump());
}
void seed(const fs::path &root, const fs::path &stage, bool tools) {
    writeAtomic(root / "openmw.cfg", "personal configuration");
    writeAtomic(root / "settings.cfg", "personal settings");
    writeAtomic(root / "userdata/save.omwsave", "personal save");
    auto exe = tools ? "FetcherLauncher.exe" : "openmw.exe";
    writeAtomic(root / exe, "old executable");
    writeAtomic(stage / exe, "new executable");
    if (tools) {
        writeAtomic(root / "ui/index.html", "old interface");
        writeAtomic(stage / "ui/index.html", "new interface");
        writeAtomic(stage / "ui/assets/fetcher-float.gif", "new asset");
        inventory(stage, true, {exe, "ui/index.html", "ui/assets/fetcher-float.gif"});
    } else {
        writeAtomic(root / "old-client.dat", "old resource");
        writeAtomic(stage / "new-client.dat", "new resource");
        inventory(root, false, {exe, "old-client.dat"});
        inventory(stage, false, {exe, "new-client.dat"});
    }
}
void preserved(const fs::path &root) {
    check(read(root / "openmw.cfg") == "personal configuration" &&
              read(root / "settings.cfg") == "personal settings" &&
              read(root / "userdata/save.omwsave") == "personal save",
          "personal files preserved");
}
void crash(const fs::path &exe, const fs::path &root, const fs::path &stage, bool tools,
           int point) {
    auto command = L"\"" + exe.wstring() + L"\" --crash \"" + root.wstring() + L"\" \"" +
                   stage.wstring() + L"\" " + (tools ? L"tools" : L"client") + L" " +
                   std::to_wstring(point);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                         nullptr, nullptr, &startup, &process) != 0,
          "start interrupted-update child");
    auto waited = WaitForSingleObject(process.hProcess, 10000);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    if (waited != WAIT_OBJECT_0)
        TerminateProcess(process.hProcess, 99);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    check(waited == WAIT_OBJECT_0 && code == 87,
          "child terminated at an actual update write boundary");
}
}
int wmain(int argc, wchar_t **argv) {
    if (argc == 5 && std::wstring(argv[1]) == L"--install") {
        installTransaction(argv[2], argv[3], Json::object(), std::wstring(argv[4]) == L"tools");
        return 0;
    }
    if (argc == 6 && std::wstring(argv[1]) == L"--crash") {
        fixtureInstallCrashAfter(std::stoi(argv[5]));
        installTransaction(argv[2], argv[3], Json::object(), std::wstring(argv[4]) == L"tools");
        return 3;
    }
    auto root = fs::temp_directory_path() /
                (L"fetcher-transaction-tests-" + std::to_wstring(GetCurrentProcessId()));
    try {
        wchar_t buffer[32768];
        GetModuleFileNameW(nullptr, buffer, 32768);
        fs::path exe = buffer;
        for (bool tools : {true, false})
            for (int point : {1, 2, 3}) {
                auto test =
                    root / (std::string(tools ? "tools-" : "client-") + std::to_string(point));
                auto install = test / "install", stage = test / "stage";
                seed(install, stage, tools);
                auto manifest = tools ? "fetcher-tester-tools.json" : "fetcher-client-files.json";
                auto before = fs::exists(install / manifest) ? read(install / manifest) : "";
                crash(exe, install, stage, tools, point);
                check(read(install / (tools ? "FetcherLauncher.exe" : "openmw.exe")) ==
                          "new executable",
                      "partial new files exist before restart");
                Backend recovered(install);
                check(read(install / (tools ? "FetcherLauncher.exe" : "openmw.exe")) ==
                          "old executable",
                      "restart restores executable");
                if (tools)
                    check(read(install / "ui/index.html") == "old interface" &&
                              !fs::exists(install / "ui/assets/fetcher-float.gif"),
                          "restart restores UI/removes added asset");
                else
                    check(read(install / "old-client.dat") == "old resource" &&
                              !fs::exists(install / "new-client.dat"),
                          "restart restores deleted resources");
                check(before.empty() ? !fs::exists(install / manifest)
                                     : read(install / manifest) == before,
                      "restart restores original inventory");
                check(recoverInstallTransactions(install).empty(), "recovery is idempotent");
                preserved(install);
            }
        auto install = root / "manual/install", stage = root / "manual/stage";
        seed(install, stage, true);
        Backend::installTools(install, stage);
        Backend backend(install);
        auto snapshots = backend.state().at("updateBackups");
        check(snapshots.size() == 1, "completed update visible in rollback list");
        auto id = snapshots[0].at("id").get<std::string>();
        auto backup = install / "_fetcher_launcher/rollback" / id;
        writeAtomic(install / "ui/index.html", "local edit after update");
        rejects([&] { restoreInstallBackup(install, id); },
                "rollback refuses post-update local edits");
        check(read(install / "FetcherLauncher.exe") == "new executable" &&
                  read(install / "ui/index.html") == "local edit after update",
              "failed rollback changes nothing");
        writeAtomic(install / "ui/index.html", "new interface");
        auto saved = read(backup / "files/FetcherLauncher.exe");
        writeAtomic(backup / "files/FetcherLauncher.exe", "corrupt backup");
        rejects([&] { restoreInstallBackup(install, id); },
                "corrupt backup rejected before restore");
        check(read(install / "ui/index.html") == "new interface",
              "corrupt backup does not partially restore other files");
        writeAtomic(backup / "files/FetcherLauncher.exe", saved);
        backend.command({{"op", "rollbackUpdate"}, {"id", id}});
        check(read(install / "FetcherLauncher.exe") == "old executable" &&
                  read(install / "ui/index.html") == "old interface",
              "operator rollback restores old launcher/UI");
        check(backend.state().at("updateBackups").empty(),
              "restored backup cannot be applied twice");
        preserved(install);
        rejects([&] { restoreInstallBackup(install, "../outside"); },
                "rollback path traversal rejected");
        // An interrupted restore is safe to resume from a mixture of old and new bytes.
        auto resume = root / "resume/install", resumeStage = root / "resume/stage";
        seed(resume, resumeStage, true);
        Backend::installTools(resume, resumeStage);
        auto resumeId = listInstallBackups(resume)[0].at("id").get<std::string>();
        auto resumeBackup = resume / "_fetcher_launcher/rollback" / resumeId;
        writeAtomic(resume / "FetcherLauncher.exe", "old executable");
        writeAtomic(resumeBackup / "restoring.json", "{}");
        Backend restarted(resume);
        check(read(resume / "ui/index.html") == "old interface", "interrupted rollback resumes");
        preserved(resume);
        removeManaged(fs::temp_directory_path(), root);
        std::cout << "PASS update transactions: " << checks << " assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
