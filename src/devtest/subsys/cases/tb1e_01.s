; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb1e_01 — $1E 矩形書込み 2: 3 プレーン (B/R/G) のバイトデータを矩形へ書く
;   Type-B の位置の ROM の並び: $D383〜$D38A=矩形 X0,Y0,X1,Y1 / $D38C=ファンクション /
;   $D38D=データ計数 / $D38E〜=1 画素 2 バイト (0000BBBB / RRRRGGGG)。本サンプルは 3 バイト
;   ($F0,$CC,$AA) を送るので、(0,0) は R と G の点いた色 6、残りの画素は 0 になる
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0F,$1E,$00,$00,$00,$00,$00,$07,$00,$00,$00
        FCB     $00,$03,$F0,$CC,$AA

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,319,199
        FCB     OP_CNT,1
        FDB     0,0,7,0
        FCB     OP_CNT,2
        FDB     0,0,7,0
        FCB     OP_CNT,3
        FDB     0,0,7,0
        FCB     OP_CNT,4
        FDB     0,0,7,0
        FCB     OP_CNT,5
        FDB     0,0,7,0
        FCB     OP_CNT,6
        FDB     0,0,7,0
        FCB     OP_CNT,7
        FDB     0,0,7,0
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     1,0
        FCB     OP_PROBE
        FDB     2,0
        FCB     OP_PROBE
        FDB     3,0
        FCB     OP_END
        END
