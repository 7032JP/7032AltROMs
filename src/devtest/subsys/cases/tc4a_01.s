; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc4a_01 — コマンドコード $4A: 一覧に無いので実行されない
;   参考書籍のコマンド一覧に $4A は無く、
;   参考書籍は一覧に無いコードへ $46 (コマンドコードに誤りがあります) を返すと
;   定める。よって塗り潰した画面はそのまま残る。
; 準備: カーソルの点滅を止める ($0C・下位ビット 0)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
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
