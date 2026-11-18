; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc3f_04 — $3F バイトコード: CALL したコードがサブ側の作業領域へ書き戻す。
;   応答領域 40 バイトのうち、CALL したコードが書いた 2 バイトが 0 でない値に変わることを見る
;     CC A5 5A  LDD #$A55A
;     B7 D3 A5  STA $D3A5  (A = $A5)
;     F7 D3 A6  STB $D3A6  (B = $5A)
;     39        RTS
;   → 読み出し領域のオフセット 34/35 ($D3A5/$D3A6) が A5 5A (発行の前は 00)。
;     オフセット 37〜39 は MEMCPY が写したコードの先頭 3 バイト
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; コマンドを発行し、応答と画面を読み戻して照合する。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $28,$1E,$3F,$00,$00,$00,$00,$00,$00,$00,$00,$91
        FCB     $D3,$96,$D3,$A8,$00,$0A,$93,$D3,$A8,$90,$CC,$A5
        FCB     $5A,$B7,$D3,$A5,$F7,$D3,$A6,$39

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
