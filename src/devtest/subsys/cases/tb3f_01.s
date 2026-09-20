; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-B の位置の ROM (画面は矩形読出し 2 ($1D) の
;   1 画素 2 バイトの応答で観測する。subsysrt.inc の RT_TYPEB)
; tb3f_01 — $3F バイトコード: MEMCPY ($91 src dst len) → STOP ($90)
;   バイトコード列は $D38B から始まる ($D383〜$D38A は使わない)
;   バイトコード自身の先頭 4 byte ($D38B〜) を $D3A0 へ写す
;   → 読み出し領域のオフセット 29 ($D3A0) から 91 d3 8b d3 が並ぶ
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
RT_TYPEB        EQU     1
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $22,$11,$3F,$00,$00,$00,$00,$00,$00,$00,$00,$91
        FCB     $D3,$8B,$D3,$A0,$00,$04,$90

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,319,199
        FCB     OP_END
        END
