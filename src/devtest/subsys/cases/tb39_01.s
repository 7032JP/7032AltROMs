; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb39_01 — コマンドコード $39 (FM77AV の並び): 相対 3〜10=X0,Y0,X1,Y1 (各 16bit) /
;   相対 11〜13=枠の色 B,R,G / 相対 14〜16=塗る色 B,R,G (各 4 ビット、bit3 のみ使用)。
;   一覧に載る命令として受理され、1 画素外側に枠を引いてから範囲を塗ることを確かめる
;   (動作観察)。
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; @screen 320
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0F,$39,$00,$64,$00,$32,$00,$C8,$00,$64,$08,$08,$08
        FCB     $00,$08,$00

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,319,199
        FCB     OP_CNT,2
        FDB     0,0,319,199
        FCB     OP_CNT,7
        FDB     0,0,319,199
        FCB     OP_PROBE
        FDB     150,75
        FCB     OP_PROBE
        FDB     99,49
        FCB     OP_PROBE
        FDB     98,48
        FCB     OP_END
        END
