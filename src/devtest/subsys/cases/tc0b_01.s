; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc0b_01 — $0B TABSET: 80 桁ぶんのタブストップ 10 byte を受理する
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0B,$0B,$01,$01,$01,$01,$01,$01,$01,$01,$01
        FCB     $01

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
