; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb22_01 — コマンドコード $22 (FM77AV の並び): 相対 3=変える所 (bit0/bit1/bit2) /
;   相対 4〜6=P0/P1/P2。一覧に載る命令として受理されることを確かめる (動作観察)。
;   bit0 (表示ページ・処理ページに影響しない bit) だけを変えて発行し、応答種別を
;   「エラーコード」($FB) にして相対 0 に誤りが立たない (= $00) ことと、塗り潰した
;   画面がそのまま残ることを確かめる。
; 準備: カーソルの点滅を止める ($0C・下位ビット 0)、FM77AV の並びの $15 (形状 2) で
;   全面 (0,0)-(319,199) を塗り潰す
;
; @screen 320
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$0E,$15,$08,$00,$08,$00,$00,$00,$00,$00,$01,$3F,$00
        FCB     $C7,$02
        FCB     $FB,5,$22,$01,$01,$00,$00

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,319,199
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     319,199
        FCB     OP_PROBE
        FDB     160,100
        FCB     OP_END
        END
