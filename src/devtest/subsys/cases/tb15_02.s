; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb15_02 — $15 形状 1 (FM77AV の並び): 矩形の 4 辺枠
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; @screen 320
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0E,$15,$08,$08,$00,$00,$00,$64,$00,$32,$01,$2C,$00
        FCB     $96,$01

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,319,199
        FCB     OP_PROBE
        FDB     100,50
        FCB     OP_PROBE
        FDB     300,150
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_PROBE
        FDB     200,50
        FCB     OP_END
        END
