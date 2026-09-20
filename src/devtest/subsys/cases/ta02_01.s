; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-A の位置の ROM
; ta02_01 — $02 CLS: 直線を引いた後に全画面を消去する
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
        FCB     $00,$01,$02

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     200,100
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_END
        END
