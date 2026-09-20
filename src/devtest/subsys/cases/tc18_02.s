; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc18_02 — $18 種点が画面外 (Y=200): 何も塗らない (境界色は 1 色 = 白)
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$08,$18,$00,$C8,$00,$C8,$02,$01,$07

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_END
        END
