; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb1b_01 — $1B 矩形読出し 1: Type-B の位置の ROM は受け付けない
;   Type-B の位置の ROM は $1B に $46 (コマンドコードに誤りがあります) を返し、応答の
;   データを置かない (動作観察)。応答種別を「エラーコード」($FB) にして相対 0 を読み戻す。
;   $D383=X1(16bit), $D385=Y1, $D387=X2, $D389=Y2, $D38B=指定色数, $D38C〜=色コード
;   先に 8x2 の塗潰し矩形を FM77AV の並びの $15 (形状 2) で置き、その左上 16x2 画素を読む
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; @screen 320
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$0E,$15,$08,$08,$08,$00,$00,$00,$00,$00,$00,$07,$00
        FCB     $01,$02
        FCB     $FB,$0B,$1B,$00,$00,$00,$00,$00,$0F,$00,$01,$01
        FCB     $07

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,319,199
        FCB     OP_END
        END
