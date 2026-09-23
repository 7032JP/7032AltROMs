#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
"""
AltROMs 8x16 ANK フォント生成ツール。

genfont.py が出力する 8x8 ANK フォント (256 文字 x 8 byte = 2048 byte) を
入力に取り、8x16 ANK フォント (256 文字 x 16 byte = 4096 byte) を出力する。

生成規則 (決定的・全文字一律):
  8x8 字形の各走査線を縦方向に 2 回ずつ複製し、16 走査線へ引き伸ばす。
    出力行 2n     = 入力行 n
    出力行 2n + 1 = 入力行 n
  横方向は 8 ドットのまま (ビット割付は入力と同一)。

  この規則を採る理由:
    - 8x8 側の字形をそのまま基とするため、字形の一貫性が保てる
      (8x8 表示と 8x16 表示で同じ書体に見える)。
    - 上下に余白を置く方式と違い、16 走査線の文字枠を字形が満たすため
      8x16 表示時に文字が小さく見えない。
    - 変換が可逆かつ機械的で、生成物の由来が入力 1 本に限定される。

字形は genfont.py 側の定義を用い、本ツールはその縦方向スケーリングのみを行う。
"""

import sys


class FontError(ValueError):
    """入力または生成物が検査条件を満たさないときに送出する。"""


def check(cond: bool, msg: str) -> None:
    """検査条件が偽なら FontError を送出する。

    組込みの assert 文は python -O で無効化されるため、生成物の検査は
    必ず本関数 (= 常に効く例外送出) を通す。
    """
    if not cond:
        raise FontError(msg)


SRC_SIZE = 2048   # 256 文字 x 8 byte
DST_SIZE = 4096   # 256 文字 x 16 byte


def expand(src: bytes) -> bytes:
    """8x8 (8 byte/字) を 8x16 (16 byte/字) へ縦 2 倍で引き伸ばす。"""
    check(len(src) == SRC_SIZE, f"input must be {SRC_SIZE} bytes, got {len(src)}")
    out = bytearray(DST_SIZE)
    for code in range(256):
        s = code * 8
        d = code * 16
        for y in range(8):
            row = src[s + y]
            out[d + y * 2] = row
            out[d + y * 2 + 1] = row
    return bytes(out)


def main(src_path: str, dst_path: str) -> None:
    with open(src_path, "rb") as f:
        src = f.read()

    data = expand(src)

    # ゲート 1: サイズ固定 (ROM 内オフセットのアンカー保護)
    check(len(data) == DST_SIZE, f"output must be {DST_SIZE} bytes")
    # ゲート 2: 全面ブロック文字 ($87) が 16 行とも塗り潰しであること
    check(bytes(data[0x87 * 16:0x87 * 16 + 16]) == b"\xFF" * 16, "$87 must be full block")
    # ゲート 3: 空白文字 ($20) が 16 行とも空であること
    check(bytes(data[0x20 * 16:0x20 * 16 + 16]) == b"\x00" * 16, "$20 must be blank")
    # ゲート 4: 引き伸ばし規則の全数検査 (偶数行と奇数行が対で一致)
    for i in range(0, DST_SIZE, 2):
        check(data[i] == data[i + 1], f"row pair mismatch at {i}")

    with open(dst_path, "wb") as f:
        f.write(data)
    nonblank = sum(1 for c in range(256)
                   if any(data[c * 16:(c + 1) * 16]))
    print(f"[OK] {dst_path}: {len(data)} byte "
          f"(8x8 -> 8x16 縦 2 倍展開, 字形定義 {nonblank} 文字)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: genfont16.py <font8x8.bin> <output8x16.bin>", file=sys.stderr)
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
