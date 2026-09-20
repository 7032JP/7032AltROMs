; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tcbas_05 — CIRCLE (多角形近似): 輪郭の通る位置と左右端の色
; 多角形の頂点の数と縦横比は実装で決まり、輪郭の画素の総数も縦の端の位置もそれで
; 変わるので数えない。横の端が x=220 と x=420 にあること、その外側に画素が無いこと、
; 中心を通る縦の帯の上下どちらにも輪郭があること、内側が空であることを、
; 画素の有無 (OP_ANY / OP_ANYC) で見る。
; BASIC の描画文で画面を作り、その結果をこのサンプル自身が読み戻して報告します。
; 先頭の `@bas` 行が、機械語を読み込んだあとに走らせる BASIC の行です。
;
; @bas 21 CIRCLE(320,100),100,7
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     1
        FCB     $00,$02,$0C,$16         ; カーソルを伏せる (制御コードの解釈は残す)

OBSTAB:
        FCB     OP_PROBE                ; 右端
        FDB     420,100
        FCB     OP_PROBE                ; 左端
        FDB     220,100
        FCB     OP_PROBE                ; 中心は空
        FDB     320,100
        FCB     OP_ANYC,7               ; 中心を通る縦の帯・上半分
        FDB     316,0,324,99
        FCB     OP_ANYC,7               ; 同・下半分
        FDB     316,101,324,199
        FCB     OP_ANY                  ; 左端より左は空
        FDB     0,0,219,199
        FCB     OP_ANY                  ; 右端より右は空
        FDB     421,0,639,199
        FCB     OP_ANY                  ; 内側は空
        FDB     300,90,340,110
        FCB     OP_END
        END
