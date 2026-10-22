# Copyright (C) 2026 Fetcher contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
# This separate UMO adapter is distributed with its complete source and LICENSE.
# UMO's upstream distribution is retained unchanged. No extraction/actions run here.
import argparse
import ctypes
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import socket
import sys
import time


def atomic_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value), encoding="utf8")
    temporary.replace(path)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def credential(data, decrypt=False):
    # The UMO process alone sees the key. At rest it is bound to this Windows user.
    class Blob(ctypes.Structure):
        _fields_ = [("size", ctypes.c_ulong), ("data", ctypes.POINTER(ctypes.c_ubyte))]

    buffer = ctypes.create_string_buffer(data)
    source = Blob(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte)))
    target = Blob()
    library = ctypes.WinDLL("crypt32", use_last_error=True)
    library.CryptProtectData.argtypes = [ctypes.POINTER(Blob), ctypes.c_wchar_p,
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(Blob)]
    library.CryptUnprotectData.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p,
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(Blob)]
    local_free = ctypes.WinDLL("kernel32", use_last_error=True).LocalFree
    local_free.argtypes = [ctypes.c_void_p]
    local_free.restype = ctypes.c_void_p
    if decrypt:
        ok = library.CryptUnprotectData(ctypes.byref(source), None, None, None, None,
                                        1, ctypes.byref(target))
    else:
        ok = library.CryptProtectData(ctypes.byref(source), "Fetcher UMO", None,
                                      None, None, 1, ctypes.byref(target))
    if not ok:
        raise RuntimeError("credential")
    try:
        return ctypes.string_at(target.data, target.size)
    finally:
        local_free(ctypes.cast(target.data, ctypes.c_void_p))


def initialize(state):
    from config import Config
    Config.CACHE_DIR = str(state / "cache")
    Config.BASEPATH = str(state / "unused-install")
    Config.MODLIST = "fetcher"
    Config.NO_NOTIFICATIONS = True
    Config.DOWNLOAD_ONLY = True
    Config.MAX_THREADS = 1
    Config.VERBOSE = False
    # Never persist upstream exceptions, API responses, credentials or signed URLs.
    logging.disable(logging.CRITICAL)
    return Config


def connect(state):
    import sso
    import umonxm
    print("Fetcher uses UMO for Nexus sign-in. Authorise UMO in your browser.", flush=True)
    key = sso.get_nexus_key()
    details = umonxm.Nxm(key).user_details()
    if not details.get("user_id"):
        raise RuntimeError("auth")
    temporary = state / "credential.tmp"
    temporary.write_bytes(credential(key.encode("utf8")))
    temporary.replace(state / "credential.bin")
    atomic_json(state / "account.json", {"connected": True,
                "premium": bool(details.get("is_premium"))})
    print("Nexus connected through UMO. You may return to Fetcher.", flush=True)


def download(state, request):
    # Reconstruct a minimal descriptor; no server UMO actions, paths or code enter UMO.
    if set(request) != {"game", "modId", "fileId", "sha256", "browserLinks"}:
        raise RuntimeError("request")
    game, mod_id, file_id, sha = (request[k] for k in ("game", "modId", "fileId", "sha256"))
    if (not re.fullmatch(r"[a-z0-9-]{1,64}", game) or
        type(mod_id) is not int or mod_id < 1 or type(file_id) is not int or file_id < 1 or
        not re.fullmatch(r"[0-9a-f]{64}", sha) or type(request["browserLinks"]) is not bool):
        raise RuntimeError("request")
    name = "Fetcher" + sha
    archive = state / "cache" / "archive-cache" / name / ("Archive_" + sha + ".archive")
    if archive.is_file() and digest(archive) == sha:
        return
    if archive.exists():
        archive.unlink()
    if not (state / "credential.bin").is_file():
        raise RuntimeError("auth")
    import cache
    import dl
    import handlers
    from momw_types import DownloadInfo, ModDesc
    import umonxm
    Config = initialize(state)
    Config.NEXUS_API_KEY = credential((state / "credential.bin").read_bytes(), True).decode("utf8")
    Config.NEXUS_PREMIUM = bool(umonxm.Nxm(Config.NEXUS_API_KEY).user_details().get("is_premium"))
    if not Config.NEXUS_PREMIUM and not request["browserLinks"]:
        raise RuntimeError("browser-links")
    info = DownloadInfo(direct_download=None, file_name="Archive", extract_to=name,
                        nexus_file_id=file_id, pinned=True, actions=[])
    page = f"https://www.nexusmods.com/{game}/mods/{mod_id}"
    mod = ModDesc(name=name, author="", description="", url=page, category="Fetcher",
                  dl_url="", usage_notes="", compat=1, dir=name, slug=name,
                  date_added="2026-01-01T00:00:00", date_updated="2026-01-01T00:00:00",
                  download_info=[info], on_lists=["fetcher"], handler="nexus",
                  nexus_id=str(mod_id), nexus_game=game)
    cache.init()
    listener = None
    try:
        # The manifest's exact SHA-256, rather than a scraped checksum, remains authoritative.
        cache.cache_synced(mod, info, "generic_data",
                           cache.generic_data(sha, hash_algo="sha256", hash_digest=sha, url=page))
        cache.cache_installed(mod, info, "downloaded", False)
        if not Config.NEXUS_PREMIUM:
            listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            # Existing UMO NXM dispatchers forward here before loading their config.
            # Browsers can retain their old handler command for the current session.
            # Accept that standard handoff too, without reading the old UMO account/cache.
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            try:
                listener.bind(Config.SOCKET)
            except OSError:
                # A running UMO/other Fetcher copy owns it. Our native dispatcher uses
                # the pending file's explicit port and can still reach this instance.
                listener.bind(("127.0.0.1", 0))
            listener.listen(4)
            listener.settimeout(600)
            Config.SOCKET_SERVER = listener
            atomic_json(state / "pending.json", {"game": game, "modId": mod_id,
                        "fileId": file_id, "port": listener.getsockname()[1],
                        "expiresAt": int(time.time()) + 600})
            print("In the Nexus browser page, choose Mod manager download / Slow download.", flush=True)
        handler = handlers.NexusHandler()
        for attempt in range(2):
            info = handler.get_link(mod, {"file_id": file_id}, info, force_refresh=attempt > 0)
            try:
                dl.download(mod, info)
                break
            except dl.DownloadNeedsNewLink:
                if attempt:
                    raise
        if not archive.is_file() or digest(archive) != sha:
            raise RuntimeError("hash")
    finally:
        cache.close()
        if listener:
            listener.close()
        (state / "pending.json").unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("operation", choices=["connect", "download", "probe"])
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--request", type=Path)
    args = parser.parse_args()
    state = args.state.resolve()
    state.mkdir(parents=True, exist_ok=True)
    result = state / "result.json"
    result.unlink(missing_ok=True)
    try:
        initialize(state)
        if args.operation == "connect":
            connect(state)
        elif args.operation == "download":
            download(state, json.loads(args.request.read_text(encoding="utf8")))
        else:
            from config import Config
            import sso, handlers, dl, umonxm
            atomic_json(result, {"ok": True, "version": Config.APP_VER})
            return 0
        atomic_json(result, {"ok": True})
        return 0
    except BaseException as error:
        # An exception can contain API bodies or signed links. Return only fixed error codes.
        code = str(error) if isinstance(error, RuntimeError) else "failed"
        if code not in {"auth", "browser-links", "request", "hash", "credential"}:
            code = "failed"
        atomic_json(result, {"ok": False, "code": code})
        print("UMO could not finish. Return to Fetcher for retry/manual import.", flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
