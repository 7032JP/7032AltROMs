#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
"""
AltROMs 辞書 ROM (dicrom.rom) 生成ツール。

拡張世代 (AV40EX / AV40SX 相当) 向けの **所定のサイズを供給する枠** として、
262,144 byte (0x40000) の全域 0x00 のイメージを生成する。

位置づけは extsub.rom と同じである。
**かな漢字変換 (日本語入力) の機能は実装していない。** 変換辞書としての内容は
一切持たず、日本語入力は非対応である (docs/COMPATIBILITY.md 参照)。

生成物は本ツールが計算で作る単一値の充填イメージである。同じ引数で何度実行しても
同じバイト列が得られる (決定的)。ネットワーク接続は不要である。

使い方:

    python3 scripts/gendicrom.py build/dicrom.rom
"""

from __future__ import annotations

import hashlib
import sys

SIZE = 0x40000  # 262,144 byte = 256 KiB
FILL = 0x00     # 全域この値で埋める


class DicromError(ValueError):
    """生成物が検査条件を満たさないときに送出する。"""


def check(cond: bool, msg: str) -> None:
    """検査条件が偽なら DicromError を送出する。

    組込みの assert 文は python -O で無効化されるため、生成物の検査は
    必ず本関数 (= 常に効く例外送出) を通す。
    """
    if not cond:
        raise DicromError(msg)


def build() -> bytes:
    data = bytes([FILL]) * SIZE
    # 検査 1: サイズ固定
    check(len(data) == SIZE, f"出力は {SIZE} byte でなければならない")
    # 検査 2: 全域が充填値であること (内容を持たない枠であることの担保)
    check(set(data) == {FILL}, f"全域が 0x{FILL:02X} でなければならない")
    return data


def main(dst_path: str) -> None:
    data = build()
    with open(dst_path, "wb") as f:
        f.write(data)
    print(f"[OK] {dst_path}: {len(data)} byte "
          f"(全域 0x{FILL:02X} の枠)  sha256={hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: gendicrom.py <output.rom>", file=sys.stderr)
        sys.exit(1)
    try:
        main(sys.argv[1])
    except DicromError as exc:
        print(f"[NG] {exc}", file=sys.stderr)
        sys.exit(1)
