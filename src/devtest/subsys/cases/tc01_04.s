; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc01_04 — $01 のパラメータに誤りがあったときは、誤りの番号 $3C を返したうえで
;   RESET 時のパラメータ (BC=0 / NC=80 / NL=25 / SL=0 / NS=25 / FD=0 / ERS=1 / GR=0)
;   で初期化する
;   先に $0D で画面を色 5 の一面にしてから、桁数に 62 ($3E) を与えた $01 を発行する。
;   RESET 時のパラメータは消去指定ありなので、画面は背景色 0 (黒) の一面へ戻る。
; 準備: カーソルの点滅を止める ($0C・下位ビット 0)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$04,$0D,$00,$05,$07
        FCB     $FB,$09,$01,$06,$3E,$19,$00,$19,$00,$01,$01

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199             ; 黒以外の画素は無い (背景色 0 で消えている)
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     639,199
        FCB     OP_END
        END
