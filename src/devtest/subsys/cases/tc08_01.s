; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc08_01 — $08 文字ブロック読出し 形式 2 (属性 + 文字)
;   画面の先頭のセル (桁 0・行 0) は消去で F (最上位ビット) が立ち、$09 で属性 7 を
;   書いても F が立ったまま返る。桁 1 は書いた属性 7 のまま
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0B,$09,$00,$00,$01,$00,$00,$04,$07,$41,$07
        FCB     $42
        FCB     $FF,$05,$08,$00,$00,$01,$00

OBSTAB:
        FCB     OP_ANY
        FDB     0,0,15,9                ; 桁 0 の "A"
        FCB     OP_ANY
        FDB     16,0,31,9               ; 桁 1 の "B"
        FCB     OP_LIT
        FDB     32,0,639,9              ; 同じ行の残り: 何も出ない
        FCB     OP_LIT
        FDB     0,10,639,199            ; 2 行目以降: 何も出ない
        FCB     OP_END
        END
