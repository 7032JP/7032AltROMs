; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;
; tc03_02 — $03 コンソール文字出力: グラフィック文字 $E0〜$FF の 32 コード。
;   参考書籍の文字構成はグラフィックパターン 62 種を挙げ、
;   参考書籍のキャラクタコード表と 参考書籍の特殊記号・
;   グラフィック領域は $E0〜$FF に図形シンボル (トランプ記号・丸・年月日などの
;   省略記号・市松・三角・×印・罫線) を置く。32 コードを 4 コードずつ 8 区画に
;   分け、どの区画にも字形の点があることを確かめる。字形の点の数は ROM ごとに
;   違うので数えない。
;   40 桁 (倍幅) の 1 セルは横 16 画素・縦 10 画素なので、4 コードで横 64 画素。
; 準備: カーソルの点滅を止め ($0C・下位ビット 0)、全画面を消去する ($02・全画面/黒)
;
; このサンプル自身がコマンドを発行し、応答と表示結果を読み戻します。
        INCLUDE "subsysrt.inc"

CMDTAB:
        FCB     3
        FCB     $00,$02,$0C,$00
        FCB     $00,$01,$02
        FCB     $01,$22,$03,$20
        FCB     $E0,$E1,$E2,$E3,$E4,$E5,$E6,$E7
        FCB     $E8,$E9,$EA,$EB,$EC,$ED,$EE,$EF
        FCB     $F0,$F1,$F2,$F3,$F4,$F5,$F6,$F7
        FCB     $F8,$F9,$FA,$FB,$FC,$FD,$FE,$FF

OBSTAB:
        FCB     OP_ANY
        FDB     0,0,63,9                ; $E0〜$E3
        FCB     OP_ANY
        FDB     64,0,127,9              ; $E4〜$E7
        FCB     OP_ANY
        FDB     128,0,191,9             ; $E8〜$EB
        FCB     OP_ANY
        FDB     192,0,255,9             ; $EC〜$EF
        FCB     OP_ANY
        FDB     256,0,319,9             ; $F0〜$F3
        FCB     OP_ANY
        FDB     320,0,383,9             ; $F4〜$F7
        FCB     OP_ANY
        FDB     384,0,447,9             ; $F8〜$FB
        FCB     OP_ANY
        FDB     448,0,511,9             ; $FC〜$FF
        FCB     OP_END
        END
