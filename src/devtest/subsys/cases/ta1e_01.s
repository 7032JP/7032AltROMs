; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-A の位置の ROM
; ta1e_01 — $1E 矩形書込み 2: 3 プレーン (B/R/G) のバイトデータを矩形へ書く
;   $D38C=ファンクション (0=SET), $D38D=データ計数, $D38E〜= B,R,G 各プレーン
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0F,$1E,$00,$00,$00,$00,$00,$07,$00,$00,$00
        FCB     $00,$03,$F0,$CC,$AA

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
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
