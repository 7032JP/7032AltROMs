; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc01_02 — $01 の範囲外パラメータ (桁数 62): 指定は採らず、RESET 時のパラメータ
;   (背景色 0 / 80 桁 / 25 行 / 消去指定あり) で初期化する
;   誤りの番号そのものを読み戻す試験は tc01_04 にある。
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$09,$01,$06,$3E,$19,$00,$19,$00,$01,$00

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     320,100
        FCB     OP_END
        END
