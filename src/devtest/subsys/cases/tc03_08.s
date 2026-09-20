; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc03_08 — $1B $39 Erase Key Buffer (参考書籍)
;   行 15 で先行入力を可能にし ($1B $67)、行 25 の待ちの間に届いた打鍵を溜める。
;   1 打目を $29 INKEY で取り出したあと Erase Key Buffer を送ると、溜まっていた
;   残りのキーコードは捨て去られ、次の $29 INKEY は「入力なし」を返す。
; 準備: 制御コードを動作として扱う表示状態にし ($0C・カーソルは消す)、$02 で消去+ホーム
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
; @bas 15 PRINT CHR$(27)+CHR$(103);
; @bas 25 FOR I=1 TO 30000:NEXT
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     5
        FCB     $00,$02,$0C,$16
        FCB     $00,$01,$02
        FCB     $02,$02,$29,$00         ; 1 打目 = "A" ($41) / 入力あり
        FCB     $00,$04,$03,$02,$1B,$39 ; Erase Key Buffer
        FCB     $02,$02,$29,$00         ; 消去後は入力なし

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
