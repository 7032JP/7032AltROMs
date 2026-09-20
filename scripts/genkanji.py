#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
"""
AltROMs 16x16 漢字フォント ROM 生成ツール。

16 ドットの BDF ビットマップフォント (JIS X 0208 の符号位置を ENCODING に持つ
もの) を入力に取り、16x16 漢字フォント ROM イメージ 2 本
(第一水準 kanji.rom / 第二水準 kanji2.rom、各 131,072 byte) を出力する。

字形 (グリフ) の出所と、本ツールが担う独自成果物の範囲は
docs/LEGAL.md §9 に記載する。要点だけ再掲すると次のとおり:

  - 大半の字形は入力 BDF に由来する。本リポジトリが同梱する入力
    fonts/shinonome/shnmk16.bdf は、パブリックドメイン宣言つきで配布されて
    いる第三者のビットマップフォントを改変したものであり、下記 107 符号位置の
    CHAR レコードを scripts/strip_bdf_chars.py で取り除いてある。
  - 1〜8 区にある 107 符号位置 (句読点・括弧・記号・数学記号・片仮名 1 字・
    ギリシア文字 1 字・キリル文字 1 字・罫線素片) の字形は、本プロジェクトが
    一から書き起こしたものを scripts/kanji_glyph_overrides.py に定義しており、
    本ツールが組み立て段でその字形を用いる。本ツールは入力 BDF を読み取る
    だけで、ファイルには書き込まない。
  - 本ツールが行うのは「どの符号位置の字形を ROM のどの枠へ置くか」という
    配置規則の実装と、その規則に従った決定的な組み替え、および上記 107 符号
    位置への独自字形の適用である。配置規則の実装・独自字形・本スクリプトは
    本プロジェクトの独自成果物であり、MIT ライセンスで提供する。

ROM イメージの構造 (参考書籍および動作観察に基づく):

  - 1 グリフ = 16 走査線 x (左 8 dot, 右 8 dot) = 32 byte。
  - 131,072 byte / 32 byte = 4,096 枠。
  - アドレスレジスタへ書いた 16 bit 値 V に対し、
        左 8 dot = ROM[V * 2] 、 右 8 dot = ROM[V * 2 + 1]
        V = (枠番号 << 4) | 走査線 (0..15)
    走査線 0 が字の上端。
  - 枠が割り当てられない符号位置、および入力フォントに字形が無い符号位置は
    32 byte すべて 0 とする。

配置規則 (区点コード -> 枠番号):

  枠番号 idx (0..4095) を次のように分解し、JIS 符号へ展開する。

      group = (idx >> 9) & 7      … 区ブロックと点ブロックの組を選ぶ
      b     = (idx >> 8) & 1      … 区の bit3 (b を使わない group では 0 固定)
      i     = idx & 0xFF          … 区の下位 3 bit と点の下位 5 bit

      high = ((r >> 2) << 4) | (b << 3) | ((i >> 5) & 7)
      low  = ((r & 3)  << 5) | (i & 0x1F)
      区 = high - 0x20 、 点 = low - 0x20

  r は group ごとに決まる定数で、r = (区の上位ニブル << 2) | (点ブロック 1..3)。
  すなわち区点コードの乗算ではなくビット組み替えで枠番号へ写す規則である。
  第一水準側の並びは §_GROUPS_L1、第二水準側は §_GROUPS_L2 を参照。

使い方:

    python3 scripts/genkanji.py fonts/shinonome/shnmk16.bdf \
        --out-l1 build/kanji.rom --out-l2 build/kanji2.rom

同じ入力から何度実行しても同じバイト列が得られる (決定的)。ネットワーク接続は
不要である。
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

from kanji_glyph_overrides import OVERRIDES

ROM_SIZE = 131072
GLYPH_BYTES = 32
GLYPH_SLOTS = ROM_SIZE // GLYPH_BYTES  # 4096
GLYPH_ROWS = 16


class KanjiError(ValueError):
    """入力または生成物が検査条件を満たさないときに送出する。"""


def check(cond: bool, msg: str) -> None:
    """検査条件が偽なら KanjiError を送出する。

    組込みの assert 文は python -O で無効化されるため、生成物の検査は
    必ず本関数 (= 常に効く例外送出) を通す。
    """
    if not cond:
        raise KanjiError(msg)


# --------------------------------------------------------------------------
# BDF 読み込み
# --------------------------------------------------------------------------

class Glyph:
    __slots__ = ("code", "rows")

    def __init__(self, code: int, rows: list[int]) -> None:
        self.code = code      # JIS X 0208 の 7bit 符号値 (0x2121 など)
        self.rows = rows      # 16 個の 16bit 値 (bit15 が左端)


def parse_bdf(path: Path) -> dict[int, Glyph]:
    """16x16 均一の BDF を読み、JIS 符号値 -> Glyph の辞書を返す。"""
    glyphs: dict[int, Glyph] = {}
    encoding: int | None = None
    rows: list[int] | None = None

    with path.open("r", encoding="latin-1") as fp:
        for raw in fp:
            line = raw.rstrip("\r\n")
            if rows is not None:
                if line.startswith("ENDCHAR"):
                    if encoding is None or encoding < 0:
                        rows = None
                        encoding = None
                        continue
                    check(len(rows) == GLYPH_ROWS,
                          f"{path.name}: ENCODING {encoding} のビットマップが "
                          f"{len(rows)} 行 ({GLYPH_ROWS} 行を期待)")
                    glyphs[encoding] = Glyph(encoding, rows)
                    rows = None
                    encoding = None
                    continue
                token = line.strip()
                if token:
                    rows.append(int(token, 16) & 0xFFFF)
                continue

            if line.startswith("ENCODING "):
                encoding = int(line.split()[1])
            elif line.startswith("BBX "):
                bbx = tuple(int(v) for v in line.split()[1:5])
                check(bbx == (16, 16, 0, -2),
                      f"{path.name}: 想定外の BBX {bbx} "
                      "(本ツールは全グリフ均一の BBX 16 16 0 -2 のみ扱う)")
            elif line.startswith("BITMAP"):
                rows = []

    check(bool(glyphs), f"{path.name}: グリフを 1 つも読み取れませんでした")
    return glyphs


# --------------------------------------------------------------------------
# 配置規則
# --------------------------------------------------------------------------

# 第一水準側の group ごとの r 値。r = (区の上位ニブル << 2) | (点ブロック 1..3)。
#   0x09/0x0a/0x0b … 区の上位ニブルが 2 (1-15 区)
#   0x0d/0x0e/0x0f … 区の上位ニブルが 3 (16-31 区)
#   0x11/0x12/0x13 … 区の上位ニブルが 4 (32-47 区)
# group 0 のみ 0x09 と 0x0b の 2 つが 512 枠へ相乗りし、b を使わない。
_GROUPS_L1 = [(0x09, 0x0b), (0x0a,), (0x0d,), (0x0e,), (0x0f,), (0x11,), (0x12,), (0x13,)]

# 第二水準側 (第一水準の規則の拡張)。同じ組み替えのまま区の上位ニブルを 5/6/7 へ広げ、
# 48-84 区の全区点に枠が行き渡るよう並べたもの。実行環境で第二水準の漢字が
# 意図した字として表示される。
_GROUPS_L2 = [(0x1d, 0x1f), (0x1e,), (0x15,), (0x16,), (0x17,), (0x19,), (0x1a,), (0x1b,)]


def slot_table(groups) -> dict[tuple[int, int], int]:
    """(区, 点) -> 枠番号 の対応表を作る。

    groups は 8 要素で、各要素は
        (r,)      … b を使う (512 枠)
        (r0, r1)  … b を 0 固定にし、idx の bit8 で r を選ぶ (2 種が 512 枠に相乗り)
    """
    check(len(groups) == 8, "groups は 8 要素でなければならない")
    table: dict[tuple[int, int], int] = {}
    for idx in range(GLYPH_SLOTS):
        spec = groups[(idx >> 9) & 7]
        if len(spec) == 2:
            r = spec[(idx >> 8) & 1]
            b = 0
        else:
            r = spec[0]
            b = (idx >> 8) & 1
        i = idx & 0xFF
        high = ((r >> 2) << 4) | (b << 3) | ((i >> 5) & 7)
        low = ((r & 3) << 5) | (i & 0x1F)
        ku, ten = high - 0x20, low - 0x20
        if 1 <= ku <= 94 and 1 <= ten <= 94:
            check((ku, ten) not in table,
                  f"配置規則が重複しています: {ku} 区 {ten} 点")
            table[(ku, ten)] = idx
    check(bool(table), "配置規則から枠が 1 つも導けませんでした")
    check(max(table.values()) < GLYPH_SLOTS,
          f"配置規則が ROM の枠数を超えています (max slot {max(table.values())})")
    return table


# --------------------------------------------------------------------------
# 生成
# --------------------------------------------------------------------------

def build_rom(glyphs: dict[int, Glyph], table: dict[tuple[int, int], int]):
    """ROM イメージと集計値を返す。"""
    rom = bytearray(ROM_SIZE)
    missing: list[tuple[int, int]] = []
    filled = 0
    blank = 0

    for (ku, ten), slot in sorted(table.items()):
        code = ((ku + 0x20) << 8) | (ten + 0x20)
        glyph = glyphs.get(code)
        if glyph is None:
            missing.append((ku, ten))
            continue
        off = slot * GLYPH_BYTES
        for i, row in enumerate(glyph.rows):
            rom[off + i * 2] = (row >> 8) & 0xFF
            rom[off + i * 2 + 1] = row & 0xFF
        if any(glyph.rows):
            filled += 1
        else:
            blank += 1

    check(len(rom) == ROM_SIZE, f"出力は {ROM_SIZE} byte でなければならない")
    return bytes(rom), missing, filled, blank


def self_check(rom: bytes, glyphs: dict[int, Glyph],
               table: dict[tuple[int, int], int]) -> None:
    """書き込んだ字形が枠から読み戻せることを全数で確かめる。"""
    for (ku, ten), slot in table.items():
        code = ((ku + 0x20) << 8) | (ten + 0x20)
        glyph = glyphs.get(code)
        if glyph is None:
            continue
        off = slot * GLYPH_BYTES
        for i, row in enumerate(glyph.rows):
            got = (rom[off + i * 2] << 8) | rom[off + i * 2 + 1]
            check(got == row,
                  f"読み戻し不一致: {ku} 区 {ten} 点 走査線 {i}")


def apply_overrides(glyphs: dict[int, Glyph]) -> int:
    """独自字形を持つ符号位置の字形を、差し替え字形表のものへ置き換える。

    入力 BDF のファイルには書き込まず、読み込んだ後の組み立て用データに対して
    のみ適用する。表に載る符号位置は必ず独自字形になる (同梱の改変した BDF は
    当該 107 符号位置に字形を持たないので、表の字形を新たに立てる経路を通る)。
    """
    for code, rows in OVERRIDES.items():
        check(len(rows) == GLYPH_ROWS,
              f"差し替え字形 {code:04x} の走査線が {len(rows)} 本")
        glyphs[code] = Glyph(code, list(rows))
    return len(OVERRIDES)


def kuten_of(code: int) -> tuple[int, int]:
    return ((code >> 8) - 0x20, (code & 0xFF) - 0x20)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="16 ドット BDF から 16x16 漢字フォント ROM を生成する")
    ap.add_argument("bdf", type=Path, help="入力 BDF (16x16, JIS X 0208)")
    ap.add_argument("--out-l1", type=Path, required=True,
                    help="第一水準側 ROM の出力先 (kanji.rom)")
    ap.add_argument("--out-l2", type=Path, required=True,
                    help="第二水準側 ROM の出力先 (kanji2.rom)")
    args = ap.parse_args()

    glyphs = parse_bdf(args.bdf)
    codes = sorted(glyphs)
    lo_ku, lo_ten = kuten_of(codes[0])
    hi_ku, hi_ten = kuten_of(codes[-1])
    print(f"[bdf] {args.bdf}: {len(glyphs)} グリフ "
          f"({lo_ku} 区 {lo_ten} 点 .. {hi_ku} 区 {hi_ten} 点)")
    n_own = apply_overrides(glyphs)
    print(f"[own] scripts/kanji_glyph_overrides.py: {n_own} 符号位置に独自字形を適用")

    for tag, groups, out_path in (("l1", _GROUPS_L1, args.out_l1),
                                  ("l2", _GROUPS_L2, args.out_l2)):
        table = slot_table(groups)
        rom, missing, filled, blank = build_rom(glyphs, table)
        self_check(rom, glyphs, table)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(rom)
        digest = hashlib.sha256(rom).hexdigest()
        total = len(table)
        print(f"[OK] {out_path}: {len(rom)} byte  sha256={digest}")
        print(f"     枠 {total} / {GLYPH_SLOTS}  字形あり {filled} / "
              f"空白グリフ {blank} / 入力に無し {len(missing)}")

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KanjiError as exc:
        print(f"[NG] {exc}", file=sys.stderr)
        sys.exit(1)
