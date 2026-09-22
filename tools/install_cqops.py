#!/usr/bin/env python3
"""Build, validate, and install libcqops as CQ's static backend package."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


CQ_CONTRACT_COMMIT = "0707dfd69e88bb76572ce518a10447823e7a5f14"


def source_root() -> Path:
    return Path(__file__).resolve().parent.parent


def run(command: list[str], *, cwd: Path | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def output(command: list[str], *, cwd: Path | None = None) -> str:
    return subprocess.run(
        command, cwd=cwd, check=True, text=True, stdout=subprocess.PIPE
    ).stdout.strip()


def cq_checkout(args: argparse.Namespace, root: Path) -> Path:
    if args.cq_dir:
        cq_dir = args.cq_dir.resolve()
    elif os.environ.get("CQ_LANG_DIR"):
        cq_dir = Path(os.environ["CQ_LANG_DIR"]).resolve()
    else:
        cq_dir = (root.parent / "CQ_lang").resolve()
    tool = cq_dir / "tools/backend_contract.py"
    if not tool.is_file():
        raise RuntimeError(
            f"CQ_lang contract tool not found at {tool}; pass --cq-dir"
        )
    head = output(["git", "rev-parse", "HEAD"], cwd=cq_dir)
    if head != CQ_CONTRACT_COMMIT:
        raise RuntimeError(
            f"CQ_lang is {head}, expected contract commit {CQ_CONTRACT_COMMIT}"
        )
    cq_build = cq_dir / args.cq_build
    if not (cq_build / "cmake_install.cmake").is_file():
        raise RuntimeError(
            f"CQ_lang build tree is not configured at {cq_build}"
        )
    return cq_dir


def check_manifest(root: Path, cq_dir: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="cqops-manifest-") as temporary:
        generated = Path(temporary) / "backend-manifest.json"
        run(
            [
                sys.executable,
                str(cq_dir / "tools/backend_contract.py"),
                "--root",
                str(cq_dir),
                "provider-manifest",
                "--name",
                "libcqops",
                "--output",
                str(generated),
            ]
        )
        expected = json.loads((root / "backend-manifest.json").read_text())
        actual = json.loads(generated.read_text())
        if actual != expected:
            raise RuntimeError(
                "backend-manifest.json differs from CQ_lang's generated manifest"
            )


def check_qec(args: argparse.Namespace) -> Path | None:
    if not args.qec_dir:
        return None
    qec_dir = args.qec_dir.resolve()
    required = [
        qec_dir / "include/qec/qec.h",
        qec_dir / args.qec_build / "libqec.a",
        qec_dir / args.qec_build / "libtommath.a",
        qec_dir / args.qec_build / "libcjson.a",
    ]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError(
            "requested QEC backend is incomplete; missing: " + ", ".join(missing)
        )
    return qec_dir


def parse_args() -> argparse.Namespace:
    root = source_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument(
        "--build-type", choices=("Debug", "Release"), default="Release"
    )
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--cq-dir", type=Path)
    parser.add_argument("--cq-build", default="build")
    parser.add_argument("--qec-dir", type=Path)
    parser.add_argument("--qec-build", default="build")
    parser.add_argument("--libdir", default="lib")
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--cmake-arg", action="append", default=[])
    args = parser.parse_args()
    if args.build_dir is None:
        args.build_dir = root / f"build-install-{args.build_type.lower()}"
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    return args


def main() -> int:
    args = parse_args()
    root = source_root()
    try:
        cq_dir = cq_checkout(args, root)
        qec_dir = check_qec(args)
        check_manifest(root, cq_dir)

        configure = [
            "cmake",
            "-S",
            str(root),
            "-B",
            str(args.build_dir.resolve()),
            f"-DCMAKE_BUILD_TYPE={args.build_type}",
            f"-DCMAKE_INSTALL_PREFIX={args.prefix.resolve()}",
            f"-DCMAKE_INSTALL_LIBDIR={args.libdir}",
            f"-DCQOPS_CQLANG_DIR={cq_dir}",
            f"-DCQOPS_CQLANG_BUILD={args.cq_build}",
            f"-DCQOPS_QEC_DIR={qec_dir or ''}",
            f"-DCQOPS_QEC_BUILD={args.qec_build}",
        ]
        configure.extend(args.cmake_arg)
        run(configure)
        run(
            [
                "cmake",
                "--build",
                str(args.build_dir.resolve()),
                "--config",
                args.build_type,
                "--parallel",
                str(args.jobs),
            ]
        )
        run(
            [
                "ctest",
                "--test-dir",
                str(args.build_dir.resolve()),
                "--build-config",
                args.build_type,
                "--parallel",
                str(args.jobs),
                "--output-on-failure",
                # L6 is the repository's opt-in, exhaustive CQ fixture audit,
                # not an installation gate (CLAUDE.md, "CQ integration").
                # The installer still runs L7, the archive contract, and the
                # installed-package consumer in addition to every local test.
                "--exclude-regex",
                "^l6_cqlang_fixtures$",
            ]
        )
        run(
            [
                "cmake",
                "--install",
                str(args.build_dir.resolve()),
                "--config",
                args.build_type,
                "--prefix",
                str(args.prefix.resolve()),
            ]
        )

        package = args.prefix.resolve() / args.libdir / "cmake/CQBackend"
        run(
            [
                sys.executable,
                str(cq_dir / "tools/backend_contract.py"),
                "--root",
                str(cq_dir),
                "check",
                "--archive",
                str(args.prefix.resolve() / args.libdir / "libcqops.a"),
                "--manifest",
                str(package / "backend-manifest.json"),
            ]
        )
        print(f"libcqops installed as CQBackend::backend in {args.prefix.resolve()}")
        return 0
    except (RuntimeError, subprocess.CalledProcessError, OSError, ValueError) as exc:
        print(f"install_cqops: ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
