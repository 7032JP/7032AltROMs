; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tcbas_03 — SYMBOL (倍率つき): 横 2 倍・縦 3 倍の枠の中だけにグリフが描かれる
; BASIC の描画文で画面を作り、その結果をこのサンプル自身が読み戻して報告します。
; 先頭の `@bas` 行が、機械語を読み込んだあとに走らせる BASIC の行です。
;
; @bas 21 SYMBOL(100,100),"A",2,3,7
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     1
        FCB     $00,$02,$0C,$16         ; カーソルを伏せる (制御コードの解釈は残す)

OBSTAB:
        FCB     OP_ANYC,7
        FDB     100,100,115,123         ; 横 2 倍・縦 3 倍の枠 16x24 に色 7 の点がある
        FCB     OP_LIT
        FDB     0,0,639,99              ; 枠より上: 何も出ない
        FCB     OP_LIT
        FDB     0,124,639,199           ; 枠より下: 何も出ない
        FCB     OP_LIT
        FDB     0,100,99,123            ; 枠の左: 何も出ない
        FCB     OP_LIT
        FDB     116,100,639,123         ; 枠の右: 何も出ない
        FCB     OP_END
        END
