; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc07_01 — $07 文字ブロック書込み 形式 1 (文字のみ、$D387=固定属性)
;   $D383=X1, $D384=Y1, $D385=X2, $D386=Y2, $D387=属性, $D388=データ計数, $D389〜=文字
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0B,$07,$00,$00,$03,$00,$07,$04,$41,$42,$43
        FCB     $44

OBSTAB:
        FCB     OP_ANYC,7
        FDB     0,0,15,9                ; 桁 0 の "A" が色 7
        FCB     OP_ANYC,7
        FDB     16,0,31,9               ; 桁 1 の "B" が色 7
        FCB     OP_ANYC,7
        FDB     32,0,47,9               ; 桁 2 の "C" が色 7
        FCB     OP_ANYC,7
        FDB     48,0,63,9               ; 桁 3 の "D" が色 7
        FCB     OP_LIT
        FDB     64,0,639,9              ; 同じ行の残り: 何も出ない
        FCB     OP_LIT
        FDB     0,10,639,199            ; 2 行目以降: 何も出ない
        FCB     OP_END
        END
