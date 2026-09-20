; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb18_01 — $18 塗りつぶし (FM77AV の並び): 種点 X,Y / 塗る色 B,R,G (各 4 ビット、
;   bit3 のみ使用) / 境界色の数 N / 境界色 B,R,G × N。枠の内側を種点から塗る
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)、
;   矩形の 4 辺枠 ($15 形状 1) を引く
;
; @screen 320
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0E,$15,$08,$08,$08,$00,$00,$64,$00,$32,$01,$2C,$00
        FCB     $96,$01
        FCB     $00,$0C,$18,$00,$C8,$00,$64,$00,$08,$00,$01,$08,$08,$08

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,319,199
        FCB     OP_CNT,2
        FDB     0,0,319,199
        FCB     OP_CNT,7
        FDB     0,0,319,199
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_PROBE
        FDB     100,50
        FCB     OP_PROBE
        FDB     99,49
        FCB     OP_END
        END
