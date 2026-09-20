; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc03_09 — $1B $67 Set Buffer Mode (参考書籍)
;   行 15 で先行入力の許可 ($1B $67) を送る。キーバッファはコマンドを受けていない
;   間に届いた打鍵も押された順に保つので、行 25 の待ちの間に届いた 2 文字を、
;   $29 INKEY で 1 打目 ("A" = $41)、2 打目 ("B" = $42) の順に取り出せる。
; 準備: 制御コードを動作として扱う表示状態にし ($0C・カーソルは消す)、$02 で消去+ホーム
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
; @bas 15 PRINT CHR$(27)+CHR$(103);
; @bas 25 FOR I=1 TO 30000:NEXT
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$16
        FCB     $00,$01,$02
        FCB     $02,$02,$29,$00         ; 1 打目 = "A" ($41)
        FCB     $02,$02,$29,$00         ; 2 打目 = "B" ($42)

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
