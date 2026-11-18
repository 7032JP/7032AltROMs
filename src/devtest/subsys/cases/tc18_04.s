; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc18_04 — $18 種点が画面外 (X=700): 何も塗らず、エラーコード $3F (グラフィック座標値の
;   誤り) を返す (参考書籍)
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; コマンドを発行し、応答と画面を読み戻して照合する。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $FB,$08,$18,$02,$BC,$00,$64,$02,$01,$01

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
