#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
"""Assemble subsystem self-check samples and verify their BASIC loaders."""

from __future__ import annotations

import argparse
import difflib
import subprocess
import sys
from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1]
SUBSYS = PUBLIC / "src" / "devtest" / "subsys"
CASES = SUBSYS / "cases"
LOAD_ADDRESS = 0x6000
WORK_ADDRESS = 0x6C00
DATA_BYTES = 80
CASE_COUNT = 128
# 文字列領域 (CLEAR の第 1 引数)。
#   ローダは DATA の 16 進文字列を 1 個ずつ H$ へ読み込む。次の値を H$ へ
#   入れる時点では前の値がまだ生きているので、いちばん長い値 (DATA_BYTES
#   バイト = 16 進 2 * DATA_BYTES 文字) が 2 個同時に載る広さが要る。
#   4 * DATA_BYTES = 320 に、"&H" + MID$ の一時のぶんの余裕を足す。
STRING_SPACE = 4 * DATA_BYTES + 192


def extra_basic_lines(source: Path) -> list[str]:
    """Return the ``; @bas <line>`` directives of one sample, in order.

    A sample may need BASIC statements of its own (drawing commands, or data
    the sample reads).  They are written in the assembly source so that the
    sample stays the single origin of its loader.
    """
    lines = []
    for raw in source.read_text(encoding="utf-8").splitlines():
        text = raw.strip()
        if text.startswith("; @bas "):
            lines.append(text[len("; @bas "):].strip())
    return lines


def screen_320(source: Path) -> bool:
    """Return True when the sample asks for the 320x200 screen (``; @screen 320``).

    Type-A / Type-B samples that draw with the FM77AV argument order run on the
    320x200 screen, the screen those commands are meant for.
    """
    return any(
        raw.strip() == "; @screen 320"
        for raw in source.read_text(encoding="utf-8").splitlines()
    )


def loader_text(case_id: str, image: bytes, extra: list[str], s320: bool = False) -> str:
    """Return the deterministic DATA/POKE BASIC loader for one image."""
    if len(image) > WORK_ADDRESS - LOAD_ADDRESS:
        raise ValueError(
            f"{case_id}: image is {len(image)} bytes; "
            f"maximum is {WORK_ADDRESS - LOAD_ADDRESS}"
        )

    chunks = [image[i : i + DATA_BYTES] for i in range(0, len(image), DATA_BYTES)]
    lines = [
        "0 REM AltROMs test - MIT, own work.",
        f"1 REM GENERATED FROM {case_id}.s BY scripts/gen_subsys_tests.py",
        f"10 CLEAR {STRING_SPACE},&H5FFF",
        # 打鍵したプログラムの一覧が画面に残っていると、観測がその文字も数えてしまう
        "18 CLS",
        f"20 A=&H6000:FOR K=1 TO {len(chunks)}:READ H$:FOR J=1 TO LEN(H$) STEP 2:POKE A,VAL(\"&H\"+MID$(H$,J,2)):A=A+1:NEXT:NEXT",
    ]
    lines.extend(extra)
    # サブ CPU モニタと画面モードの選択は、機械語を走らせる直前に行う
    # (切替えるとサブ CPU が初期化されるので、BASIC 側の描画より後に置く)
    # 画面モード ($FD12 bit6): 0 = 640x200、&H40 = 320x200
    mode = "&H40" if s320 else "0"
    if case_id.startswith("ta"):
        lines.append(f"29 POKE &HFD12,{mode}:POKE &HFD13,1")
    elif case_id.startswith("tb"):
        lines.append(f"29 POKE &HFD12,{mode}:POKE &HFD13,2")
    lines.append("30 EXEC &H6000")
    if case_id.startswith(("ta", "tb")):
        # 試験結果は報告域 (主記憶) へ確定済み。文字で読めるよう、表示だけを
        # 起動時のサブ CPU モニタへ戻し、落ち着くまで待ってから PRINT に渡す。
        lines.append("35 POKE &HFD12,0:POKE &HFD13,0")
        lines.append("36 FOR I=1 TO 2000:NEXT")
    lines.extend(
        [
            "40 WIDTH 40:CLS:A=&H6E00",
            "50 C=PEEK(A):A=A+1:IF C=0 THEN END",
            "60 IF C=13 THEN PRINT:GOTO 50",
            "70 PRINT CHR$(C);:GOTO 50",
        ]
    )
    for index, chunk in enumerate(chunks):
        lines.append(f'{1000 + index * 10} DATA "{chunk.hex().upper()}"')
    # BASIC は行番号順に並べ替えて保持するので、ファイル上も行番号順に揃える
    # (DATA を読む順序が見た目のとおりになる)。
    lines.sort(key=lambda line: int(line.split(" ", 1)[0]))
    return "\n".join(lines) + "\n"


def assemble(lwasm: str, source: Path, binary: Path, listing: Path) -> None:
    binary.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            lwasm,
            "-fraw",
            "-I",
            str(SUBSYS),
            "-o",
            str(binary),
            f"-l{listing}",
            str(source),
        ],
        check=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="assemble subsystem samples and check generated BASIC loaders"
    )
    parser.add_argument("--lwasm", default="lwasm", help="lwasm executable")
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=PUBLIC / "build" / "devtest" / "subsys",
        help="directory for generated .bin and .lst files",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="rewrite generated .bas files instead of checking them",
    )
    args = parser.parse_args()

    sources = sorted(CASES.glob("t[abc]*.s"))
    if len(sources) != CASE_COUNT:
        print(f"[NG] subsystem assembly source count: {len(sources)} (expected {CASE_COUNT})")
        return 1

    failed = 0
    for source in sources:
        case_id = source.stem
        binary = args.build_dir / f"{case_id}.bin"
        listing = args.build_dir / f"{case_id}.lst"
        try:
            assemble(args.lwasm, source, binary, listing)
            generated = loader_text(
                case_id, binary.read_bytes(), extra_basic_lines(source), screen_320(source)
            )
        except (OSError, subprocess.CalledProcessError, ValueError) as error:
            print(f"[NG] {case_id}: {error}")
            failed += 1
            continue

        loader = source.with_suffix(".bas")
        if args.write:
            loader.write_text(generated, encoding="utf-8", newline="\n")
            print(f"[OK] {case_id}: {binary.stat().st_size} B -> {loader.name}")
            continue

        actual = loader.read_text(encoding="utf-8") if loader.exists() else ""
        if actual == generated:
            print(f"[OK] {case_id}: {binary.stat().st_size} B / loader matches")
            continue

        print(f"[NG] {case_id}: {loader.name} is not generated from {source.name}")
        for line in difflib.unified_diff(
            actual.splitlines(),
            generated.splitlines(),
            fromfile=str(loader),
            tofile="generated",
            lineterm="",
            n=2,
        ):
            print(line)
        failed += 1

    if failed:
        print(f"[NG] devtest-subsys: {failed} failure(s)")
        return 1
    action = "generated" if args.write else "verified"
    print(f"[OK] devtest-subsys: {CASE_COUNT} samples assembled; loaders {action}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
