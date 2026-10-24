# Server browser ping

The launcher and in-game browser display locally measured server-address network round-trip
time, with sorting and unavailable readings shown as `—`. Both try ICMP echo first, then a TCP
handshake on port 443 if echo is blocked. Each successful reading identifies its method.
The TCP fallback sends no HTTP request, credentials or application data. Neither method opens
a game connection, adds a player or uses the master service's HTTP response time.

This is host network latency, not game tick/processing latency. Servers sharing an address
share a measurement; their actual gameplay latency can differ. If both echo and TCP 443 are
unavailable, no latency is invented. Server owners do not need an engine update for this feature.

Probes run off the UI thread and accept numeric addresses only, without hostname resolution.
An address is probed once per refresh, with at most eight workers and 256 unique addresses.
Each method has a 500 ms timeout and the queue has a three-second budget; outstanding probes
can take the total to about four seconds. In-game refresh generations discard stale results
and retain the user's selected server when measurements update the sort order.

The shared implementation is mirrored in the engine's `components/openmw-mp/ServerPing.*`.
Windows uses the native ICMP API; Linux uses unprivileged datagram echo sockets, falling back
when unavailable. It never invokes a shell or requests elevated network privileges.

## Acceptance — 2026-10-24

Windows loopback, deduplication, invalid-address and cancellation tests pass. Unprivileged Linux
IPv4/IPv6 loopback checks pass. Both live Fetcher VPS entries returned TCP fallback readings
around 6 ms from the operator's machine; its network blocks ICMP. This number varies with the
player's location and network. The engine's 15 master/browser protocol tests include ascending
and descending ping sorting with unavailable values always last. Both Windows builds pass.
