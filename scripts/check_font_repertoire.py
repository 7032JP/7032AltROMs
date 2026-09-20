#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
"""
AltROMs フォント収録範囲の照合ツール。

docs/LEGAL.md §8.1 に掲げた 2 表 (非ゼロ / ゼロ) を唯一の情報源として読み取り、
生成済みフォントバイナリの実測と機械的に突き合わせる。文書が「収録していない」と
書いた符号位置に字形があったり、その逆であったりした状態のまま配布されることを防ぐ。

検査内容:
  1. 2 表が全 256 符号位置をちょうど 1 回ずつ覆っていること (重複も欠落も不可)
  2. 非ゼロ表の符号位置が、すべて 1 byte 以上の非ゼロを含むこと
  3. ゼロ表の符号位置が、すべて全 byte 0 であること
  4. 8x8 と 8x16 で非ゼロ符号位置の集合が一致すること (縦 2 倍展開の帰結)

いずれかを満たさなければ差分を表示して非ゼロ終了する。

usage: check_font_repertoire.py <docs/LEGAL.md> <font8x8.bin> <font8x16.bin>
"""

import re
import sys

BYTES_PER_GLYPH = {2048: 8, 4096: 16}

MARK_NONZERO = "<!-- font-repertoire:nonzero -->"
MARK_ZERO = "<!-- font-repertoire:zero -->"

# 表のセル中に置いた `$21-$7E` / `$EC` 形式の符号位置指定
RANGE_RE = re.compile(r"`\$([0-9A-F]{2})(?:-\$([0-9A-F]{2}))?`")


class RepertoireError(ValueError):
    """文書の記載または生成物が検査条件を満たさないときに送出する。"""


def parse_table(text: str, marker: str) -> set:
    """marker 直後の Markdown 表から符号位置の集合を読み取る。

    表は marker より後の最初の `|` 始まりの行から始まり、`|` で始まらない行で
    終わるものとする。区切り行 (`| --- |`) は読み飛ばす。
    """
    pos = text.find(marker)
    if pos < 0:
        raise RepertoireError(f"目印 {marker} が見つかりません")

    codes = set()
    started = False
    for line in text[pos + len(marker):].splitlines():
        s = line.strip()
        if not s.startswith("|"):
            if started:
                break
            continue
        started = True
        if set(s) <= set("|- :"):
            continue
        for lo_s, hi_s in RANGE_RE.findall(s):
            lo = int(lo_s, 16)
            hi = int(hi_s, 16) if hi_s else lo
            if hi < lo:
                raise RepertoireError(f"符号位置の範囲が逆順です: ${lo:02X}-${hi:02X}")
            for c in range(lo, hi + 1):
                if c in codes:
                    raise RepertoireError(
                        f"{marker} の表で ${c:02X} が重複しています")
                codes.add(c)

    if not started:
        raise RepertoireError(f"目印 {marker} の後に表がありません")
    if not codes:
        raise RepertoireError(f"{marker} の表から符号位置を読み取れません")
    return codes


def measure(path: str) -> tuple:
    """フォントバイナリを走査し (非ゼロ符号位置の集合, 1 字あたり byte 数) を返す。"""
    with open(path, "rb") as f:
        data = f.read()
    step = BYTES_PER_GLYPH.get(len(data))
    if step is None:
        raise RepertoireError(
            f"{path}: サイズ {len(data)} byte は 2048 / 4096 のいずれでもありません")
    nonzero = {c for c in range(256) if any(data[c * step:(c + 1) * step])}
    return nonzero, step


def fmt(codes) -> str:
    """符号位置の集合を `$01-$1F $21-$7E` の形に畳んで表示用に整える。"""
    out = []
    start = prev = None
    for c in sorted(codes):
        if start is None:
            start = prev = c
        elif c == prev + 1:
            prev = c
        else:
            out.append((start, prev))
            start = prev = c
    if start is not None:
        out.append((start, prev))
    return " ".join(f"${a:02X}" if a == b else f"${a:02X}-${b:02X}" for a, b in out)


def main(doc_path: str, f8_path: str, f16_path: str) -> None:
    with open(doc_path, encoding="utf-8") as f:
        text = f.read()

    doc_nonzero = parse_table(text, MARK_NONZERO)
    doc_zero = parse_table(text, MARK_ZERO)

    # 1. 2 表が全 256 符号位置をちょうど 1 回ずつ覆うこと
    both = doc_nonzero & doc_zero
    if both:
        raise RepertoireError(f"2 表に重複する符号位置があります: {fmt(both)}")
    missing = set(range(256)) - doc_nonzero - doc_zero
    if missing:
        raise RepertoireError(f"どちらの表にも無い符号位置があります: {fmt(missing)}")

    ref = None
    for path in (f8_path, f16_path):
        actual, step = measure(path)

        # 2 / 3. 文書の記載と実測の突き合わせ
        undocumented = actual - doc_nonzero
        overdocumented = doc_nonzero - actual
        if undocumented or overdocumented:
            msgs = []
            if undocumented:
                msgs.append(
                    f"文書がゼロと記すが字形がある: {fmt(undocumented)}")
            if overdocumented:
                msgs.append(
                    f"文書が収録と記すが字形が無い: {fmt(overdocumented)}")
            raise RepertoireError(f"{path}: " + " / ".join(msgs))

        # 4. 8x8 と 8x16 で非ゼロ符号位置が一致すること
        if ref is None:
            ref = actual
        elif actual != ref:
            raise RepertoireError(
                f"{path}: 非ゼロ符号位置が 8x8 と一致しません: "
                f"{fmt(actual ^ ref)}")

        print(f"[OK] {path}: {step} byte/字, 非ゼロ {len(actual)} 符号位置 "
              f"= 文書の記載と一致")

    print(f"[OK] 収録範囲 {fmt(doc_nonzero)}")
    print(f"[OK] ゼロ     {fmt(doc_zero)}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("usage: check_font_repertoire.py <docs/LEGAL.md> "
              "<font8x8.bin> <font8x16.bin>", file=sys.stderr)
        sys.exit(1)
    try:
        main(sys.argv[1], sys.argv[2], sys.argv[3])
    except (RepertoireError, OSError) as e:
        print(f"[NG] {e}", file=sys.stderr)
        sys.exit(1)
