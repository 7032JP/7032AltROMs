; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc19_03 — $19 SYMBOL の文字数 81 (参考書籍)
;   文字数 81 は誤りにならない (相対 0 は 0。動作観察)。文字列の欄は 0 のままなので、
;   文字コード $00 の空白だけが並び、1 画素も描かれない。
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $FB,$0B,$19,$07,$00,$00,$01,$01,$00,$64,$00,$64,$51

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
