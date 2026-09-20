; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc1b_01 — $1B 矩形読出し 1: 単色矩形をビットイメージとして読む
;   $D383=X1(16bit), $D385=Y1, $D387=X2, $D389=Y2, $D38B=指定色数, $D38C〜=色コード
;   先に 8x2 の塗潰し矩形を置き、その左上 16x2 画素を読む
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0C,$15,$07,$00,$00,$00,$00,$00,$00,$07,$00
        FCB     $01,$02
        FCB     $FF,$0B,$1B,$00,$00,$00,$00,$00,$0F,$00,$01,$01
        FCB     $07

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
