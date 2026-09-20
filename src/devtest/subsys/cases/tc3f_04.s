; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc3f_04 — $3F バイトコード: CALL したコードがサブ側の作業領域へ書き戻す。
;   応答領域 40 バイトのうち、CALL で書いた 2 バイトだけが変わることを見る
;     4F        CLRA
;     5F        CLRB
;     12        NOP        (転送量を 10 byte に揃える詰め物)
;     B7 D3 A5  STA $D3A5  (A = 0)
;     F7 D3 A6  STB $D3A6  (B = 0)
;     39        RTS
;   → 読み出し領域のオフセット 34/35 ($D3A5/$D3A6) がともに 00
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $28,$1E,$3F,$00,$00,$00,$00,$00,$00,$00,$00,$91
        FCB     $D3,$96,$D3,$A8,$00,$0A,$93,$D3,$A8,$90,$4F,$5F
        FCB     $12,$B7,$D3,$A5,$F7,$D3,$A6,$39

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
