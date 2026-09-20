; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; 対象: FM77AV 系 / サブシステム Type-A の位置の ROM
; ta01_01 — $01 コンソールイニシャライズ: 背景色 6 (シアン)・80 桁 25 行・消去指定あり
;   $D383=背景色 / $D384=桁数 / $D385=行数 / $D386=範囲上端 / $D387=範囲行数 /
;   $D388=ファンクション行表示 / $D389=消去指定 / $D38A=単色表示 (GR)
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$09,$01,$06,$50,$19,$00,$19,$00,$01,$00

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     639,199
        FCB     OP_PROBE
        FDB     320,100
        FCB     OP_END
        END
