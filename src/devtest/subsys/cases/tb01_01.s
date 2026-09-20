; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb01_01 — $01 コンソールイニシャライズ: Type-C と同じ並び (背景色 6・80 桁 25 行・消去指定あり)
;   Type-B の位置の ROM は、どのパラメータにも誤りの番号 $3C を返し、12 面を 0 で
;   消してカーソルをホームへ戻す (背景色の指定は画面に出ない。動作観察)
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$09,$01,$06,$50,$19,$00,$19,$00,$01,$00

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
