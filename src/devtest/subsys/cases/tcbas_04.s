; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tcbas_04 — PUT@ (配列の内容を矩形へ書込み): 1 行おきに灯る 16x16 の矩形
; BASIC の描画文で画面を作り、その結果をこのサンプル自身が読み戻して報告します。
; 先頭の `@bas` 行が、機械語を読み込んだあとに走らせる BASIC の行です。
;
; @bas 21 DIM P%(200)
; @bas 22 FOR I=0 TO 15 STEP 2:P%(I)=-1:NEXT
; @bas 23 PUT@(200,0)-(215,15),P%,PSET
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     1
        FCB     $00,$02,$0C,$16         ; カーソルを伏せる (制御コードの解釈は残す)

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     200,0
        FCB     OP_PROBE
        FDB     200,1
        FCB     OP_PROBE
        FDB     215,0
        FCB     OP_PROBE
        FDB     216,0
        FCB     OP_END
        END
