; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc3f_06 — $3F バイトコード: 2 回の合成を 1 本の CALL で続けて行う。
;   $F0 を OR 合成して灯した 4 画素へ $C0 を EOR 合成すると上位 2 画素が消え、
;   $30 の 2 画素だけが残る。展開先は $D3C0 (転送元と重ならないアドレス) に採る
;     CE 00 00 C6 F0 EA C4 E7 C4   (OR  $F0)
;     CE 00 00 C6 C0 E8 C4 E7 C4   (EOR $C0)
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
        FCB     $00,$C6,$F0,$EA,$C4,$E7,$C4,$CE,$00,$00,$C6,$C0
        FCB     $E8,$C4,$E7,$C4,$39

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
