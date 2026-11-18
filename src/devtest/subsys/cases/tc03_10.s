; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc03_10 — $1B に受け付けない 2 バイト目が続くオーダシーケンス
;   未対応の ESC 後続バイトを受けた際の応答を確認する。(動作観察)
; 準備: 制御コードを動作として扱う表示状態にし ($0C・カーソルは消す)、$02 で消去+ホーム
;
; コマンドを発行し、応答と画面を読み戻して照合する。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$16
        FCB     $00,$01,$02
        FCB     $FB,$04,$03,$02,$1B,$21 ; 受け付けない 2 バイト目

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
