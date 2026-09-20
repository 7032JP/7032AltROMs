; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tcbas_06 — PSET / PRESET (点の描画と消去): 3 点のうち 1 点が消える
; BASIC の描画文で画面を作り、その結果をこのサンプル自身が読み戻して報告します。
; 先頭の `@bas` 行が、機械語を読み込んだあとに走らせる BASIC の行です。
;
; @bas 21 PSET(100,100,3)
; @bas 22 PSET(200,100,5)
; @bas 23 PSET(300,100,7)
; @bas 24 PRESET(200,100)
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     1
        FCB     $00,$02,$0C,$16         ; カーソルを伏せる (制御コードの解釈は残す)

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_CNT,3
        FDB     100,100,300,100
        FCB     OP_CNT,7
        FDB     100,100,300,100
        FCB     OP_PROBE
        FDB     100,100
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_PROBE
        FDB     300,100
        FCB     OP_END
        END
