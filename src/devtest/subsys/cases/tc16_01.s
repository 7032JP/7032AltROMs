; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc16_01 — $16 折線 (CHAIN): $D383=色, $D384=ファンクション, $D385=点数 N,
;   $D386〜= N 組の (X,Y) 各 16bit
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$10,$16,$06,$00,$03,$00,$32,$00,$32,$00,$96
        FCB     $00,$96,$00,$FA,$00,$32

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     50,50
        FCB     OP_PROBE
        FDB     150,150
        FCB     OP_PROBE
        FDB     250,50
        FCB     OP_END
        END
