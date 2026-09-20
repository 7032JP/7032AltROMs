; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc1f_01 — $1F グラフィックカーソルの座標数ガード ($D384=0 と $D384=$0B は何もしない)
;   参考書籍の「要求する座標数 N」は 1〜10。
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$07,$1F,$07,$00,$00,$64,$00,$64
        FCB     $00,$07,$1F,$07,$0B,$00,$64,$00,$64

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     100,100
        FCB     OP_END
        END
