; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc3f_03 — $3F バイトコード: MEMCPY で 6 byte の機械語を $D3A8 へ展開し、
;   CALL ($93 addr) で実行する。機械語は $D3A5 へ $5A を書いて戻る
;     86 5A     LDA #$5A
;     B7 D3 A5  STA $D3A5
;     39        RTS
;   → 読み出し領域のオフセット 34 ($D3A5) が 5a になる
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $28,$1A,$3F,$00,$00,$00,$00,$00,$00,$00,$00,$91
        FCB     $D3,$96,$D3,$A8,$00,$06,$93,$D3,$A8,$90,$86,$5A
        FCB     $B7,$D3,$A5,$39

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
