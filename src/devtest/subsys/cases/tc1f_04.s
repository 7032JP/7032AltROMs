; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc1f_04 — $1F GRAPHIC CURSOR の数字キー [9] (参考書籍)
;   初期座標 (100,100) で 1 個の座標を要求する。打鍵 (tc1f_04.stdin) は
;   [9] [→] [→] [RETURN] で、数字キー [9] が 1 ステップの移動ドット数を 9 に定め、
;   移動キー 2 回で X が 18 増え、改行キーで (118,100) が読み取られる。
;   行 15 で先行入力の許可 ($1B $67) を送ってキー入力の状態を明示する。
; 準備: 制御コードを動作として扱う表示状態にし ($0C・カーソルは消す)、$02 で消去+ホーム
;
; コマンドを発行し、応答と画面を読み戻して照合する。
; @bas 15 PRINT CHR$(27)+CHR$(103);
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$16
        FCB     $00,$01,$02
        FCB     $04,$07,$1F,$07,$01,$00,$64,$00,$64

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
