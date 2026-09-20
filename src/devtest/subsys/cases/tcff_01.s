; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tcff_01 — 範囲外のコマンドコード ($40 以上): 受理して画面を変えない
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     6
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$01,$40
        FCB     $00,$01,$7F
        FCB     $00,$01,$80
        FCB     $00,$01,$FF

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     320,100
        FCB     OP_END
        END
