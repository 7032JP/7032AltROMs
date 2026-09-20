; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc19_01 — $19 SYMBOL: 文字パターンを横 2 倍・縦 3 倍で描画
;   $D383=色, $D384=ファンクション, $D385=方向, $D386=横倍率, $D387=縦倍率,
;   $D388=X(16bit), $D38A=Y(16bit), $D38C=文字数, $D38D〜=文字列 ("A")
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0C,$19,$07,$00,$00,$02,$03,$00,$64,$00,$64
        FCB     $01,$41

OBSTAB:
        FCB     OP_ANYC,7
        FDB     100,100,115,123         ; 横 2 倍・縦 3 倍の枠 16x24 に色 7 の点がある
        FCB     OP_LIT
        FDB     0,0,639,99              ; 枠より上: 何も出ない
        FCB     OP_LIT
        FDB     0,124,639,199           ; 枠より下: 何も出ない
        FCB     OP_LIT
        FDB     0,100,99,123            ; 枠の左: 何も出ない
        FCB     OP_LIT
        FDB     116,100,639,123         ; 枠の右: 何も出ない
        FCB     OP_END
        END
