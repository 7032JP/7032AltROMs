; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; AltROMs initbase.rom — FM77AV 中位世代 (20 系) メイン CPU イニシエート ROM
;
; 8192 byte 固定。initiate.rom と同じメイン CPU イニシエート ROM だが、機種識別域
; (file offset $0B09-$0B13) の提示内容だけが異なる機種別変種である。
;
;   initiate.rom : 世代 "AV040" / 機種区分 "40" → 拡張表示モード (26 万色) を備える世代
;   initbase.rom : 世代 "AV020" / 機種区分 "20" → 中位世代 (26 万色モードなし)
;   initav1.rom  : 世代 "AV001" / 機種区分なし  → 第 1 世代 (起動機構自体が別)
;
; 実体は initiate/initiate.s を INIT_BASE 条件でアセンブルしたものであり、ソースは
; 一本しか持たない。initiate.rom とのバイト差は機種識別域の 2 byte のみである
; (起動処理そのものは同一)。
;
; 26 万色モードを持たない構成へは本 ROM を配置する。機種識別域の詳細は
; initiate/initiate.s の機種識別域の項を参照。
;==============================================================================

INIT_BASE       EQU     1
                INCLUDE "../initiate/initiate.s"
