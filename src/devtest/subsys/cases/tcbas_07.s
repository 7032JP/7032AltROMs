; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tcbas_07 — 機械語 I/O サービスの OUTPUT (リクエスト番号 20 = $14):
;   制御ブロック (RCB) で渡した 3 文字が描かれる
; BASIC の描画文で画面を作り、その結果をこのサンプル自身が読み戻して報告します。
; 先頭の `@bas` 行が、機械語を読み込んだあとに走らせる BASIC の行です。
;
;   機械語 I/O サービスは、RCB の先頭を X に置き `JSR [$FBFA]` で呼ぶ。
;   RCB の割付:
;     +0 リクエスト番号 = 20 ($14) / +1 エラーステータス /
;     +2,3 データバッファ先頭アドレス / +4,5 データバイト数 (16 ビット)
;
; @bas 21 FOR I=0 TO 7:READ V:POKE &H7000+I,V:NEXT
; @bas 22 FOR I=0 TO 5:READ V:POKE &H7010+I,V:NEXT
; @bas 23 FOR I=0 TO 2:READ V:POKE &H7020+I,V:NEXT
; @bas 24 EXEC &H7000
; @bas 2000 DATA &H8E,&H70,&H10,&HAD,&H9F,&HFB,&HFA,&H39
; @bas 2010 DATA &H14,&H00,&H70,&H20,&H00,&H03
; @bas 2020 DATA &H58,&H59,&H5A
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     1
        FCB     $00,$02,$0C,$16         ; カーソルを伏せる (制御コードの解釈は残す)

OBSTAB:
        FCB     OP_ANYC,7
        FDB     0,0,15,9                ; 桁 0 の "X"
        FCB     OP_ANYC,7
        FDB     16,0,31,9               ; 桁 1 の "Y"
        FCB     OP_ANYC,7
        FDB     32,0,47,9               ; 桁 2 の "Z"
        FCB     OP_LIT
        FDB     48,0,639,9              ; 同じ行の残り: 何も出ない
        FCB     OP_LIT
        FDB     0,10,639,199            ; 2 行目以降: 何も出ない
        FCB     OP_END
        END
