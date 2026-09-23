; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; AltROMs initav1.rom — FM77AV 第 1 世代 メイン CPU イニシエート ROM
;
; 8192 byte 固定。initiate.rom / initbase.rom と同じメイン CPU イニシエート ROM
; だが、**起動機構そのものが異なる世代** 向けの変種である。
;
; 本変種の構成差 (initiate.rom との差):
;   1. AV ブートイメージを持たない (file offset $1C00 は空き)。
;   2. AV ブートイメージの転送処理を持たない。起動用 ROM イメージを resident boot
;      領域へ複写してその場で実行する。
;   3. トランポリンの引渡し先が resident boot ($FE00) 直行。
;   4. 機種識別域の世代名が "AV001"、機種区分は非提示 ($FF x 6)。
;
; 実体は initiate/initiate.s を INIT_AV1 条件でアセンブルしたものであり、
; ソースは一本しか持たない。
;
; 配置先のファイル名は他の変種と同じ initiate.rom である。
; どの変種を配るかは、ビルドで生成した変種のうち 1 つを配置先へ
; initiate.rom という名前で置くことで選ぶ。
;==============================================================================

INIT_AV1        EQU     1
                INCLUDE "../initiate/initiate.s"
