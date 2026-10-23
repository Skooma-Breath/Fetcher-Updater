"""Prepare a disposable GUI handoff fixture; never automates Windows UI or launches a game.

Use Computer Use to open its executable and press Install updates. Public release metadata
receipts deliberately mark the test client/tools current, preventing downloads/downgrades while
the transaction helper supplies verified replacement runtime files for the handoff test.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
from urllib.request import Request, urlopen


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("mode", choices=["seed", "stage", "corrupt", "verify"])
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--variant", default="V2")
    args = p.parse_args()
    root, build = args.root.resolve(), args.build.resolve()
    marker = root / "restart-fixture.json"
    if args.mode == "seed":
        assert not root.exists(), "Use a new test-owned root"
        root.mkdir(parents=True)
        saved = {"openmw.cfg": b"content=Personal.esp\n", "settings.cfg": b"personal settings\n",
                 "profiles/personal/key.bin": b"fake test key, not a credential"}
        for name, data in saved.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (root / "openmw.exe").write_bytes((build / "fetcher-launch-fixture.exe").read_bytes())
        managed = root / "_fetcher_launcher"
        managed.mkdir()
        for kind, repo, tag, asset in [
            ("tools", "Skooma-Breath/Fetcher-Updater", "fetcher-tester-tools", "fetcher-tester-tools.zip"),
            ("client", "Fetcher-Simulator/Fetcher-Simulator", "Fetcher-Simulator-Studio", "fetcher-simulator.zip")]:
            request = Request(f"https://api.github.com/repos/{repo}/releases/tags/{tag}",
                              headers={"User-Agent": "Fetcher-handoff-test"})
            release = json.load(urlopen(request, timeout=30))
            pinned = next(a["digest"].removeprefix("sha256:") for a in release["assets"] if a["name"] == asset)
            (managed / (kind + ".json")).write_text(json.dumps({"sha256": pinned}))
        (root / "fetcher-update-channel.json").write_text(json.dumps({
            "clientRepository": "Fetcher-Simulator/Fetcher-Simulator",
            "clientReleaseTag": "Fetcher-Simulator-Studio", "clientAssetName": "fetcher-simulator.zip"}))
        (root / "fetcher-client-files.json").write_text(json.dumps({"files": [{
            "path": "openmw.exe", "size": (root / "openmw.exe").stat().st_size,
            "sha256": digest((root / "openmw.exe").read_bytes())}]}))
        marker.write_text(json.dumps({"personal": {name: digest(data) for name, data in saved.items()}}))
        args.variant = "V1"
    else:
        assert marker.is_file(), "This is not a test-owned installation"
    if args.mode in ["seed", "stage"]:
        stage = root.parent / (root.name + "-" + args.variant)
        assert not stage.exists(), "Use a new fixture variant"
        stage.mkdir()
        files = [("FetcherLauncher.exe", (build / "FetcherLauncher.exe").read_bytes() +
                  (b"" if args.variant == "V1" else ("\nfixture " + args.variant).encode()))]
        for path in (build / "ui").rglob("*"):
            if path.is_file():
                data = path.read_bytes()
                if path.name == "index.html":
                    data = data.replace(b"Client &amp; profile updates", ("Client &amp; profile updates [" + args.variant + "]").encode())
                files.append((path.relative_to(build).as_posix(), data))
        records = []
        for name, data in files:
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            records.append({"path": name, "sha256": digest(data), "size": len(data)})
        (stage / "fetcher-tester-tools.json").write_text(json.dumps({
            "schemaVersion": 1, "launcherRestartHandshake": 1, "files": records}))
        subprocess.run([str(build / "fetcher-transaction-tests.exe"), "--install", str(root), str(stage), "tools"], check=True)
    elif args.mode == "corrupt":
        with open(root / "ui/index.html", "ab") as file:
            file.write(b"\n<!-- unverified test change -->")
    saved = json.loads(marker.read_text())["personal"]
    for name, expected in saved.items():
        assert digest((root / name).read_bytes()) == expected, name
    print("PASS fixture", args.mode, "personal files intact; launcher:", root / "FetcherLauncher.exe")


if __name__ == "__main__":
    main()
