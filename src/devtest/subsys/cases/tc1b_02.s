; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc1b_02 — $1B の分割応答: 応答が共有領域の応答部 (124 byte) を超えるとき $D381 bit7 で
;   継続を要求し、継続コマンド $64 で残りを受け取る (640x2 画素 = 160 byte)
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0C,$15,$07,$00,$00,$00,$00,$00,$02,$7F,$00
        FCB     $01,$02
        FCB     $FF,$0B,$1B,$00,$00,$00,$00,$02,$7F,$00,$01,$01
        FCB     $07

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
