#include "server_ping.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <thread>
#ifdef _WIN32
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
// clang-format on
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace FetcherNet
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        int tcpProbe(const std::string& host)
        {
            sockaddr_in v4{};
            sockaddr_in6 v6{};
            bool ipv4 = inet_pton(AF_INET, host.c_str(), &v4.sin_addr) == 1;
            if (!ipv4 && inet_pton(AF_INET6, host.c_str(), &v6.sin6_addr) != 1)
                return -1;
            v4.sin_family = AF_INET;
            v4.sin_port = htons(443);
            v6.sin6_family = AF_INET6;
            v6.sin6_port = htons(443);
            auto fd = socket(ipv4 ? AF_INET : AF_INET6, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
            if (fd == INVALID_SOCKET)
                return -1;
            u_long nonblocking = 1;
            if (ioctlsocket(fd, FIONBIO, &nonblocking) != 0)
            {
                closesocket(fd);
                return -1;
            }
#else
            if (fd < 0)
                return -1;
            if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
            {
                close(fd);
                return -1;
            }
#endif
            auto start = Clock::now();
            const auto connected = ipv4 ? connect(fd, reinterpret_cast<sockaddr*>(&v4), sizeof(v4))
                                        : connect(fd, reinterpret_cast<sockaddr*>(&v6), sizeof(v6));
            int result = -1;
            if (connected == 0)
                result = 0;
            else
            {
#ifdef _WIN32
                if (WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    fd_set writable, errors;
                    FD_ZERO(&writable);
                    FD_ZERO(&errors);
                    FD_SET(fd, &writable);
                    FD_SET(fd, &errors);
                    timeval timeout{ 0, 500000 };
                    if (select(0, nullptr, &writable, &errors, &timeout) > 0 && FD_ISSET(fd, &writable))
                    {
                        int error = 0, length = sizeof(error);
                        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == 0
                            && error == 0)
                            result = static_cast<int>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
                    }
                }
#else
                if (errno == EINPROGRESS)
                {
                    pollfd item{ fd, POLLOUT, 0 };
                    if (poll(&item, 1, 500) > 0)
                    {
                        int error = 0;
                        socklen_t length = sizeof(error);
                        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0)
                            result = static_cast<int>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
                    }
                }
#endif
            }
            linger abortive{ 1, 0 };
            setsockopt(fd, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&abortive), sizeof(abortive));
#ifdef _WIN32
            closesocket(fd);
#else
            close(fd);
