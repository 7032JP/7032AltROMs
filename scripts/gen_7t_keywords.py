#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
"""予約語表・ジャンプ表・トークン定数の同期生成器。

7T-BASIC の予約語トークン値は「予約語表の中の位置 + 起点値」で機械的に決まる。
本スクリプトは、予約語の一覧 (綴り・実装ルーチン) を唯一の情報源として、

  1. 予約語表      kw_tbl / kw_fn_tbl          (7t_startup.inc)
  2. ジャンプ表    cmd_jt / fn_jt              (7t_startup.inc、予約語表と同順)
  3. 二次予約語表  kw_2nd_tbl                  (7t_runtime.inc)
  4. 二次ハンドラ  cmd2_hdl_tbl                (7t_startup.inc、二次表と同順)
  5. トークン定数  7t_tokens.inc               (全欄 1 対 1 のシンボル定義)

を同一の並びで同期出力する。表とジャンプ表の 1 対 1 対応が 1 語でもずれると
即座に暴走するため、これらの表は手書き禁止であり、必ず本スクリプトで再生成する。

並び規則 (本プロジェクトが独自に定めた導出規則):
  - 先頭 1 文字 (バイト値の昇順) でまとめる
  - 同一先頭字の中は綴り長の降順、同長は綴り (バイト列) の昇順
この規則は「同一表内で、ある語が別の語の先頭部分になる組は長い綴りを先に置く」
という最長一致の成立条件を機械的に満たす (先頭部分の組は必ず同一先頭字群に
入り、群内で長い方が先に並ぶため)。出力前に次を機械検査する:

  - 各表の語数 (命令 105 / 関数 43 / 二次命令 6 / 二次関数 1)
  - 同一表内の先頭部分関係の全列挙と「長い方が先」の成立
  - '+' と '-' の隣接 (TOK_MINUS = TOK_PLUS+1。単項符号の判定が依存)
  - 命令語域が二次域 ($EE-$F3)・数値定数前置 ($FE)・関数前置 ($FF) と
    重ならないこと、関数語域が二次関数起点 ($B4) と重ならないこと

書き換えの前に、対象ファイルの現在の表を読み取り、綴りと実装ルーチンの対応が
本スクリプトの一覧と (並び順を除いて) 完全一致することを確認する。一致しない
場合はどちらかが古いので、書き換えずに異常終了する。

使い方:
  python3 scripts/gen_7t_keywords.py                   # 自ツリーを更新
  python3 scripts/gen_7t_keywords.py --common DIR ...  # 指定ツリー (複数可) を更新
  python3 scripts/gen_7t_keywords.py --check           # 検査のみ (書き換えない)
  python3 scripts/gen_7t_keywords.py --print-index     # 先頭字索引を表示 (参考)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# 予約語の一覧 (唯一の情報源)
#   (シンボル名, 綴り, ジャンプ表の実装ルーチン, 注記)
#   綴りは参考書籍の F-BASIC 文法書に基づく。シンボル名は 7t_tokens.inc の命名規約
#   ($ 落とし・記号は役割名) に従う。
# ---------------------------------------------------------------------------

CMDS = [
    ("END",       "END",       "impl_end",          ""),
    ("FOR",       "FOR",       "impl_for",          ""),
    ("NEXT",      "NEXT",      "impl_next",         ""),
    ("DATA",      "DATA",      "impl_data",         ""),
    ("DIM",       "DIM",       "impl_dim",          ""),
    ("READ",      "READ",      "impl_read",         ""),
    ("LET",       "LET",       "impl_let",          ""),
    ("GO",        "GO",        "impl_go",           ""),
    ("RUN",       "RUN",       "impl_run",          ""),
    ("IF",        "IF",        "impl_if",           ""),
    ("RESTORE",   "RESTORE",   "impl_restore",      ""),
    ("RETURN",    "RETURN",    "impl_return_entry", "(INTERVAL 自動保留の解除入口)"),
    ("REM",       "REM",       "impl_rem",          ""),
    ("APOS",      "'",         "impl_rem",          ""),
    ("QMARK",     "?",         "impl_print_disp",   "(PRINT の省略形。参考書籍の F-BASIC 文法書 3.3)"),
    ("STOP",      "STOP",      "impl_stop",         ""),
    ("ELSE",      "ELSE",      "impl_else",         ""),
    ("TRON",      "TRON",      "impl_tron",         ""),
    ("TROFF",     "TROFF",     "impl_troff",        ""),
    ("SWAP",      "SWAP",      "impl_swap",         ""),
    ("DEFSTR",    "DEFSTR",    "impl_defstr",       ""),
    ("DEFINT",    "DEFINT",    "impl_defint",       ""),
    ("DEFSNG",    "DEFSNG",    "impl_defsng",       ""),
    ("DEFDBL",    "DEFDBL",    "impl_defdbl",       ""),
    ("ON",        "ON",        "impl_on_entry",     "(ON INTERVAL GOSUB を先に見る)"),
    ("HARDC",     "HARDC",     "impl_stub",         ""),
    ("RENUM",     "RENUM",     "impl_renum",        ""),
    ("EDIT",      "EDIT",      "impl_edit",         ""),
    ("ERROR",     "ERROR",     "impl_error",        ""),
    ("RESUME",    "RESUME",    "impl_resume",       ""),
    ("AUTO",      "AUTO",      "impl_auto",         ""),
    ("DELETE",    "DELETE",    "impl_delete",       ""),
    ("TERM",      "TERM",      "impl_term",         ""),
    ("WIDTH",     "WIDTH",     "impl_width",        ""),
    ("UNLIST",    "UNLIST",    "impl_unlist",       ""),
    ("MON",       "MON",       "impl_mon",          ""),
    ("LOCATE",    "LOCATE",    "impl_locate3",      "(第3引数 ,S 対応, free 領域)"),
    ("CLS",       "CLS",       "impl_cls",          ""),
    ("CONSOLE",   "CONSOLE",   "impl_console",      ""),
    ("PSET",      "PSET",      "impl_pset",         ""),
    ("PRESET",    "PRESET",    "impl_preset",       ""),
    ("MOTOR",     "MOTOR",     "impl_motor",        ""),
    ("SKIPF",     "SKIPF",     "impl_skipf",        ""),
    ("SAVE",      "SAVE",      "impl_save",         ""),
    ("LOAD",      "LOAD",      "impl_load",         ""),
    ("MERGE",     "MERGE",     "impl_merge",        ""),
    ("EXEC",      "EXEC",      "impl_exec",         ""),
    ("OPEN",      "OPEN",      "impl_open",         "(KYBD:/SCRN: のみ)"),
    ("CLOSE",     "CLOSE",     "impl_close",        ""),
    ("FILES",     "FILES",     "impl_files",        ""),
    ("COM",       "COM",       "impl_stub",         ""),
    ("KEY",       "KEY",       "impl_key",          ""),
    ("PAINT",     "PAINT",     "impl_paint",        ""),
    ("BEEP",      "BEEP",      "impl_beep_snd",     ""),
    ("COLOR",     "COLOR",     "impl_color_disp",   "(形式1/2/3 振り分け)"),
    ("LINE",      "LINE",      "impl_line",         ""),
    ("DEF",       "DEF",       "impl_def",          ""),
    ("POKE",      "POKE",      "impl_poke",         ""),
    ("PRINT",     "PRINT",     "impl_print_disp",   "(@座標描画 振り分け, free 領域)"),
    ("CONT",      "CONT",      "impl_cont",         ""),
    ("LIST",      "LIST",      "impl_list",         ""),
    ("CLEAR",     "CLEAR",     "impl_clear",        ""),
    ("RANDOMIZE", "RANDOMIZE", "impl_randomize",    ""),
    ("WHILE",     "WHILE",     "impl_while",        ""),
    ("WEND",      "WEND",      "impl_wend",         ""),
    ("NEW",       "NEW",       "impl_new",          ""),
    ("GET",       "GET",       "impl_get",          ""),
    ("PUT",       "PUT",       "impl_put",          ""),
    ("CIRCLE",    "CIRCLE",    "impl_circle",       ""),
    ("CONNECT",   "CONNECT",   "impl_connect",      ""),
    ("SYMBOL",    "SYMBOL",    "impl_symbol",       ""),
    ("GCURSOR",   "GCURSOR",   "impl_gcursor",      ""),
    ("BUBINI",    "BUBINI",    "impl_stub",         ""),
    ("BUBW",      "BUBW",      "impl_stub",         ""),
    ("BUBR",      "BUBR",      "impl_stub",         ""),
    ("KILL",      "KILL",      "impl_stub",         ""),
    ("INTERVAL",  "INTERVAL",  "impl_interval",     ""),
    ("TAB",       "TAB(",      "ion_err",           ""),
    ("TO",        "TO",        "ion_err",           ""),
    ("SUB",       "SUB",       "ion_err",           ""),
    ("FN",        "FN",        "impl_fn",           ""),
    ("SPC",       "SPC(",      "ion_err",           ""),
    ("USING",     "USING",     "ion_err",           ""),
    ("USR",       "USR",       "ion_err",           ""),
    ("ERL",       "ERL",       "ion_err",           ""),
    ("ERR",       "ERR",       "ion_err",           ""),
    ("OFF",       "OFF",       "ion_err",           ""),
    ("THEN",      "THEN",      "ion_err",           ""),
    ("NOT",       "NOT",       "ion_err",           ""),
    ("STEP",      "STEP",      "ion_err",           ""),
    ("PLUS",      "+",         "ion_err",           ""),
    ("MINUS",     "-",         "ion_err",           ""),
    ("MUL",       "*",         "ion_err",           ""),
    ("DIV",       "/",         "ion_err",           ""),
    ("POW",       "^",         "ion_err",           ""),
    ("AND",       "AND",       "ion_err",           ""),
    ("OR",        "OR",        "ion_err",           ""),
    ("XOR",       "XOR",       "ion_err",           ""),
    ("EQV",       "EQV",       "ion_err",           ""),
    ("IMP",       "IMP",       "ion_err",           ""),
    ("MOD",       "MOD",       "ion_err",           ""),
    ("IDIV",      "\\",        "ion_err",           ""),
    ("GT",        ">",         "ion_err",           ""),
    ("EQ",        "=",         "ion_err",           ""),
    ("LT",        "<",         "ion_err",           ""),
]

FNS = [
    ("SGN",    "SGN",     "impl_fn_sgn",     ""),
    ("INT",    "INT",     "impl_fn_int",     ""),
    ("ABS",    "ABS",     "impl_fn_abs",     ""),
    ("FRE",    "FRE",     "impl_fn_fre",     ""),
    ("POS",    "POS",     "impl_fn_pos",     ""),
    ("SQR",    "SQR",     "impl_fn_sqr",     ""),
    ("LOG",    "LOG",     "impl_fn_log",     ""),
    ("EXP",    "EXP",     "impl_fn_exp",     ""),
    ("COS",    "COS",     "impl_fn_cos",     ""),
    ("SIN",    "SIN",     "impl_fn_sin",     ""),
    ("TAN",    "TAN",     "impl_fn_tan",     ""),
    ("ATN",    "ATN",     "impl_fn_atn",     ""),
    ("PEEK",   "PEEK",    "impl_fn_peek",    ""),
    ("LEN",    "LEN",     "impl_fn_len",     ""),
    ("STR",    "STR$",    "impl_fn_str_d",   ""),
    ("VAL",    "VAL",     "impl_fn_val",     ""),
    ("ASC",    "ASC",     "impl_fn_asc",     ""),
    ("CHR",    "CHR$",    "impl_fn_chr_d",   ""),
    ("CINT",   "CINT",    "impl_fn_cint",    ""),
    ("CSNG",   "CSNG",    "impl_fn_csng",    ""),
    ("CDBL",   "CDBL",    "impl_fn_cdbl",    ""),
    ("FIX",    "FIX",     "impl_fn_fix",     ""),
    ("SPACE",  "SPACE$",  "impl_fn_space_d", ""),
    ("HEX",    "HEX$",    "impl_fn_hex_d",   ""),
    ("OCT",    "OCT$",    "impl_fn_oct_d",   ""),
    ("LOF",    "LOF",     "impl_fn_lof",     ""),
    ("EOF",    "EOF",     "impl_fn_eof",     ""),
    ("PEN",    "PEN",     "pen_dev60",       "— 装置なし → Error 60"),
    ("LEFT",   "LEFT$",   "impl_fn_left_d",  ""),
    ("RIGHT",  "RIGHT$",  "impl_fn_right_d", ""),
    ("MID",    "MID$",    "impl_fn_mid_d",   ""),
    ("INSTR",  "INSTR",   "impl_fn_instr",   ""),
    ("SCREEN", "SCREEN",  "impl_fn_screen",  ""),
    ("ANPORT", "ANPORT",  "impl_fn_anport",  ""),
    ("VARPTR", "VARPTR",  "impl_fn_varptr",  ""),
    ("STRING", "STRING$", "impl_fn_strng_d", ""),
    ("RND",    "RND",     "impl_fn_rnd",     ""),
    ("INKEY",  "INKEY$",  "impl_fn_inkey_d", ""),
    ("INPUT",  "INPUT",   "impl_fn_input",   ""),
    ("CSRLIN", "CSRLIN",  "impl_fn_csrlin",  ""),
    ("POINT",  "POINT",   "impl_fn_point",   ""),
    ("TIME",   "TIME",    "impl_fn_time",    ""),
    ("DATE",   "DATE",    "impl_fn_date",    ""),
]

SECS = [
    ("CHAIN",  "CHAIN",  "impl_chain",  ""),
    ("ERASE",  "ERASE",  "impl_erase",  ""),
    ("LLIST",  "LLIST",  "impl_llist",  ""),
    ("LPRINT", "LPRINT", "impl_lprint", ""),
    ("SOUND",  "SOUND",  "impl_sound",  ""),
    ("PLAY",   "PLAY",   "impl_play",   ""),
]

# 二次関数語表 (kw_2nd_fn_tbl) は LPOS の 1 語のみ。1 語の表に並びは無いので
# 生成対象とせず、語数と綴りの検査のみ行う。
SEC_FNS = [("LPOS", "LPOS")]

# 域の割付 (P2 では従来の域をそのまま予約する)
TOK_FIRST = 0x80        # 命令語トークンの起点
TOK2_FIRST = 0x80       # 関数語トークン第2バイトの起点
SEC_FIRST = 0xEE        # 二次命令語トークンの起点
SEC_LAST_MAX = 0xF3     # 二次命令語域の上限
SEC_FN_FIRST = 0xB4     # 二次関数語トークン第2バイトの起点
NUM_PFX = 0xFE          # 数値定数・行番号参照の前置 (予約値)
FUNC_PFX = 0xFF         # 関数語トークンの前置 (予約値)


def sort_key(spelling: str):
    """独自並びの整列キー: (先頭バイト, 綴り長の降順, 綴りバイト列)。"""
    b = spelling.encode("ascii")
    return (b[0], -len(b), b)


def order_table(entries):
    return sorted(entries, key=lambda e: sort_key(e[1]))


# ---------------------------------------------------------------------------
# 機械検査
# ---------------------------------------------------------------------------

def check_tables(cmds, fns, secs) -> list[str]:
    errors = []
    if len(cmds) != 105:
        errors.append(f"命令語の語数が 105 でない: {len(cmds)}")
    if len(fns) != 43:
        errors.append(f"関数語の語数が 43 でない: {len(fns)}")
    if len(secs) != 6:
        errors.append(f"二次命令語の語数が 6 でない: {len(secs)}")
    if len(SEC_FNS) != 1:
        errors.append(f"二次関数語の語数が 1 でない: {len(SEC_FNS)}")

    # 同一表内の先頭部分関係: 長い綴りが先 (最長一致の成立条件)
    for name, table in (("kw_tbl", cmds), ("kw_fn_tbl", fns), ("kw_2nd_tbl", secs)):
        spells = [e[1] for e in table]
        for i, a in enumerate(spells):
            for j, b in enumerate(spells):
                if i == j:
                    continue
                if b.startswith(a) and len(b) > len(a) and j > i:
                    errors.append(
                        f"{name}: 「{a}」(位置 {i}) が先頭部分になる「{b}」(位置 {j}) より先にある")

    # '+' と '-' の隣接 (単項符号判定 7t_expr / 7t_numchk が依存)
    cmd_spells = [e[1] for e in cmds]
    if cmd_spells.index("-") != cmd_spells.index("+") + 1:
        errors.append("'-' が '+' の直後にない (TOK_MINUS = TOK_PLUS+1 が崩れる)")

    # 域の重なり
    cmd_last = TOK_FIRST + len(cmds) - 1
    if cmd_last >= SEC_FIRST:
        errors.append(f"命令語域の上端 ${cmd_last:02X} が二次域 ${SEC_FIRST:02X}- と重なる")
    sec_last = SEC_FIRST + len(secs) - 1
    if sec_last > SEC_LAST_MAX:
        errors.append(f"二次命令語域の上端 ${sec_last:02X} が上限 ${SEC_LAST_MAX:02X} を超える")
    fn_last = TOK2_FIRST + len(fns) - 1
    if fn_last >= SEC_FN_FIRST:
        errors.append(f"関数語域の上端 ${fn_last:02X} が二次関数起点 ${SEC_FN_FIRST:02X} と重なる")
    for v in range(TOK_FIRST, cmd_last + 1):
        if v in (NUM_PFX, FUNC_PFX):
            errors.append(f"命令語トークンが予約値 ${v:02X} を侵食している")
    return errors


# ---------------------------------------------------------------------------
# 出力 (アセンブラ断片)
# ---------------------------------------------------------------------------

def fcb_item(ch: str, last: bool) -> str:
    if ch in ("'", "\\"):
        s = f"${ord(ch):02X}"
    else:
        s = f"'{ch}'"
    return s + "+$80" if last else s


def emit_kw_lines(table, first_val):
    lines = []
    for i, (sym, spelling, _tgt, _note) in enumerate(table):
        items = [fcb_item(c, k == len(spelling) - 1) for k, c in enumerate(spelling)]
        body = f"                FCB     {','.join(items)}"
        pad = max(1, 56 - len(body))
        lines.append(f"{body}{' ' * pad}; ${first_val + i:02X} {spelling}")
    return lines


def kwidx_rows(table, first_val, label):
    """先頭字索引 (kw_idx / fn_idx) の 26 欄 (A-Z) を計算する。

    各欄は「その英字以上の語頭を持つ最初の語」(下限探索) を指す 2 バイト:
      byte0 = 群先頭の表内変位の下位 8 ビット
      byte1 = bit7 に変位の第 9 ビット、下位 7 ビットに群先頭トークンの下位 7 ビット
    トークン値は常に bit7=1 ($80-) なので、bit7 を変位の桁上げに転用しても
    復元 (byte1 の下位 7 ビット | $80) で情報は失われない。
    その英字の語が無い欄は次の群を指し、走査本体の語頭昇順早期打切りが
    そのまま失敗を返す。表終端まで語が無い場合は変位 = 表長 (終端検査で失敗)。
    """
    spells = [e[1] for e in table]
    offs, off = [], 0
    for sp in spells:
        offs.append(off)
        off += len(sp.encode("ascii"))
    total = off
    rows = []
    for li in range(26):
        letter = chr(ord("A") + li)
        for i, sp in enumerate(spells):
            if ord(sp[0]) >= ord(letter):
                widx, o = i, offs[i]
                break
        else:
            widx, o = len(spells), total
        tok = first_val + widx
        if o >= 512:
            raise ValueError(f"{label}: 変位 {o} が 9 ビットを超える (表が伸び過ぎ)")
        packed = (tok & 0x7F) | (0x80 if o >= 256 else 0)
        tgt = spells[widx] if widx < len(spells) else "(表終端)"
        rows.append((letter, o & 0xFF, packed, o, tok, tgt))
    return rows


def emit_kwidx_lines(table, first_val, label):
    lines = []
    for letter, b0, b1, off, tok, tgt in kwidx_rows(table, first_val, label):
        body = f"                FCB     ${b0:02X},${b1:02X}"
        pad = max(1, 56 - len(body))
        lines.append(f"{body}{' ' * pad}; '{letter}' → 変位 {off:3d} (${tok:02X} {tgt})")
    return lines


def emit_jt_lines(table, first_val):
    lines = []
    for i, (sym, spelling, tgt, note) in enumerate(table):
        comment = f"; ${first_val + i:02X} {spelling}"
        if note:
            comment += f" {note}"
        lines.append(f"                FDB     {tgt:<15} {comment}")
    return lines


def emit_tokens_inc(cmds, fns, secs, with_spdx: bool) -> str:
    out = []
    w = out.append
    if with_spdx:
        w("; SPDX-License-Identifier: MIT")
        w("; Copyright (c) 2026 Naomitsu.Tsugiiwa")
    w(";==============================================================================")
    w("; 7T-BASIC 3.1 — 予約語トークン値のシンボル定義")
    w(";")
    w(";   予約語表 (7t_startup.inc の kw_tbl / kw_fn_tbl、7t_runtime.inc の")
    w(";   kw_2nd_tbl / kw_2nd_fn_tbl) の各欄に 1 対 1 で対応する定数を、当ファイルへ")
    w(";   一元化する。実装側は生の 16 進即値を書かず、必ずここの名前を参照する。")
    w(";   トークン値は予約語表の位置から機械的に決まる内部値であり、表の並びは")
    w(";   本プロジェクトが独自に定めた規則 (先頭 1 文字でまとめ、同一先頭字の中は")
    w(";   綴り長の降順 → 同長は綴り順) による。")
    w(";   綴りは参考書籍の F-BASIC 文法書に基づく。値の並びは表の並びと同順。")
    w(";   $ で終わる関数名は名前から $ を落とす (既存の TOK2_MID に合わせる)。")
    w(";")
    w(";   本ファイルと予約語表・ジャンプ表は scripts/gen_7t_keywords.py が同期生成")
    w(";   する。手書きで編集しない (表とジャンプ表の対応がずれると暴走するため)。")
    w(";==============================================================================")
    w("")

    def row(name, val, comment):
        width = 8 if len(val) < 8 else 16
        s = name.ljust(16) + "EQU".ljust(8) + val.ljust(width)
        if comment:
            s += "; " + comment
        return s.rstrip()

    w("; --- 域の境界と予約値 ----------------------------------------------------")
    w(row("TOK_FIRST", f"${TOK_FIRST:02X}", "命令語トークンの起点 (= kw_tbl 0 番, これ未満は生バイト)"))
    w(row("TOK2_FIRST", f"${TOK2_FIRST:02X}", "関数語トークン ($FF xx) 第2バイトの起点 (= kw_fn_tbl 0 番)"))
    w(row("TOK_2ND_FIRST", f"${SEC_FIRST:02X}", "二次命令語トークンの起点 (= kw_2nd_tbl 0 番)"))
    w(row("TOK_2ND_LAST", f"${SEC_FIRST + len(secs) - 1:02X}", "二次命令語トークンの終端 (= kw_2nd_tbl 最終欄)"))
    w(row("TOK_NUM_PFX", f"${NUM_PFX:02X}", "数値定数・行番号参照の前置バイト (予約値)"))
    w(row("TOK_FUNC_PFX", f"${FUNC_PFX:02X}", "関数語トークンの前置バイト (予約値)"))
    w("")
    w(f"; --- 命令語トークン (kw_tbl / cmd_jt と同順、{len(cmds)} 語) ---------------------")
    for i, (sym, spelling, _t, _n) in enumerate(cmds):
        w(row("TOK_" + sym, f"${TOK_FIRST + i:02X}", spelling))
    w("")
    w(f"; --- 関数語トークン第2バイト (kw_fn_tbl / fn_jt と同順、{len(fns)} 語) -----------")
    for i, (sym, spelling, _t, _n) in enumerate(fns):
        w(row("TOK2_" + sym, f"${TOK2_FIRST + i:02X}", spelling))
    w("")
    w("; --- 二次予約語トークン (kw_2nd_tbl / kw_2nd_fn_tbl と同順) --------------")
    for i, (sym, spelling, _t, _n) in enumerate(secs):
        w(row("TOK_2ND_" + sym, f"${SEC_FIRST + i:02X}", spelling))
    w(row("TOK_2ND_FN", f"${SEC_FN_FIRST:02X}", "二次関数語トークン ($FF xx) 第2バイトの起点 (LPOS)"))
    w("")
    w("; --- 既存の呼び名 (先行して使われている別名。値は上の定義と同一) ---------")
    w(row("TOK_REM_APOS", "TOK_APOS", "注記の短縮形 ' のトークン (REM と同じ扱い)"))
    w(row("TOK_UPLUS", "TOK_PLUS", "'+' 演算子トークン ('-' は +1。生成器が隣接を検査)"))
    w("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# 既存ソースの読み取り (書き換え前の照合)
# ---------------------------------------------------------------------------

FCB_ITEM_RE = re.compile(r"^(?:'(.)'|\$([0-9A-Fa-f]{2}))(\+\$80)?$")


def parse_kw_block(lines):
    """FCB 行の並びから綴りの一覧を復元する。"""
    spells = []
    for ln in lines:
        body = ln.split(";", 1)[0].strip()
        if not body:
            continue
        m = re.match(r"^FCB\s+(.*)$", body)
        if not m:
            raise ValueError(f"FCB 行でない: {ln!r}")
        word = []
        ended = False
        for item in split_items(m.group(1)):
            im = FCB_ITEM_RE.match(item.strip())
            if not im:
                raise ValueError(f"FCB 項を解釈できない: {item!r} ({ln!r})")
            ch = im.group(1) if im.group(1) is not None else chr(int(im.group(2), 16))
            word.append(ch)
            if im.group(3):
                ended = True
        if not ended:
            raise ValueError(f"語末フラグ (+$80) が無い: {ln!r}")
        spells.append("".join(word))
    return spells


def split_items(s: str):
    """カンマ区切り (引用符内のカンマは区切らない) 。"""
    items, cur, q = [], "", False
    for c in s:
        if c == "'":
            q = not q
            cur += c
        elif c == "," and not q:
            items.append(cur)
            cur = ""
        else:
            cur += c
    if cur.strip():
        items.append(cur)
    return items


def parse_jt_block(lines):
    targets = []
    for ln in lines:
        body = ln.split(";", 1)[0].strip()
        if not body:
            continue
        m = re.match(r"^FDB\s+(\S+)$", body)
        if not m:
            raise ValueError(f"FDB 行でない: {ln!r}")
        targets.append(m.group(1))
    return targets


def find_region(lines, start_label, end_label=None, count_directive=None):
    """start_label 行の次から、end_label 行の直前 (または連続する
    count_directive 行が尽きるまで) の範囲 [lo, hi) を返す。"""
    for i, ln in enumerate(lines):
        if ln.split(";", 1)[0].strip() == start_label + ":":
            lo = i + 1
            break
    else:
        raise ValueError(f"ラベルが見つからない: {start_label}")
    if end_label is not None:
        for j in range(lo, len(lines)):
            if lines[j].split(";", 1)[0].strip() == end_label + ":":
                return lo, j
        raise ValueError(f"終端ラベルが見つからない: {end_label}")
    j = lo
    while j < len(lines):
        body = lines[j].split(";", 1)[0].strip()
        if body.startswith(count_directive + " ") or body.startswith(count_directive + "\t"):
            j += 1
            continue
        break
    return lo, j


def verify_current(startup_lines, runtime_lines):
    """現在の表の内容 (綴り→実装ルーチンの対応) が本一覧と一致するか照合する。"""
    lo, hi = find_region(startup_lines, "kw_tbl", "kw_tbl_end")
    cur_cmd_spells = parse_kw_block(startup_lines[lo:hi])
    lo, hi = find_region(startup_lines, "kw_fn_tbl", "kw_fn_tbl_end")
    cur_fn_spells = parse_kw_block(startup_lines[lo:hi])
    lo, hi = find_region(startup_lines, "cmd_jt", "cmd_jt_end")
    cur_cmd_targets = parse_jt_block(startup_lines[lo:hi])
    lo, hi = find_region(startup_lines, "fn_jt", count_directive="FDB")
    cur_fn_targets = parse_jt_block(startup_lines[lo:hi])
    lo, hi = find_region(startup_lines, "cmd2_hdl_tbl", count_directive="FDB")
    cur_sec_targets = parse_jt_block(startup_lines[lo:hi])
    lo, hi = find_region(runtime_lines, "kw_2nd_tbl", "kw_2nd_fn_tbl")
    cur_sec_spells = parse_kw_block(runtime_lines[lo:hi])
    lo, hi = find_region(runtime_lines, "kw_2nd_fn_tbl", "kw_2nd_fn_tbl_end")
    cur_secfn_spells = parse_kw_block(runtime_lines[lo:hi])

    errors = []
    for name, spells, targets, canon in (
        ("命令語", cur_cmd_spells, cur_cmd_targets, CMDS),
        ("関数語", cur_fn_spells, cur_fn_targets, FNS),
        ("二次命令語", cur_sec_spells, cur_sec_targets, SECS),
    ):
        if len(spells) != len(targets):
            errors.append(f"{name}: 表 {len(spells)} 語とジャンプ表 {len(targets)} 欄で数が違う")
            continue
        cur_map = dict(zip(spells, targets))
        canon_map = {sp: tgt for (_sym, sp, tgt, _n) in canon}
        if cur_map != canon_map:
            for sp in sorted(set(cur_map) | set(canon_map)):
                a, b = cur_map.get(sp), canon_map.get(sp)
                if a != b:
                    errors.append(f"{name}: 「{sp}」実体 {a!r} / 一覧 {b!r} が食い違う")
    if cur_secfn_spells != [sp for (_s, sp) in SEC_FNS]:
        errors.append(f"二次関数語: 実体 {cur_secfn_spells!r} が一覧と食い違う")
    return errors


# ---------------------------------------------------------------------------
# 書き換え
# ---------------------------------------------------------------------------

def replace_region(lines, start_label, new_lines, end_label=None, count_directive=None):
    lo, hi = find_region(lines, start_label, end_label, count_directive)
    return lines[:lo] + new_lines + lines[hi:]


def process_tree(common: Path, write: bool) -> bool:
    startup_p = common / "7t_startup.inc"
    runtime_p = common / "7t_runtime.inc"
    tokens_p = common / "7t_tokens.inc"
    deffn_p = common / "7t_deffn.inc"
    startup = startup_p.read_text(encoding="utf-8").split("\n")
    runtime = runtime_p.read_text(encoding="utf-8").split("\n")
    tokens_old = tokens_p.read_text(encoding="utf-8")
    deffn = deffn_p.read_text(encoding="utf-8").split("\n")

    errors = verify_current(startup, runtime)
    if errors:
        for e in errors:
            print(f"[NG] {common}: {e}", file=sys.stderr)
        return False

    cmds = order_table(CMDS)
    fns = order_table(FNS)
    secs = order_table(SECS)
    errors = check_tables(cmds, fns, secs)
    if errors:
        for e in errors:
            print(f"[NG] {e}", file=sys.stderr)
        return False

    startup = replace_region(startup, "kw_tbl", emit_kw_lines(cmds, TOK_FIRST), "kw_tbl_end")
    startup = replace_region(startup, "kw_fn_tbl", emit_kw_lines(fns, TOK2_FIRST), "kw_fn_tbl_end")
    startup = replace_region(startup, "cmd_jt", emit_jt_lines(cmds, TOK_FIRST), "cmd_jt_end")
    startup = replace_region(startup, "fn_jt", emit_jt_lines(fns, TOK2_FIRST), count_directive="FDB")
    startup = replace_region(startup, "cmd2_hdl_tbl", emit_jt_lines(secs, SEC_FIRST), count_directive="FDB")
    runtime = replace_region(runtime, "kw_2nd_tbl", emit_kw_lines(secs, SEC_FIRST), "kw_2nd_fn_tbl")
    for label, tbl, first_val in (("kw_idx", cmds, TOK_FIRST), ("fn_idx", fns, TOK2_FIRST)):
        idx_new = emit_kwidx_lines(tbl, first_val, label)
        lo, hi = find_region(deffn, label, label + "_end")
        if not write and [ln.strip() for ln in deffn[lo:hi] if ln.strip()] != [ln.strip() for ln in idx_new]:
            print(f"[NG] {common}: {label} (先頭字索引) が予約語表と同期していない", file=sys.stderr)
            return False
        deffn = deffn[:lo] + idx_new + deffn[hi:]

    with_spdx = tokens_old.lstrip().startswith("; SPDX")
    tokens_new = emit_tokens_inc(cmds, fns, secs, with_spdx)

    if write:
        startup_p.write_text("\n".join(startup), encoding="utf-8", newline="\n")
        runtime_p.write_text("\n".join(runtime), encoding="utf-8", newline="\n")
        tokens_p.write_text(tokens_new, encoding="utf-8", newline="\n")
        deffn_p.write_text("\n".join(deffn), encoding="utf-8", newline="\n")
        print(f"[OK] {common}: kw_tbl/kw_fn_tbl/cmd_jt/fn_jt/kw_2nd_tbl/cmd2_hdl_tbl/kw_idx/fn_idx/7t_tokens.inc を再生成")
    else:
        print(f"[OK] {common}: 照合のみ (書き換えなし)")
    return True


def print_index(cmds):
    print("; 先頭字索引 (参考: 先頭バイト → kw_tbl 内の語番号)")
    seen = {}
    for i, (_s, sp, _t, _n) in enumerate(cmds):
        b = sp[0]
        if b not in seen:
            seen[b] = i
    for b, i in seen.items():
        print(f";   {b!r} -> 語 {i} (トークン ${TOK_FIRST + i:02X})")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--common", action="append", type=Path,
                    help="7t_*.inc のあるディレクトリ (複数指定可。省略時は自ツリー)")
    ap.add_argument("--check", action="store_true", help="照合と機械検査のみ (書き換えない)")
    ap.add_argument("--print-index", action="store_true", help="先頭字索引を表示")
    args = ap.parse_args()

    commons = args.common or [Path(__file__).resolve().parent.parent / "src" / "7tbasic3" / "common"]
    if args.print_index:
        print_index(order_table(CMDS))
        return 0
    ok = all(process_tree(c, write=not args.check) for c in commons)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
