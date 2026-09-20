; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tcbas_01 — LINE (直線・矩形枠 B・塗潰し矩形 BF): 3 図形の画素数と色
; BASIC の描画文で画面を作り、その結果をこのサンプル自身が読み戻して報告します。
; 先頭の `@bas` 行が、機械語を読み込んだあとに走らせる BASIC の行です。
;
; @bas 21 LINE(100,50)-(300,150),PSET,3,B
; @bas 22 LINE(400,60)-(500,140),PSET,5,BF
; @bas 23 LINE(20,20)-(620,20),PSET,7
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     1
        FCB     $00,$02,$0C,$16         ; カーソルを伏せる (制御コードの解釈は残す)

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_CNT,3
        FDB     100,50,300,150
        FCB     OP_CNT,5
        FDB     400,60,500,140
        FCB     OP_ANYC,7               ; 幅 601 の 1 行: 最後のバイトの余りのビットは不定 (参考書籍)
        FDB     20,20,620,20
        FCB     OP_PROBE
        FDB     100,50
        FCB     OP_PROBE
        FDB     300,150
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_PROBE
        FDB     400,60
        FCB     OP_PROBE
        FDB     450,80
        FCB     OP_END
        END
