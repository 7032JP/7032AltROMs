; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc04_07 — $04 GET: [CTRL]+[C] は入力を終結し、終了キーコード K=$01 を返す。
;   表示済みのフィールドを変更せずに [CTRL]+[C] を送るので、応答は K=$01, N=0
;   になる。打鍵は tc04_07.stdin から送る。SHIFT 補助移動は対象にしない。
; 準備: 制御コードを動作として扱う表示状態にし ($0C・カーソルは消す)、$02 で消去+ホーム。
;
; 行 15 で先行入力の許可 ($1B $67) を送ってキー入力の状態を明示する。
; @bas 15 PRINT CHR$(27)+CHR$(103);
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$16
        FCB     $00,$01,$02
        FCB     $00,$0A,$03,$08,$12,$00,$00,$11,$07,$43,$41,$4E
        FCB     $02,$01,$04

OBSTAB:
        FCB     OP_END
        END
