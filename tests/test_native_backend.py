"""Exercise the real Windows native executable against a local package server."""
from pathlib import Path
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from tempfile import TemporaryDirectory
from threading import Thread
from zipfile import ZipFile, ZipInfo
import argparse
import hashlib
import json
import struct
import subprocess


def plugin(masters=()):
    data = b"".join(b"MAST" + struct.pack("<I", len(m) + 1) + m.encode() + b"\0" for m in masters)
    return b"TES3" + struct.pack("<III", len(data), 0, 0) + data


def main():
    args = argparse.ArgumentParser()
    args.add_argument("--exe", type=Path, required=True)
    args.add_argument("--bundle", type=Path)
    options = args.parse_args()
    exe = options.exe.resolve()
    with TemporaryDirectory(prefix="fetcher-native-integration-") as temporary:
        base = Path(temporary)
        root = base / "inställ with spaces"
        root.mkdir()
        data = base / "owned-game"
        data.mkdir()
        for name in ["Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"]:
            (data / name).write_bytes(plugin())
        original = b"# Personal configuration\ncontent=KeepPersonal.esp\n"
        (root / "openmw.cfg").write_bytes(original)
        (root / "profiles").mkdir()
        (root / "profiles/keep.bin").write_bytes(b"character settings")
        archive = base / "mod.zip"
        with ZipFile(archive, "w") as z:
            z.writestr("openmw-client-package.json", json.dumps({"dataDirs": ["./Data Files"]}))
            z.writestr("Data Files/Test.esp", plugin(["Morrowind.esm"]))
            z.writestr("Data Files/meshes/test.nif", b"test model")
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()

        class Handler(SimpleHTTPRequestHandler):
            requests = 0

            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=str(base), **kwargs)

            def do_GET(self):
                type(self).requests += 1
                super().do_GET()

            def log_message(self, *_):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = Thread(target=server.serve_forever, daemon=True)
        worker.start()
        url = f"http://127.0.0.1:{server.server_port}"
        installed = {"id": "installed", "name": "Existing", "mode": "installed", "content": [], "packages": []}
        profile = {"id": "fixture", "name": "Native integration", "mode": "managed", "content": ["Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm", "Test.esp"], "requiredFiles": ["meshes/test.nif"], "packages": [{"id": "content", "url": url + "/mod.zip", "sha256": digest}]}
        catalog = {"schemaVersion": 1, "profiles": [installed, profile]}
        (root / "fetcher-launcher-profiles.json").write_text(json.dumps(catalog))

        def command(value, success=True):
            (base / "request.json").write_text(json.dumps(value))
            result = subprocess.run([str(exe), "--install-root", str(root), "--request-file", str(base / "request.json"), "--log-file", str(base / "result.json")], timeout=60)
            response = json.loads((base / "result.json").read_text())
            assert (result.returncode == 0) == success, response
            return response

        try:
            command({"op": "gameData", "path": str(data)})
            command({"op": "profile", "id": "fixture"})
            command({"op": "prepare"})
            config = root / "_fetcher_launcher/profiles/fixture/openmw.cfg"
            text = config.read_text()
            assert 'replace=config' in text and text.index("content=Morrowind.esm") < text.index("content=Test.esp")
            assert all("replace=" + key in text for key in ["data", "content", "groundcover", "fallback-archive"])
            assert "data-local=" in text and (config.parent/"local-data").is_dir()
            assert (root / "openmw.cfg").read_bytes() == original
            requests = Handler.requests
            command({"op": "prepare"})
            assert Handler.requests == requests, "Verified package cache should not download again"
            cached = root / "_fetcher_launcher/packages" / digest / "Data Files/meshes/test.nif"
            cached.write_bytes(b"corrupted")
            command({"op": "prepare"})
            assert cached.read_bytes() == b"test model", "Corrupt package cache was not repaired"
            command({"op": "favorite", "host": "community.example", "port": 25565})
            assert command({"op": "init"})["favorites"] == ["community.example:25565"]
            before = config.read_bytes()
            for case in ["traversal", "duplicate", "symlink", "hash"]:
                bad = base / (case + ".zip")
                with ZipFile(bad, "w") as z:
                    if case == "traversal":
                        z.writestr("../escaped", "no")
                    elif case == "duplicate":
                        z.writestr("x", "one")
                        z.writestr("X", "two")
                    elif case == "symlink":
                        info = ZipInfo("link")
                        info.create_system = 3
                        info.external_attr = 0o120777 << 16
                        z.writestr(info, "../escaped")
                    else:
                        z.writestr("anything", "bad digest")
                profile["packages"][0] = {"id": "content", "url": url + "/" + bad.name, "sha256": "0" * 64 if case == "hash" else hashlib.sha256(bad.read_bytes()).hexdigest()}
                (root / "fetcher-launcher-profiles.json").write_text(json.dumps(catalog))
                command({"op": "prepare"}, success=False)
                assert config.read_bytes() == before and not (root / "_fetcher_launcher/escaped").exists()
            if options.bundle:
                candidate = base / "candidate.zip"
                candidate.write_bytes(options.bundle.read_bytes())
                with ZipFile(candidate) as z:
                    manifest = json.loads(z.read("openmw-client-package.json").decode("utf-8-sig"))
                profile["content"] = manifest["content"]
                profile["requiredFiles"] = ["meshes/fetcher/synth/keyboard-stand.nif", "icons/fetcher/synth/keyboard.dds", "FetcherStudio/ATTRIBUTION.txt", "FetcherVehicles/CREDITS.txt"]
                profile["packages"][0] = {"id": "content", "url": url + "/candidate.zip", "sha256": hashlib.sha256(candidate.read_bytes()).hexdigest()}
                (root / "fetcher-launcher-profiles.json").write_text(json.dumps(catalog))
                command({"op": "prepare"})
                assert "content=FetcherVehicles.omwaddon" in config.read_text()
                print("PASS real surf/truck/Studio bundle: native download, hash, extraction, assets and plugin load order")
            assert (root / "profiles/keep.bin").read_bytes() == b"character settings"
            assert (root / "openmw.cfg").read_bytes() == original
            print("PASS native integration: Unicode/spaced paths, profile persistence, isolated config, cache repair, bad hashes, traversal/duplicate/symlink rejection, original data preserved")
        finally:
            server.shutdown()
            server.server_close()
            worker.join()


if __name__ == "__main__":
    main()
