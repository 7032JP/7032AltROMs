; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb03_01 — $03 コンソール文字出力: $D383=文字数, $D384〜=文字列 ("AB")
;   応答は相対 0 のエラーコードだけ (参考書籍)。相対 3 は 0 で返る
;   画面は 40 桁 25 行 (1 桁 8 ドット)。字形に依存しないよう、書いた桁に点が
;   あることと、それ以外に点が無いことを見る
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $01,$04,$03,$02,$41,$42

OBSTAB:
        FCB     OP_ANY
        FDB     0,0,7,7                 ; 桁 0 の "A"
        FCB     OP_ANY
        FDB     8,0,15,7                ; 桁 1 の "B"
        FCB     OP_LIT
        FDB     16,0,319,7              ; 同じ行の残り: 何も出ない
        FCB     OP_LIT
        FDB     0,8,319,199             ; 2 行目以降: 何も出ない
        FCB     OP_END
        END
