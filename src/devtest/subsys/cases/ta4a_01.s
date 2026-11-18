; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-A の位置の ROM
; ta4a_01 — コマンドコード $4A: 受け付けないので実行されない
;   応答が完了し、画面が変わらないことを確かめる (参考書籍)。
; 準備: カーソルの点滅を止める ($0C・下位ビット 0)、$15 (色 1 バイトの並び、形状 2) で
;   全面 (0,0)-(639,199) を塗り潰す
;
; コマンドを発行し、画面を読み戻して照合する。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     4
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$4A
        FCB     $00,$0C,$15,$05,$00,$00,$00,$00,$00,$02,$7F,$00
        FCB     $C7,$02
        FCB     $00,$01,$4A

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     639,199
        FCB     OP_END
        END
