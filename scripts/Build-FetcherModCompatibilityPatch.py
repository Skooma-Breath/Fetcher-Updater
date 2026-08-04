#!/usr/bin/env python3

"""Build hash-gated byte deltas for Fetcher-managed third-party mod fixes."""

import argparse
import base64
import difflib
import hashlib
import json
from pathlib import Path, PurePosixPath


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def safe_target(value: str) -> str:
    normalized = PurePosixPath(value.replace("\\", "/"))
    if (
        ":" in value
        or normalized.is_absolute()
        or not normalized.parts
        or any(part in {"", ".", ".."} for part in normalized.parts)
    ):
        raise ValueError(f"invalid install-relative target path: {value}")
    return normalized.as_posix()


def line_offsets(lines: list[bytes]) -> list[int]:
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    return offsets


def make_operations(source: bytes, output: bytes) -> list[dict[str, object]]:
    if not source:
        return [{"data": base64.b64encode(output).decode("ascii")}] if output else []

    source_lines = source.splitlines(keepends=True)
    output_lines = output.splitlines(keepends=True)
    offsets = line_offsets(source_lines)
    operations: list[dict[str, object]] = []
    matcher = difflib.SequenceMatcher(None, source_lines, output_lines, autojunk=False)
    for tag, source_start, source_end, output_start, output_end in matcher.get_opcodes():
        if tag == "equal":
            offset = offsets[source_start]
            length = offsets[source_end] - offset
            if length:
                operations.append({"copyOffset": offset, "copyLength": length})
        elif output_start != output_end:
            data = b"".join(output_lines[output_start:output_end])
            operations.append({"data": base64.b64encode(data).decode("ascii")})
    return operations


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build Fetcher Updater compatibility deltas for third-party mods."
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument(
        "--file",
        action="append",
        required=True,
        nargs=3,
        metavar=("TARGET_PATH", "UPSTREAM_FILE", "FETCHER_FILE"),
    )
    args = parser.parse_args()

    records: list[dict[str, object]] = []
    seen: set[str] = set()
    for target_value, upstream_value, fetcher_value in args.file:
        target = safe_target(target_value)
        target_key = target.casefold()
        if target_key in seen:
            raise ValueError(f"duplicate target path: {target}")
        seen.add(target_key)

        upstream = Path(upstream_value).resolve()
        fetcher = Path(fetcher_value).resolve()
        if not upstream.is_file():
            raise FileNotFoundError(f"upstream file is missing: {upstream}")
        if not fetcher.is_file():
            raise FileNotFoundError(f"Fetcher file is missing: {fetcher}")

        source = upstream.read_bytes()
        output = fetcher.read_bytes()
        if source == output:
            raise ValueError(f"source and output are identical: {target}")
        records.append(
            {
                "path": target,
                "sourceSha256": sha256(source),
                "sourceSize": len(source),
                "outputSha256": sha256(output),
                "outputSize": len(output),
                "operations": make_operations(source, output),
            }
        )

    records.sort(key=lambda record: str(record["path"]).casefold())
    manifest = {
        "formatVersion": 1,
        "patchVersion": args.version,
        "files": records,
    }
    output_path = args.output.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Built compatibility patch {args.version}: files={len(records)}")
    for record in records:
        print(f"  {record['path']}")


if __name__ == "__main__":
    main()
