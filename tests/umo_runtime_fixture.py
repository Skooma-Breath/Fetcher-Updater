"""Test harness only: exercise the shipped UMO modules against a loopback archive.

Run with upstream's embedded Python. Nexus account calls are replaced here, never in production.
The real UMO handler, token handoff, downloader, hash checks and adapter credential storage run.
"""
import importlib.util
import json
from pathlib import Path
import sys
import socket

adapter_path, state_path, url, sha, premium = sys.argv[1:]
state = Path(state_path)
state.mkdir(parents=True)
spec = importlib.util.spec_from_file_location("fetcher_adapter", adapter_path)
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)
import sso
import umonxm
import helper


class NexusFixture:
    def __init__(self, key):
        assert key == "umo-fixture-secret"

    def user_details(self):
        return {"user_id": 42, "is_premium": premium == "yes"}

    def mod_file_download_link(self, game, mod, file, key=None, expires=None):
        assert (game, str(mod), int(file)) == ("morrowind", "56939", 1000064599)
        if premium != "yes":
            assert key == "fixture-token" and expires.isdigit()
        return [{"URI": url}]


sso.get_nexus_key = lambda: "umo-fixture-secret"
umonxm.Nxm = NexusFixture
helper.open_url = lambda page: adapter.atomic_json(state / "browser.json", {"url": page})
config = adapter.initialize(state)
# Keep automated tests off the real UMO IPC port; production prefers port 6666.
config.SOCKET = ("127.0.0.1", 0)
occupied = None
if premium == "no":
    occupied = socket.socket()
    occupied.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    occupied.bind(("127.0.0.1", 0))
    # Exercise fallback without occupying the user's actual standard UMO port.
    config.SOCKET = occupied.getsockname()
    original_open = helper.open_url
    def check_fallback(page):
        pending = json.loads((state / "pending.json").read_text())
        assert pending["port"] != occupied.getsockname()[1]
        original_open(page)
    helper.open_url = check_fallback
adapter.connect(state)
adapter.download(state, {"game": "morrowind", "modId": 56939, "fileId": 1000064599,
                         "sha256": sha, "browserLinks": premium != "yes"})
adapter.atomic_json(state / "fixture-result.json", {"ok": True})
if occupied:
    occupied.close()
