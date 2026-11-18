; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc18_03 — $18 PAINT の境界色の数 Nc = 0 (参考書籍)
;   境界色の数 0 での塗りと応答を確認する。(動作観察)
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; コマンドを発行し、応答と画面を読み戻して照合する。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $FB,$07,$18,$00,$64,$00,$64,$02,$00

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
