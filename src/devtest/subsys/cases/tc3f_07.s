; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc3f_07 — $3F バイトコード: CALL から戻った後も解釈が続く。
;   MEMCPY ($91) → CALL ($93) → MEMCPY ($91) → STOP ($90) の順に並べ、
;   CALL の後ろの MEMCPY が実行されたことを応答領域で確かめる
;   バイトコードは $D38B から 17 byte、STOP が $D39C、機械語は $D39D から
;     86 A5     LDA #$A5
;     B7 D3 A6  STA $D3A6
;     39        RTS
;   → オフセット 35 ($D3A6) が a5 (CALL したコードが書いた)
;     オフセット 33,34 ($D3A4,$D3A5) が 86 a5 (CALL の後ろの MEMCPY が写した)
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $28,$21,$3F,$00,$00,$00,$00,$00,$00,$00,$00,$91
        FCB     $D3,$9D,$D3,$A8,$00,$06,$93,$D3,$A8,$91,$D3,$9D
        FCB     $D3,$A4,$00,$02,$90,$86,$A5,$B7,$D3,$A6,$39

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
