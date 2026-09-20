; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc05_01 — $05 GETC: 変更フィールド無し ($D383=終了キー 0 / $D384=変更数 0)
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $02,$01,$05

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
