; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-A の位置の ROM
; ta15_01 — $15 直線 (FM77AV の並び): 相対 3〜5=色 B,R,G (各 4 ビット、bit3 のみ使用) /
;   相対 6=演算 (0=PSET) / 相対 7〜14=X0,Y0,X1,Y1 (各 16bit) / 相対 15=形状 (0=直線)
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;   Type-A の位置の ROM は色を 1 バイトで受ける並びなので、この並びは相対 4 の 8 を
;   演算コードとみなして $40 を返し、描かない (動作観察)。応答種別を「エラーコード」
;   ($FB) にして相対 0 を読み戻す。320×200 の画面で走らせる。
; @screen 320
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $FB,$0E,$15,$08,$08,$08,$00,$00,$64,$00,$64,$01,$2C,$00
        FCB     $64,$00

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     100,100
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_PROBE
        FDB     300,100
        FCB     OP_PROBE
        FDB     99,100
        FCB     OP_PROBE
        FDB     301,100
        FCB     OP_END
        END
