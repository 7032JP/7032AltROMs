; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc20_02 — $20 文字ライン 形状 1: 文字で矩形の枠を描く
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0C,$20,$07,$23,$00,$00,$00,$00,$00,$0A,$00
        FCB     $05,$01

OBSTAB:
        FCB     OP_ANYC,7
        FDB     0,0,15,9                ; セル (0,0)
        FCB     OP_ANYC,7
        FDB     160,0,175,9             ; セル (10,0)
        FCB     OP_ANYC,7
        FDB     0,50,15,59              ; セル (0,5)
        FCB     OP_ANYC,7
        FDB     160,50,175,59           ; セル (10,5)
        FCB     OP_LIT
        FDB     16,10,159,49            ; 枠の内側: 何も出ない
        FCB     OP_LIT
        FDB     176,0,639,199           ; 枠の右: 何も出ない
        FCB     OP_LIT
        FDB     0,60,175,199            ; 枠の下: 何も出ない
        FCB     OP_END
        END
