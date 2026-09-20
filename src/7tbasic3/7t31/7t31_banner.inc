; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
; 7T-BASIC 3.1 — コールドスタート時のタイトル・著作権表示 (製品識別 その 1)
;   cold_banner (7t_startup.inc) の語ループの直後に取り込む。文言は ASCII のまま
;   ROM に残り、バイナリ検索で見える。
;   1 行目 (title_tbl) は青の帯の上に SYMBOL で描く。先頭 8 バイトはサブシステムの
;   SYMBOL ($19) の固定部 (F=0 PSET / A=0 / W=2 (40 桁の倍幅) / H=1 / X の上位 0 /
;   Y=0)。続いて語ごとに [色][X 座標の下位 (桁 × 16)][文字数][文字列]、色 0 で終り。
;   2 行目 (msg_title) はコンソールへ出す。語ごとの色は $11 <色> (COLOR 文が
;   コンソールへ送るのと同じ制御コード) を文言の中に置いて切り替える。行送り (LF)
;   のたびに現在の文字色を送り直す (impl_putchar の ipc_lf) ので、次の行と Ready
;   は元の文字色 (白) に戻る。
;   色番号: 0 黒 1 青 2 赤 3 マゼンタ 4 緑 5 シアン 6 黄 7 白
title_tbl:
                FCB     0,0                     ; F = PSET, A = 0
                FCB     2,1                     ; W = 2 (倍幅), H = 1
                FDB     0                       ; X (下位は語ごとに書く)
                FDB     0                       ; Y = 0 (1 行目)
                FCB     5,0,3                   ; シアン, 桁 0
                FCC     "7T-"
                FCB     2,48,1                  ; 赤, 桁 3
                FCC     "B"
                FCB     6,64,1                  ; 黄, 桁 4
                FCC     "A"
                FCB     4,80,1                  ; 緑, 桁 5
                FCC     "S"
                FCB     5,96,1                  ; シアン, 桁 6
                FCC     "I"
                FCB     3,112,1                 ; マゼンタ, 桁 7
                FCC     "C"
                FCB     7,128,12                ; 白, 桁 8
                FCC     " version 3.1"
                FCB     0                       ; 終り
banner_line2:
                JSR     print_inline            ; 直後の文言を出して、その次から続ける
msg_title:
                FCB     $11,7                   ; 白
                FCC     "Copyright (C)2026 "
                FCB     $11,4                   ; 緑
                FCC     "N.Tsugiiwa"
                FCB     $11,5                   ; シアン
                FCC     " @7032"
                FCB     $0D,$0A,0
