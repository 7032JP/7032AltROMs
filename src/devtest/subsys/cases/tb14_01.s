; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb14_01 — コマンドコード $14: 未対応として $46 を返し、画面を変えない
;   色 7 を添えて発行する。Type-B の位置の ROM は $14 を持たず、相対 0 に $46
;   (コマンドコードに誤りがあります) を置いて画面を変えない。応答種別を「エラーコード」
;   ($FB) にして相対 0 を読み戻し、塗り潰した画面がそのまま残ることも確かめる。
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
        FCB     $FB,2,$14,$07

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
