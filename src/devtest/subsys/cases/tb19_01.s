; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb19_01 — $19 SYMBOL: 文字パターンを横 2 倍・縦 3 倍で描画
;   Type-C の並び: $D383=色, $D384=ファンクション, $D385=方向, $D386=横倍率, $D387=縦倍率,
;   $D388=X(16bit), $D38A=Y(16bit), $D38C=文字数, $D38D〜=文字列 ("A")
;   Type-B の位置の ROM は $D383〜$D385=B・R・G / $D386=ファンクション / $D387=方向 /
;   $D388=横倍率 / $D389=縦倍率 / $D38A〜$D38D=X,Y / $D38E=文字数 / $D38F〜=文字列 で
;   読む。本サンプルの並びでは文字数が 0、Y が 199 を超える値になるので、何も描かない
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0C,$19,$07,$00,$00,$02,$03,$00,$64,$00,$64
        FCB     $01,$41

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,319,199
        FCB     OP_PROBE
        FDB     106,100
        FCB     OP_PROBE
        FDB     102,106
        FCB     OP_PROBE
        FDB     104,112
        FCB     OP_PROBE
        FDB     100,100
        FCB     OP_END
        END
