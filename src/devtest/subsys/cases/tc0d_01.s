; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc0d_01 — $0D ERASE2: $D383=0 (全画面) のとき背景色 5 で全面を消去する
;   $D383=消去範囲 / $D384=背景色 / $D385=文字色
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$04,$0D,$00,$05,$07

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     639,199
        FCB     OP_PROBE
        FDB     320,100
        FCB     OP_END
        END
