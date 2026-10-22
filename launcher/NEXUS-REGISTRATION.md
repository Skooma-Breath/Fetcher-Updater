# Nexus Mods application registration

For current local/private testing, use a Nexus Personal API Key. Registration and SSO are deferred
until public rollout, as requested on 2026-10-22. Generate a personal key on Nexus's API Access
page, then paste it into Fetcher Launcher under Mods > Nexus API key > Save key locally. Do not
paste it into chat or put it in a shared server manifest. Native Fetcher identifies its API requests
as Fetcher Launcher; it does not borrow UMO's application identity. Download eligibility remains
controlled by Nexus. This testing route is already implemented; no SSO slug is needed to use it.

API Access: https://next.nexusmods.com/settings/api-keys

The registration steps below remain the public-release plan, not a blocker for local testing.

Nexus staff issue the SSO application slug. This is not a self-service account API key.
The operator sends the request; the launcher never impersonates UMO or another registered app.

Official registration process:
https://help.nexusmods.com/article/114-api-acceptable-use-policy

Official SSO protocol example:
https://github.com/Nexus-Mods/sso-integration-demo

1. Email support@nexusmods.com from the address associated with the operator's Nexus account.
2. Attach the local Fetcher-Launcher-Nexus-Review.zip, including its test build, logo, source
   snapshot and review instructions. It is a private review candidate, not a stable release.
3. Supply the application name Fetcher Launcher and the description below. Answer staff's
   review questions and make requested changes before public release.
4. When approved, record the issued public application slug for the SSO integration. Do not
   send personal API keys, passwords or client secrets in chat or include them in the archive.

Suggested subject: API/SSO application registration — Fetcher Launcher

Suggested message:

Hello, I'd like to register Fetcher Launcher for Nexus Mods API access and SSO. It is a C++
Windows launcher and mod manager for OpenMW multiplayer servers. It installs server-defined
mod profiles, orders plugins, and supports Nexus downloads and exact-file manual imports.

A testing build, logo, source snapshot and instructions are attached. It currently supports
personal API keys for testing, respects download eligibility, and stores keys locally using
Windows protection. We would like a dedicated application slug for browser SSO.

Source: https://github.com/Skooma-Breath/Fetcher-Updater

Please let me know what else you need for review.

## Review instructions included with the test build

Windows x64 and Microsoft Edge WebView2 Runtime are required. Extract to a new writable folder
and run FetcherLauncher.exe. This build has no registered SSO slug and does not register NXM.
The source snapshot identifies the exact review build; the public repository may lag the candidate.

Under Mods, enter a personal test API key and click Save key locally. This stores the key for
subsequent user-initiated Nexus requests. Forget key removes this launcher's stored key.
Keys are DPAPI-protected for the current Windows account; no key is bundled or sent to Fetcher.

For a pinned Nexus-file test, use the included fetcher-vanilla-hackle-lo.json in the manifest
review/import controls. Review and accept it explicitly. It pins morrowind mod 56939, file
1000064599 (Hackle-Lo Pipes v4), SHA-256
a4211504a66587218c3ffd830a680ef33c0654110afbb503fb7d91b61e945970.
This older file is pinned for repeatability; eligibility and availability are determined by Nexus.

With owned Morrowind, Tribunal and Bloodmoon Data Files selected under Mods, Prepare profile
requests the pinned download link. Ineligible accounts receive the manual-download/import route;
the application does not bypass Nexus restrictions. Open Nexus file opens the system browser.
Import archive requires the exact pinned SHA-256. Do not use Launch to join a public server with
this content-test profile; no matching public-server rollout is part of this review.

Nexus credentials are sent only to api.nexusmods.com. API redirects are refused and archive/CDN
requests do not carry the API key. The application sends its own Application-Name and version
headers. No background scraping, account endorsement/tracking mutation or remote credential
storage is implemented. Mod scripts/executables cannot be invoked as installer actions.
