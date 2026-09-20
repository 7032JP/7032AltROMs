#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
"""
入力 BDF から指定の符号位置を取り除いて改変した BDF を作る変換ツール。

本リポジトリが同梱する fonts/shinonome/shnmk16.bdf は、東雲フォントファミリー
16 ドット漢字ゴシック体の配布物の BDF から、本ツールで 107 符号位置の CHAR
レコードを取り除いて**改変したもの**である。取り除く符号位置は
scripts/kanji_glyph_overrides.py が独自字形を定義する 107 符号位置と同一で、
配布物の AUTHORS が「X11 標準の jiskan16 フォントからそのまま使わせていただ
いている文字のリスト」として 16 ドット節に列挙する 107 字に対応する。

同梱の改変した BDF はこの 107 符号位置に字形を持たない。ROM を組み立てる
scripts/genkanji.py は当該 107 符号位置へ scripts/kanji_glyph_overrides.py の
独自字形を必ず適用するため、改変した BDF を入力にしても生成される ROM のバイト列は
変わらない (docs/LEGAL.md §9.2)。

東雲フォントファミリーの配布条件は「自由な改造、他フォーマットへの変換、
組込み、再配布を行うことができます」と明文で述べており、本ツールによる改変と
改変したものの再配布はその許諾の範囲に含まれる (fonts/shinonome/LICENSE)。

リポジトリには改変した BDF だけを置く。配布物そのもの (アーカイブ・無改変の BDF) は
同梱しない。第三者が同じ改変結果を再現・検証したい場合は、docs/LEGAL.md §10 に
掲げた取得元 URL から配布物を取得し、その BDF を入力に本ツールを実行すればよい。
入力と出力の両方について SHA-256 を表示するので、fonts/shinonome/UPSTREAM_SHA256SUMS
(配布物側) と fonts/SHA256SUMS (同梱の改変した BDF) の値と突き合わせられる。

同じ入力から何度実行しても同じバイト列が得られる (決定的)。ネットワーク接続は
不要である。

使い方:

    # 改変した BDF を作る (配布物の bdf/shnmk16.bdf を入力に取る)
    python3 scripts/strip_bdf_chars.py <配布物の shnmk16.bdf> \
        --out fonts/shinonome/shnmk16.bdf

    # 同梱の改変した BDF が正しく作られているかだけを確かめる (書き出さない)
    python3 scripts/strip_bdf_chars.py <配布物の shnmk16.bdf> \
        --check fonts/shinonome/shnmk16.bdf
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

from kanji_glyph_overrides import OVERRIDES, kuten

# 改変した BDF のヘッダへ差し込む、導出方法を述べる COMMENT 行。BDF の COMMENT は
# ASCII で書く (配布物の BDF 全体が ASCII のため、改変したものも ASCII を保つ)。
DERIVATION_COMMENT = [
    "COMMENT",
    "COMMENT This is a modified version of the original shnmk16.bdf,",
    "COMMENT derived by the AltROMs project (scripts/strip_bdf_chars.py).",
    "COMMENT The 107 code positions that the original distribution's AUTHORS",
    "COMMENT file lists (16dot section) as taken from the X11 jiskan16 font",
    "COMMENT have been removed from this file.",
    "COMMENT AltROMs draws its own glyphs for those 107 code positions;",
    "COMMENT see scripts/kanji_glyph_overrides.py and docs/LEGAL.md.",
    "COMMENT The original license permits modification, conversion,",
    "COMMENT embedding and redistribution (see LICENSE in this directory).",
    "COMMENT",
]


class StripError(ValueError):
    """入力または生成物が検査条件を満たさないときに送出する。"""


def check(cond: bool, msg: str) -> None:
    """検査条件が偽なら StripError を送出する。

    組込みの assert 文は python -O で無効化されるため、生成物の検査は
    必ず本関数 (= 常に効く例外送出) を通す。
    """
    if not cond:
        raise StripError(msg)


def strip_chars(text: str, targets: set[int]) -> tuple[str, set[int]]:
    """BDF 本文から targets の符号位置の CHAR レコードを取り除く。

    返すのは (改変した本文, 実際に取り除いた符号位置の集合)。
    CHARS 行の字数は取り除いた分だけ減らす。ヘッダには導出方法を述べる
    COMMENT を差し込む。行の並び・字形の並びはそれ以外いっさい変えない。
    """
    lines = text.split("\n")
    out: list[str] = []
    removed: set[int] = set()

    block: list[str] | None = None   # STARTCHAR から ENDCHAR までを溜める
    encoding: int | None = None
    chars_line: int | None = None    # out の中の CHARS 行の位置
    kept = 0
    comment_done = False

    for line in lines:
        if block is not None:
            block.append(line)
            if line.startswith("ENCODING "):
                encoding = int(line.split()[1])
            elif line.startswith("ENDCHAR"):
                check(encoding is not None,
                      "ENCODING を持たない CHAR レコードがあります")
                if encoding in targets:
                    removed.add(encoding)
                else:
                    out.extend(block)
                    kept += 1
                block = None
                encoding = None
            continue

        if line.startswith("STARTCHAR"):
            block = [line]
            encoding = None
            continue

        if line.startswith("CHARS "):
            chars_line = len(out)
        elif line.startswith("FONT ") and not comment_done:
            out.extend(DERIVATION_COMMENT)
            comment_done = True

        out.append(line)

    check(block is None, "ENDCHAR で閉じていない CHAR レコードがあります")
    check(comment_done, "FONT 行が見つかりません")
    check(chars_line is not None, "CHARS 行が見つかりません")
    out[chars_line] = f"CHARS {kept}"
    return "\n".join(out), removed


def sha256_of(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(
        description="BDF から指定の符号位置を取り除いて改変したものを作る")
    ap.add_argument("bdf", type=Path,
                    help="入力 BDF (東雲フォント配布物の bdf/shnmk16.bdf)")
    ap.add_argument("--out", type=Path,
                    help="改変した BDF の出力先")
    ap.add_argument("--check", type=Path, dest="check_path",
                    help="既存の改変した BDF と突き合わせるだけで書き出さない")
    args = ap.parse_args()

    check(bool(args.out) != bool(args.check_path),
          "--out と --check はどちらか一方だけを指定する")

    src = args.bdf.read_bytes()
    check(src.isascii(),
          f"{args.bdf.name}: ASCII 以外のバイトを含みます")
    text = src.decode("ascii")

    targets = set(OVERRIDES)
    result, removed = strip_chars(text, targets)

    missing = sorted(targets - removed)
    check(not missing,
          "入力 BDF に次の符号位置がありません (配布物の bdf/shnmk16.bdf を "
          "入力に指定しているか確かめてください): "
          + ", ".join(f"{kuten(c)[0]} 区 {kuten(c)[1]} 点" for c in missing[:8]))
    check(len(removed) == len(targets),
          f"取り除いた符号位置が {len(removed)} 個です "
          f"({len(targets)} 個を期待)")

    data = result.encode("ascii")
    print(f"[in ] {args.bdf}: {len(src)} byte  sha256={sha256_of(args.bdf)}")
    print(f"[cut] {len(removed)} 符号位置の CHAR レコードを取り除きました "
          "(scripts/kanji_glyph_overrides.py の 107 符号位置)")

    if args.check_path:
        got = args.check_path.read_bytes()
        digest = hashlib.sha256(got).hexdigest()
        check(got == data,
              f"{args.check_path}: 改変した BDF が本ツールの出力と一致しません "
              f"(算出 sha256={hashlib.sha256(data).hexdigest()} / "
              f"当該ファイル sha256={digest})")
        print(f"[OK ] {args.check_path}: {len(got)} byte  sha256={digest}")
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    print(f"[out] {args.out}: {len(data)} byte  sha256={sha256_of(args.out)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except StripError as exc:
        print(f"[NG] {exc}", file=sys.stderr)
        sys.exit(1)
