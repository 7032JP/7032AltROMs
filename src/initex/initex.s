; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; AltROMs initex.rom — FM77AV 拡張世代 (EX / SX 系) メイン CPU イニシエート ROM
;
; 8192 byte 固定。initiate.rom / initbase.rom / initav1.rom と同じメイン CPU
; イニシエート ROM の機種別変種である。
;
; 本変種の構成差 (initiate.rom との差) は機種識別域の中身だけ (世代名 "AV4EX" と
; 拡張世代の識別値。initiate.s の機種識別域の項を参照)。
;
; 起動処理は上位世代 (initiate.rom) と同じで、AV ブートイメージ (avboot.bin) も
; 同じものを取込む。フロッピー以外の起動デバイスからの起動は持たない。
;
; ★ ファイルの長さは 8192 byte。
;
; 実体は initiate/initiate.s を INIT_EXSX 条件でアセンブルしたものであり、
; ソースは一本しか持たない。
;
; 配置先のファイル名は他の変種と同じ initiate.rom である。
; どの変種を配るかは、ビルドで生成した変種のうち 1 つを配置先へ
; initiate.rom という名前で置くことで選ぶ。
;==============================================================================

INIT_EXSX       EQU     1
                INCLUDE "../initiate/initiate.s"
