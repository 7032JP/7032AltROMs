; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc03_10 — $1B に一覧に無い 2 バイト目が続くオーダシーケンス
;   $1B に続く 2 バイト目が、参考書籍の一覧 ($23/$22/$39/$67/$68) に
;   無い値のとき、その 2 バイト目は読み捨てられ、共有領域の相対 0 は 0 のまま (動作観察)。
; 準備: 制御コードを動作として扱う表示状態にし ($0C・カーソルは消す)、$02 で消去+ホーム
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$16
        FCB     $00,$01,$02
        FCB     $FB,$04,$03,$02,$1B,$21 ; 一覧に無い 2 バイト目

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
