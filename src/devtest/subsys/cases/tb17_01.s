; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb17_01 — $17 点列: Type-C の並び ($D383=点数 N, 以降 6 byte 周期で X(16bit) Y(16bit) 色 ファンクション)
;   Type-B の位置の ROM は 1 点 8 バイト (X・Y 各 2、B・R・G、演算) で読む。本サンプルの
;   1 点目の演算は $14 (5 以上) になるので、誤り $40 で何も描かない
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$14,$17,$03,$00,$0A,$00,$0A,$07,$00,$00,$14
        FCB     $00,$14,$03,$00,$00,$1E,$00,$1E,$05,$00

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,319,199
        FCB     OP_CNT,3
        FDB     0,0,39,39
        FCB     OP_CNT,5
        FDB     0,0,39,39
        FCB     OP_CNT,7
        FDB     0,0,39,39
        FCB     OP_PROBE
        FDB     10,10
        FCB     OP_PROBE
        FDB     20,20
        FCB     OP_PROBE
        FDB     30,30
        FCB     OP_PROBE
        FDB     11,10
        FCB     OP_END
        END
