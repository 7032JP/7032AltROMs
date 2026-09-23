#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
"""
AltROMs 独自字形 (差し替え字形表) の到達検査ツール。

scripts/kanji_glyph_overrides.py が定義する 107 符号位置について、
生成済みの 16x16 漢字フォント ROM を機械的に列挙し、独自字形が意図した枠へ
確実に行き渡っていること (107 符号位置の枠が差し替え字形表と一致すること) を
確かめる。

検査内容:
  1. 差し替え字形表が 107 符号位置を過不足なく定義していること
     (表側の自己検査は import 時に効く。ここでは区ごとの内訳まで確かめる)
  2. 同梱の改変した BDF が、当該 107 符号位置に字形を 1 つも持たないこと。
     同梱の BDF は scripts/strip_bdf_chars.py で当該 107 符号位置の CHAR
     レコードを取り除いて改変したものであり、この 107 字の字形の供給元は
     差し替え字形表だけである (docs/LEGAL.md §9.2)
  3. 各符号位置について、配置規則から枠番号を機械的に導き、どちらの ROM の
     どの枠へ到達するかを一覧化すること
  4. 枠へ到達する符号位置は、その枠 32 byte が差し替え字形表の字形と
     完全に一致すること。2 と合わせて、当該 107 枠の中身が独自字形で
     あることの判定として働く
  5. 枠へ到達しない符号位置が 8 区 1-31 点と一致すること

usage: check_kanji_overrides.py <shnmk16.bdf> <kanji.rom> <kanji2.rom>
"""

from __future__ import annotations

import sys
from pathlib import Path

import genkanji as gk
from kanji_glyph_overrides import OVERRIDES, kuten

EXPECTED_BY_KU = {1: 47, 2: 25, 5: 1, 6: 1, 7: 1, 8: 32}


class OverrideCheckError(ValueError):
    """生成物が検査条件を満たさないときに送出する。"""


def check(cond: bool, msg: str) -> None:
    """検査条件が偽なら OverrideCheckError を送出する。"""
    if not cond:
        raise OverrideCheckError(msg)


def glyph_bytes(rows) -> bytes:
    """16 個の 16bit 値を ROM 上の 32 byte 表現へ変換する。"""
    out = bytearray()
    for row in rows:
        out.append((row >> 8) & 0xFF)
        out.append(row & 0xFF)
    return bytes(out)


def slot_bytes(rom: bytes, slot: int) -> bytes:
    off = slot * gk.GLYPH_BYTES
    return rom[off:off + gk.GLYPH_BYTES]


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    bdf_path, l1_path, l2_path = (Path(p) for p in sys.argv[1:4])

    # 1. 表の内訳
    by_ku: dict[int, int] = {}
    for code in OVERRIDES:
        ku, _ = kuten(code)
        by_ku[ku] = by_ku.get(ku, 0) + 1
    check(by_ku == EXPECTED_BY_KU,
          f"独自字形の区ごとの内訳が想定と異なります: {by_ku}")
    total = sum(EXPECTED_BY_KU.values())

    bdf = gk.parse_bdf(bdf_path)
    l1 = l1_path.read_bytes()
    l2 = l2_path.read_bytes()
    for tag, rom in (("l1", l1), ("l2", l2)):
        check(len(rom) == gk.ROM_SIZE,
              f"{tag}: {len(rom)} byte ({gk.ROM_SIZE} byte を期待)")

    tables = {
        l1_path.name: (l1, gk.slot_table(gk._GROUPS_L1)),
        l2_path.name: (l2, gk.slot_table(gk._GROUPS_L2)),
    }

    # 2. 同梱の改変した BDF に当該 107 符号位置が残っていないこと
    present = [code for code in sorted(OVERRIDES) if code in bdf]
    check(not present,
          f"同梱の入力 BDF ({bdf_path.name}) に、独自字形を持つ符号位置の字形が "
          f"{len(present)} 個残っています。同梱するのは "
          "scripts/strip_bdf_chars.py で当該 107 符号位置を取り除いて改変した"
          "ものでなければなりません: "
          + ", ".join(f"{kuten(c)[0]} 区 {kuten(c)[1]} 点" for c in present[:8]))

    # 3 / 4 / 5. 符号位置ごとの到達先と枠の内容
    reached: list[tuple[int, str, int]] = []
    unreached: list[int] = []
    for code in sorted(OVERRIDES):
        ku, ten = kuten(code)
        own = glyph_bytes(OVERRIDES[code])
        hits = [(name, table[(ku, ten)])
                for name, (_, table) in tables.items() if (ku, ten) in table]
        if not hits:
            unreached.append(code)
            continue
        for name, slot in hits:
            rom = tables[name][0]
            got = slot_bytes(rom, slot)
            check(got == own,
                  f"{name} 枠 {slot} ({ku} 区 {ten} 点): "
                  "独自字形と内容が一致しません")
            reached.append((code, name, slot))

    check(all(kuten(c) == (8, i + 1) for i, c in enumerate(unreached)),
          "枠へ到達しない符号位置が 8 区 1-31 点と一致しません: "
          + ", ".join(f"{kuten(c)[0]} 区 {kuten(c)[1]} 点" for c in unreached))

    per_rom: dict[str, int] = {}
    for _, name, _ in reached:
        per_rom[name] = per_rom.get(name, 0) + 1
    print(f"[OK] 独自字形 {total} 符号位置 "
          f"(内訳 " + " / ".join(f"{k} 区 {v}" for k, v in
                                 sorted(EXPECTED_BY_KU.items())) + ")")
    for name in tables:
        print(f"[OK] {name}: 枠へ到達 {per_rom.get(name, 0)} 符号位置 "
              "(全枠が独自字形と一致)")
    print(f"[OK] 配置規則上どの枠にも回らない符号位置 {len(unreached)} "
          "(8 区 1-31 点)")
    print(f"[OK] {bdf_path.name}: 当該 {total} 符号位置の字形を持たない "
          f"(収録 {len(bdf)} グリフ)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OverrideCheckError, gk.KanjiError) as exc:
        print(f"[NG] {exc}", file=sys.stderr)
        sys.exit(1)
