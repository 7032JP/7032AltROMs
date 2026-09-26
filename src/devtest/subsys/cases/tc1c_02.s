; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc1c_02 — $1C 矩形書込み 1: パターンを継続コマンド ($64) 2 回に分けて送る
;   矩形 (0,0)-(15,2) は 1 行 2 バイトで 3 行 = 6 バイト。最初の $1C は継続フラグを
;   1 にして 1 行目の 2 バイトだけを送り、2 行目を $64 (継続フラグ 1)、3 行目を
;   $64 (継続フラグ 0) で送る。$64 は相対 3 = 今回のバイト数、相対 4〜 = パターン。
;   3 行とも、送った位置のパターンが描かれることを確かめる。
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_CONT         EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     5
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $FA,$0E,$1C,$00,$00,$00,$00,$00,$0F,$00,$02,$07,$00,$02
        FCB     $F0,$0F                 ; 1 行目
        FCB     $FA,$04,$64,$02,$0F,$F0 ; 2 行目 (継続フラグ 1)
        FCB     $00,$04,$64,$02,$FF,$00 ; 3 行目 (継続フラグ 0)

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_CNT,7
        FDB     0,0,15,0
        FCB     OP_CNT,7
        FDB     0,1,15,1
        FCB     OP_CNT,7
        FDB     0,2,15,2
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     3,1
        FCB     OP_PROBE
        FDB     4,1
        FCB     OP_PROBE
        FDB     11,1
        FCB     OP_PROBE
        FDB     12,1
        FCB     OP_PROBE
        FDB     7,2
        FCB     OP_PROBE
        FDB     8,2
        FCB     OP_END
        END
