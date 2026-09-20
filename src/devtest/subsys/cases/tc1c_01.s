; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc1c_01 — $1C 矩形書込み 1: ビットパターンを矩形へ書く
;   $D383=X0(16bit), $D385=Y0, $D387=X1, $D389=Y1, $D38B=色, $D38C=ファンクション,
;   $D38D=データ計数, $D38E〜=パターン
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0E,$1C,$00,$00,$00,$00,$00,$07,$00,$01,$07
        FCB     $00,$02,$AA,$55

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     1,0
        FCB     OP_PROBE
        FDB     0,1
        FCB     OP_PROBE
        FDB     1,1
        FCB     OP_END
        END
