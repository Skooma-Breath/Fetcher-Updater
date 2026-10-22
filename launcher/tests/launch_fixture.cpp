// Test-only process: records a real CreateProcess handoff without starting the game.
#include <nlohmann/json.hpp>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
namespace fs = std::filesystem;
int wmain(int count, wchar_t **arguments) {
    nlohmann::json result = {{"pid", GetCurrentProcessId()},
                             {"cwd", fs::current_path().u8string()},
                             {"arguments", nlohmann::json::array()}};
    for (int i = 1; i < count; ++i)
        result["arguments"].push_back(fs::path(arguments[i]).u8string());
    std::ofstream(fs::current_path() / "launch-capture.json") << result.dump(2);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (fs::exists(fs::current_path() / "hold-process") &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    return 0;
}
