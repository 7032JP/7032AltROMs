; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tcbas_02 — PAINT (境界色つき): 枠内の塗り画素数
; BASIC の描画文で画面を作り、その結果をこのサンプル自身が読み戻して報告します。
; 先頭の `@bas` 行が、機械語を読み込んだあとに走らせる BASIC の行です。
;
; @bas 21 LINE(100,50)-(300,150),PSET,7,B
; @bas 22 PAINT(200,100),2,7
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     1
        FCB     $00,$02,$0C,$16         ; カーソルを伏せる (制御コードの解釈は残す)

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_CNT,2
        FDB     100,50,300,150
        FCB     OP_CNT,7
        FDB     100,50,300,150
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_PROBE
        FDB     100,50
        FCB     OP_PROBE
        FDB     99,49
        FCB     OP_END
        END
