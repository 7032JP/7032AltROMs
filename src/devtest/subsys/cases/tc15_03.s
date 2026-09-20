; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc15_03 — $15 形状 2: 塗潰し矩形
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0C,$15,$05,$00,$00,$64,$00,$32,$01,$2C,$00
        FCB     $96,$02

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_PROBE
        FDB     100,50
        FCB     OP_PROBE
        FDB     300,150
        FCB     OP_PROBE
        FDB     99,50
        FCB     OP_END
        END
