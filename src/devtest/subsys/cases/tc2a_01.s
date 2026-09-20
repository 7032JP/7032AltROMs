; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc2a_01 — $2A 機能キー文字列の定義 → $2B で同じ文字列を読み戻す
;   $2A: $D383=キー番号, $D384=文字数, $D385〜=文字列 ("HI")
;   $2B: $D383=キー番号 → $D384〜$D393 に 16 byte (先頭 = 長さ)
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$05,$2A,$01,$02,$48,$49
        FCB     $11,$02,$2B,$01

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
