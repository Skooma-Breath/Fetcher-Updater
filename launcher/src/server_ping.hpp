#pragma once
#include <functional>
#include <string>
#include <vector>
namespace FetcherNet
{
    // Numeric server addresses only. Host RTT, not HTTP time or a game connection.
    // -1 means unavailable. HTTPS-port TCP handshake fallback sends no application
    // data.
    // Deduplicated, eight workers, 500 ms/method, 3 s queue budget (4 s maximum).
    struct ServerPingResult
    {
        int milliseconds = -1;
        bool tcp = false;
    };
    std::vector<ServerPingResult> measureServerPings(
        const std::vector<std::string>& hosts, const std::function<bool()>& cancelled = {});
}
