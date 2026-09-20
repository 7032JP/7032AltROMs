; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc03_07 — $1B $23 Lock Keyboard / $1B $22 Unlock Keyboard (参考書籍)
;   行 15 でキー入力を禁止し、行 25 の待ちの間に打鍵 (tc03_07.stdin) が届く。
;   禁止中の打鍵は 1 文字もキーバッファへ入らないので、禁止中の $29 INKEY も、
;   禁止を解いた後の $29 INKEY も「入力なし」($D384 = 0) を返す。
; 準備: 制御コードを動作として扱う表示状態にし ($0C・カーソルは消す)、$02 で消去+ホーム
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
; @bas 15 PRINT CHR$(27)+CHR$(35);
; @bas 25 FOR I=1 TO 30000:NEXT
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     5
        FCB     $00,$02,$0C,$16
        FCB     $00,$01,$02
        FCB     $02,$02,$29,$00         ; 禁止中: キーコード $00 / 入力なし
        FCB     $00,$04,$03,$02,$1B,$22 ; Unlock Keyboard
        FCB     $02,$02,$29,$00         ; 解除後も 1 文字も入っていない

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_END
        END
