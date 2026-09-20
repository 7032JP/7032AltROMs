; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc06_01 — $06 文字ブロック読出し 形式 1 (文字のみ)
;   $D383=X1, $D384=Y1, $D385=X2, $D386=Y2 (文字座標)
;   先に $07 で 4 セルへ "ABCD" を置き、同じ矩形を読み戻す
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0B,$07,$00,$00,$03,$00,$07,$04,$41,$42,$43
        FCB     $44
        FCB     $FF,$05,$06,$00,$00,$03,$00

OBSTAB:
        FCB     OP_ANY
        FDB     0,0,63,9                ; 桁 0-3 に "ABCD"
        FCB     OP_LIT
        FDB     64,0,639,9              ; 同じ行の残り: 何も出ない
        FCB     OP_LIT
        FDB     0,10,639,199            ; 2 行目以降: 何も出ない
        FCB     OP_END
        END
