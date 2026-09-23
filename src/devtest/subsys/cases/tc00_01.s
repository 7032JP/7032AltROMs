; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc00_01 — コマンドコード $00: 取り上げられない (Type-A / Type-C)
;   共有 RAM 相対 2 (COMMAND) の 0 はコマンドが無い状態を表すので、
;   0 を書いてもコマンドの提示にはならない。
;   よって $00 は受理されず (OK は増えない)、画面も変わらない。
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$01,$00

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     320,100
        FCB     OP_END
        END
