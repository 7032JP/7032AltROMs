; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb0d_01 — $0D ERASE2: $D383=0 (全画面) のとき背景色 5 で全面を消去する
;   Type-B の位置の ROM の並び: $D383=消去範囲 W / $D384〜$D386=背景色 BC の B・R・G
;   (各 4 ビット) / $D387=文字色 FC。本サンプルは B=5・R=7・G=0 を置くので、全面が
;   B と R の点いた色 3 になる
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$04,$0D,$00,$05,$07

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,319,199
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     319,199
        FCB     OP_PROBE
        FDB     160,100
        FCB     OP_END
        END
