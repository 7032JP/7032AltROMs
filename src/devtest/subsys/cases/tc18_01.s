; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc18_01 — $18 塗りつぶし: 枠の内側を種点から塗る
;   $D383=X(16bit), $D385=Y(16bit), $D387=塗色, $D388=境界色数 N, $D389〜=境界色
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0C,$15,$07,$00,$00,$64,$00,$32,$01,$2C,$00
        FCB     $96,$01
        FCB     $00,$08,$18,$00,$C8,$00,$64,$02,$01,$07

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_CNT,2
        FDB     0,0,639,199
        FCB     OP_CNT,7
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_PROBE
        FDB     100,50
        FCB     OP_PROBE
        FDB     99,49
        FCB     OP_END
        END
