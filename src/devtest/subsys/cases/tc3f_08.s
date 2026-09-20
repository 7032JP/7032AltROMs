; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc3f_08 — $3F バイトコード: 合成でビットを消す向きの操作。
;   $F0 を OR 合成して灯した 4 画素へ $CF を AND 合成すると下位 2 画素が消え、
;   $C0 の 2 画素だけが残る。展開先は $D3C0 (転送元と重ならないアドレス) に採る
;     CE 00 00 C6 F0 EA C4 E7 C4   (OR  $F0)
;     CE 00 00 C6 CF E4 C4 E7 C4   (AND $CF = $30 のビットを消す)
;     39
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $00,$27,$3F,$00,$00,$00,$00,$00,$00,$00,$00,$91
        FCB     $D3,$96,$D3,$C0,$00,$13,$93,$D3,$C0,$90,$CE,$00
        FCB     $00,$C6,$F0,$EA,$C4,$E7,$C4,$CE,$00,$00,$C6,$CF
        FCB     $E4,$C4,$E7,$C4,$39

OBSTAB:
        FCB     OP_LIT
        FDB     0,0,639,199
        FCB     OP_PROBE
        FDB     0,0
        FCB     OP_PROBE
        FDB     1,0
        FCB     OP_PROBE
        FDB     2,0
        FCB     OP_PROBE
        FDB     3,0
        FCB     OP_END
        END
