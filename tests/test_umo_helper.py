"""Native UMO handoff checks. Optional pinned upstream ZIPs enable the real runtime test.

Does not sign into Nexus, change the NXM registry handler, or use an existing UMO configuration.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import socket
import struct
import subprocess
from tempfile import TemporaryDirectory
from threading import Thread
import time
import winreg
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from zipfile import ZipFile


def registry_snapshot():
    result = []
    for path, name in [(r"Software\Classes\nxm", ""),
                       (r"Software\Classes\nxm", "URL Protocol"),
                       (r"Software\Classes\nxm\shell\open\command", "")]:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
                result.append(winreg.QueryValueEx(key, name))
        except FileNotFoundError:
            result.append(None)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--upstream-archive", type=Path)
    parser.add_argument("--upstream-source", type=Path)
    args = parser.parse_args()
    exe = args.exe.resolve()
    before = registry_snapshot()
    count = 0
    with TemporaryDirectory(prefix="fetcher-umo-") as temporary:
        base = Path(temporary)
        root = base / "independent player"
        root.mkdir()
        state = root / "_fetcher_launcher" / "umo"
        state.mkdir(parents=True)
        def command(value, ok=True):
            nonlocal count
            count += 1
            request, result = base / "request.json", base / "result.json"
            request.write_text(json.dumps(value), encoding="utf8")
            process = subprocess.run([str(exe), "--install-root", str(root),
                "--request-file", str(request), "--log-file", str(result)], timeout=120)
            response = json.loads(result.read_text(encoding="utf8"))
            assert (process.returncode == 0) == ok, (value, response)
            return response
        status = command({"op": "init"})
        assert status["nexusProvider"] == "umo" and not status["umo"]["connected"]
        assert not status["umo"]["installed"]
        command({"op": "umoConnect"}, False)
        command({"op": "nexusProvider", "provider": "arbitrary-app-id"}, False)
        command({"op": "umoProtocol", "enable": True}, False)
        command({"op": "umoForget"})
        assert command({"op": "nexusKey", "key": "private-test-placeholder"})["nexusProvider"] == "personal-testing"
        assert command({"op": "nexusProvider", "provider": "umo"})["nexusProvider"] == "umo"
        command({"op": "nexusKey", "key": ""})
        # Browser callbacks bypass the installation lock and forward only the active tuple.
        expiry = int(time.time()) + 3600
        good = f"nxm://morrowind/mods/56939/files/1000064599?user_id=42&expires={expiry}&key=fixture-token"
        pending = {"game": "morrowind", "modId": 56939, "fileId": 1000064599,
                   "expiresAt": int(time.time()) + 600, "port": 1}
        def callback(uri, ok=False):
            nonlocal count
            count += 1
            result = subprocess.run([str(exe), "--install-root", str(root), "--umo-nxm", uri], timeout=10)
            assert (result.returncode == 0) == ok, "Callback incorrectly accepted or rejected"
            audit = (state / "callback-result.json").read_text()
            assert "fixture-token" not in audit and '"query"' not in audit and '"key"' not in audit
        for uri in ["not-a-link", good, good.replace("56939", "56938"), good + "&key=second",
                    good + "&unexpected=anything", good.replace("nxm://", "https://"),
                    good.replace("fixture-token", 'bad"token'), good.replace("user_id=42", "user_id=NaN")]:
            (state / "pending.json").write_text(json.dumps(pending))
            callback(uri)
        pending["expiresAt"] = int(time.time()) - 1
        (state / "pending.json").write_text(json.dumps(pending))
        callback(good)
        pending["expiresAt"] = int(time.time()) + 600
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(10)
            pending["port"] = listener.getsockname()[1]
            (state / "pending.json").write_text(json.dumps(pending))
            received = []
            def receive():
                with listener.accept()[0] as client:
                    message = b""
                    while chunk := client.recv(37):
                        message += chunk
                    received.append(message.decode())
                    client.sendall(b"OK")
            thread = Thread(target=receive)
            thread.start()
            callback(good, True)
            thread.join(10)
            assert received == [f"nxm://morrowind/mods/56939/files/1000064599?key=fixture-token&expires={expiry}&user_id=42"]
        assert not (state / "credential.bin").exists()
        (state / "pending.json").unlink()
        # Actual Windows DPAPI in the adapter, without accessing an account or network.
        spec = importlib.util.spec_from_file_location("fetcher_umo_adapter", Path(__file__).resolve().parents[1] / "launcher/umo/adapter.py")
        adapter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(adapter)
        protected = adapter.credential(b"fake-umo-secret")
        assert b"fake-umo-secret" not in protected
        assert adapter.credential(protected, True) == b"fake-umo-secret"
        if args.upstream_archive or args.upstream_source:
            assert args.upstream_archive and args.upstream_source
            shutil.copyfile(args.upstream_archive, state / "umo-win.zip")
            shutil.copyfile(args.upstream_source, state / "upstream-source.zip")
            installed = command({"op": "umoInstall"})
            assert installed["umo"]["installed"] and not installed["umo"]["connected"]
            tool = state / "tool-0.11.2"
            assert json.loads((state / "result.json").read_text())["version"] == "0.11.2"
            assert (tool / "adapter.py").read_bytes() == Path(spec.origin).read_bytes()
            assert (tool / "upstream-source.zip").read_bytes() == args.upstream_source.read_bytes()
            with ZipFile(args.upstream_archive) as archive:
                names = [name for name in archive.namelist() if not name.endswith("/")]
                for name in names:
                    assert (tool / "upstream" / name).read_bytes() == archive.read(name), name
            # A real helper process returns a cached exact archive, then native preparation
            # verifies/extracts it. No UMO auth key, extraction tool or mod actions are needed.
            archive = base / "sample.zip"
            plugin = b"TES3" + struct.pack("<III", 0, 0, 0)
            with ZipFile(archive, "w") as zipped:
                zipped.writestr("Data Files/Test.esp", plugin)
            sha = hashlib.sha256(archive.read_bytes()).hexdigest()
            class HttpFixture(SimpleHTTPRequestHandler):
                def __init__(self, *a, **kw):
                    super().__init__(*a, directory=str(base), **kw)
                def log_message(self, *_): pass
            http = ThreadingHTTPServer(("127.0.0.1", 0), HttpFixture)
            http_thread = Thread(target=http.serve_forever, daemon=True)
            http_thread.start()
            try:
                for premium in [True, False, "legacy"]:
                    fixture_root = base / ("legacy" if premium == "legacy" else "premium" if premium else "free")
                    fixture_state = fixture_root / "_fetcher_launcher/umo"
                    process = subprocess.Popen([str(tool / "upstream/umo/python.exe"), "-I", "-B",
                        str(Path(__file__).with_name("umo_runtime_fixture.py").resolve()),
                        str(tool / "adapter.py"), str(fixture_state),
                        f"http://127.0.0.1:{http.server_port}/sample.zip", sha,
                        "yes" if premium is True else "no"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    try:
                        if premium is not True:
                            deadline = time.monotonic() + 30
                            while not (fixture_state / "browser.json").exists() and process.poll() is None and time.monotonic() < deadline:
                                time.sleep(0.05)
                            assert (fixture_state / "pending.json").exists(), process.communicate(timeout=5)
                            assert "file_id=1000064599" in json.loads((fixture_state / "browser.json").read_text())["url"]
                            if premium == "legacy":
                                pending = json.loads((fixture_state / "pending.json").read_text())
                                forward_command = [str(tool / "upstream/umo/python.exe"), "-I", "-B",
                                    str(Path(__file__).with_name("umo_legacy_forward_fixture.py").resolve()),
                                    str(pending["port"]), f"nxm://morrowind/mods/56939/files/1000064599?key=fixture-token&expires={expiry}&user_id=42"]
                            else:
                                forward_command = [str(exe), "--install-root", str(fixture_root), "--umo-nxm", good]
                            forwarded = subprocess.run(forward_command, timeout=10, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                            assert forwarded.returncode == 0
                        output, errors = process.communicate(timeout=60)
                        assert process.returncode == 0, (output.decode(errors="replace"), errors.decode(errors="replace"))
                        assert json.loads((fixture_state / "fixture-result.json").read_text())["ok"]
                        downloaded = fixture_state / "cache/archive-cache" / ("Fetcher" + sha) / ("Archive_" + sha + ".archive")
                        assert downloaded.read_bytes() == archive.read_bytes()
                        assert b"umo-fixture-secret" not in (fixture_state / "credential.bin").read_bytes()
                        assert not (fixture_state / "pending.json").exists()
                    finally:
                        if process.poll() is None: process.kill(); process.wait()
            finally:
                http.shutdown(); http.server_close(); http_thread.join()
            cached = state / "cache" / "archive-cache" / ("Fetcher" + sha) / ("Archive_" + sha + ".archive")
            cached.parent.mkdir(parents=True)
            shutil.copyfile(archive, cached)
            data = base / "owned data"
            data.mkdir()
            for name in ["Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"]:
                (data / name).write_bytes(plugin)
            catalog = {"schemaVersion": 1, "profiles": [{"id": "umo-test", "name": "UMO test", "mode": "managed",
                "content": ["Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm", "Test.esp"],
                "packages": [{"id": "one", "nexus": {"game": "morrowind", "modId": 56939, "fileId": 1000064599},
                              "sha256": sha, "dataDirs": ["Data Files"]}]}]}
            (root / "fetcher-launcher-profiles.json").write_text(json.dumps(catalog))
            command({"op": "gameData", "path": str(data)})
            command({"op": "profile", "id": "umo-test"})
            command({"op": "prepare"})
            assert (root / "_fetcher_launcher/downloads" / (sha + ".zip")).read_bytes() == archive.read_bytes()
            assert (root / "_fetcher_launcher/packages" / sha / "Data Files/Test.esp").read_bytes() == plugin
            shutil.copyfile(exe, root / "FetcherLauncher.exe")
            registry_fixture = exe.with_name("fetcher-umo-registry-fixture.exe")
            if registry_fixture.is_file():
                subprocess.run([str(registry_fixture), str(root)], check=True, timeout=60)
            # A changed runtime must not run, even with a valid cached Nexus archive.
            (tool / "upstream/umo/sso.py").write_text("raise RuntimeError('changed helper')")
            command({"op": "umoConnect"}, False)
            assert not (state / "credential.bin").exists()
            print(f"PASS real upstream UMO 0.11.2: all {len(names)} distribution files unchanged; Premium/free fixture downloads, native callback, cached handoff, native install, tampered runtime refused")
        assert registry_snapshot() == before, "UMO tests changed the existing protocol handler"
        print(f"PASS UMO handoff: {count} native process operations; default provider, exact-file callbacks, malformed/stale links, DPAPI, existing handler preserved")


if __name__ == "__main__":
    main()