#endif
            return result;
        }
        int probe(const std::string& host)
        {
            sockaddr_in v4{};
            sockaddr_in6 v6{};
            const bool ipv4 = inet_pton(AF_INET, host.c_str(), &v4.sin_addr) == 1;
            if (!ipv4 && inet_pton(AF_INET6, host.c_str(), &v6.sin6_addr) != 1)
                return -1;
            static std::atomic<unsigned> sequence{ 0 };
            const auto nonce = static_cast<std::uint64_t>(Clock::now().time_since_epoch().count()) ^ ++sequence;
#ifdef _WIN32
            const auto handle = ipv4 ? IcmpCreateFile() : Icmp6CreateFile();
            if (handle == INVALID_HANDLE_VALUE)
                return -1;
            std::array<unsigned char, 256> buffer{};
            DWORD replies = 0;
            if (ipv4)
                replies = IcmpSendEcho(handle, v4.sin_addr.s_addr, const_cast<std::uint64_t*>(&nonce), sizeof(nonce),
                    nullptr, buffer.data(), static_cast<DWORD>(buffer.size()), 500);
            else
            {
                sockaddr_in6 source{};
                source.sin6_family = AF_INET6;
                v6.sin6_family = AF_INET6;
                replies = Icmp6SendEcho2(handle, nullptr, nullptr, nullptr, &source, &v6,
                    const_cast<std::uint64_t*>(&nonce), sizeof(nonce), nullptr, buffer.data(),
                    static_cast<DWORD>(buffer.size()), 500);
            }
            int result = -1;
            if (replies)
            {
                if (ipv4)
                {
                    const auto& reply = *reinterpret_cast<const ICMP_ECHO_REPLY*>(buffer.data());
                    if (reply.Status == IP_SUCCESS && reply.Address == v4.sin_addr.s_addr
                        && reply.DataSize == sizeof(nonce) && reply.Data
                        && std::memcmp(reply.Data, &nonce, sizeof(nonce)) == 0)
                        result = static_cast<int>(reply.RoundTripTime);
                }
                else
                {
                    const auto& reply = *reinterpret_cast<const ICMPV6_ECHO_REPLY*>(buffer.data());
                    if (reply.Status == IP_SUCCESS && std::memcmp(reply.Address.sin6_addr, &v6.sin6_addr, 16) == 0)
                        result = static_cast<int>(reply.RoundTripTime);
                }
            }
            IcmpCloseHandle(handle);
            return result;
#else
            const int family = ipv4 ? AF_INET : AF_INET6;
            const int fd = socket(family, SOCK_DGRAM, ipv4 ? int(IPPROTO_ICMP) : int(IPPROTO_ICMPV6));
            if (fd < 0)
                return -1; // No elevated permissions or shell fallback.
            std::array<unsigned char, 16> packet{};
            packet[0] = ipv4 ? 8 : 128;
            packet[7] = 1;
            std::memcpy(packet.data() + 8, &nonce, sizeof(nonce));
            if (ipv4)
            {
                unsigned sum = 0;
                for (std::size_t i = 0; i < packet.size(); i += 2)
                    sum += (unsigned(packet[i]) << 8) | packet[i + 1];
                while (sum >> 16)
                    sum = (sum & 65535) + (sum >> 16);
                sum = ~sum;
                packet[2] = static_cast<unsigned char>(sum >> 8);
                packet[3] = static_cast<unsigned char>(sum);
            }
            v4.sin_family = AF_INET;
            v6.sin6_family = AF_INET6;
            const auto start = Clock::now();
            const auto sent = ipv4
                ? sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&v4), sizeof(v4))
                : sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&v6), sizeof(v6));
            int result = -1;
            while (sent == static_cast<decltype(sent)>(packet.size()))
            {
                const auto remaining
                    = 500 - std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
                if (remaining <= 0)
                    break;
                pollfd item{ fd, POLLIN, 0 };
                if (poll(&item, 1, static_cast<int>(remaining)) <= 0)
                    break;
                std::array<unsigned char, 256> response{};
                sockaddr_storage source{};
                socklen_t length = sizeof(source);
                const auto received
                    = recvfrom(fd, response.data(), response.size(), 0, reinterpret_cast<sockaddr*>(&source), &length);
                const bool matched = ipv4 ? source.ss_family == AF_INET
                        && std::memcmp(&reinterpret_cast<sockaddr_in*>(&source)->sin_addr, &v4.sin_addr, 4) == 0
                                          : source.ss_family == AF_INET6
                        && std::memcmp(&reinterpret_cast<sockaddr_in6*>(&source)->sin6_addr, &v6.sin6_addr, 16) == 0;
                if (matched && received >= 16 && response[0] == (ipv4 ? 0 : 129) && response[1] == 0 && response[6] == 0
                    && response[7] == 1 && std::memcmp(response.data() + 8, &nonce, sizeof(nonce)) == 0)
                {
                    result = static_cast<int>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
                    break;
                }
            }
            close(fd);
            return result;
#endif
        }
    }
    std::vector<ServerPingResult> measureServerPings(
        const std::vector<std::string>& hosts, const std::function<bool()>& cancelled)
    {
        std::vector<ServerPingResult> result(hosts.size());
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            return result;
#endif
        std::map<std::string, std::size_t> indices;
        std::vector<std::string> unique;
        for (const auto& host : hosts)
        {
            if (unique.size() >= 256 || host.size() > 64)
                continue;
            if (indices.emplace(host, unique.size()).second)
                unique.push_back(host);
        }
        std::vector<ServerPingResult> values(unique.size());
        std::atomic<std::size_t> next{ 0 };
        const auto deadline = Clock::now() + std::chrono::seconds(3);
        std::vector<std::thread> workers;
        auto work = [&] {
            while (Clock::now() < deadline && (!cancelled || !cancelled()))
            {
                auto index = next.fetch_add(1);
                if (index >= unique.size())
                    break;
                values[index].milliseconds = probe(unique[index]);
                if (values[index].milliseconds < 0 && (!cancelled || !cancelled()))
                {
                    values[index].milliseconds = tcpProbe(unique[index]);
                    values[index].tcp = values[index].milliseconds >= 0;
                }
            }
        };
        // Join workers even if creating another thread fails.
        try
        {
            for (std::size_t i = 0; i < std::min<std::size_t>(8, unique.size()); ++i)
                workers.emplace_back(work);
        }
        catch (...)
        {
        }
        for (auto& worker : workers)
            worker.join();
        for (std::size_t i = 0; i < hosts.size(); ++i)
        {
            auto found = indices.find(hosts[i]);
            if (found != indices.end())
                result[i] = values[found->second];
        }
#ifdef _WIN32
        WSACleanup();
#endif
        return result;
    }
}
