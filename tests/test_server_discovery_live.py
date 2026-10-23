"""Opt-in: real master discovery and fresh pinned content preparation; no game launch."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--game-data", type=Path, required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    if root.exists():
        raise SystemExit("Use a new test-owned directory; existing caches must not mask downloads.")
    root.mkdir(parents=True)
    original = b"# Personal configuration must remain intact\n"
    (root / "openmw.cfg").write_bytes(original)

    def command(request):
        request_file, result_file = root / "request.json", root / "result.json"
        request_file.write_text(json.dumps(request), encoding="utf8")
        result = subprocess.run([str(args.exe.resolve()), "--install-root", str(root),
                                 "--request-file", str(request_file), "--log-file", str(result_file)],
                                timeout=180)
        value = json.loads(result_file.read_text(encoding="utf8"))
        assert result.returncode == 0, value
        return value

    command({"op": "gameData", "path": str(args.game_data.resolve())})
    selected = command({"op": "selectServer", "host": args.host, "port": args.port})
    profile = next(p for p in selected["profiles"] if p["id"] == selected["selectedProfile"])
    assert profile["_discovered"] is True
    assert selected["host"] == args.host and selected["port"] == args.port
    assert not selected["profileReview"], "Automatic discovery introduced a manual review step"
    managed = root / "_fetcher_launcher"
    assert not (managed / "downloads").exists(), "Selection downloaded archives"
    command({"op": "prepare"})
    config = managed / "profiles" / profile["id"] / "openmw.cfg"
    plugins = [line.removeprefix("content=") for line in config.read_text().splitlines()
               if line.startswith("content=")]
    assert plugins == profile["content"]
    archives = []
    for package in profile["packages"]:
        digest = package["sha256"].lower()
        archive = managed / "downloads" / (digest + ".zip")
        assert hashlib.sha256(archive.read_bytes()).hexdigest() == digest
        receipt = json.loads((managed / "packages" / digest / ".fetcher-package.json").read_text())
        assert receipt["sha256"] == digest
        archives.append({"sha256": digest, "bytes": archive.stat().st_size,
                         "files": len(receipt["files"])})
    assert (root / "openmw.cfg").read_bytes() == original
    report = {"profile": profile["id"], "manifestDigest": profile["_manifestDigest"],
              "source": profile["_sourceUrl"], "version": profile["version"],
              "archives": archives, "content": plugins, "gameLaunched": False,
              "personalConfigurationPreserved": True}
    (root / "discovery-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print("PASS automatic master discovery, fresh HTTPS archive download and isolated load order")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
