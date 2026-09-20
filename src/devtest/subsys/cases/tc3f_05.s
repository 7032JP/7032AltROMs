; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc3f_05 — $3F バイトコード: CALL したコードがサブ側から画面の記憶域へ書く。
;   青プレーンの先頭バイトへ $F0 を OR 合成し、画面左上に 4 画素を灯す
;     CE 00 00  LDU #$0000   (青プレーンの先頭)
;     C6 F0     LDB #$F0
;     EA C4     ORB ,U
;     E7 C4     STB ,U
;     39        RTS
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$1E,$3F,$00,$00,$00,$00,$00,$00,$00,$00,$91
        FCB     $D3,$96,$D3,$A8,$00,$0A,$93,$D3,$A8,$90,$CE,$00
        FCB     $00,$C6,$F0,$EA,$C4,$E7,$C4,$39

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     3,0
        FCB     OP_PROBE
        FDB     4,0
        FCB     OP_END
        END
