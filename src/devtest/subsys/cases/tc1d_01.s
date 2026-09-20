; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc1d_01 — $1D 矩形読出し 2: 矩形を B/R/G の 3 プレーンとして読む
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0C,$15,$05,$00,$00,$00,$00,$00,$00,$07,$00
        FCB     $01,$02
        FCB     $FF,$09,$1D,$00,$00,$00,$00,$00,$0F,$00,$01

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
