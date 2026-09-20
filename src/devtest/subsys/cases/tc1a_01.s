; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc1a_01 — $1A 矩形内の色置換: 色 7 の画素だけを色 2 へ置換える
;   パラメータの割付は参考書籍のとおり
;     相対 3,4=X1 / 5,6=Y1 / 7,8=X2 / 9,10=Y2 / 11=変更するカラーの数 N (1〜8) /
;     12=旧カラーコード 1 / 13=新カラーコード 1 (以下 N 組)
;   ここでは N=1 で (旧 7, 新 2) の 1 組だけを渡す
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0C,$15,$07,$00,$00,$64,$00,$64,$01,$2C,$00
        FCB     $64,$00
        FCB     $00,$0C,$1A,$00,$96,$00,$5A,$01,$2C,$00,$6E,$01
        FCB     $07,$02

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_CNT,2
        FDB     0,0,639,199
        FCB     OP_CNT,7
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_PROBE
        FDB     100,100
        FCB     OP_END
        END
