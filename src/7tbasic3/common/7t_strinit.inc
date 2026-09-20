; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 文字列領域 — 領域の張り直し
;   ROM の空きはアドレスアンカで区切られた区画ごとに独立しているため、文字列領域の
;   実装は複数の空きプールへ分けて配置してある。参照は全てラベル経由で位置
;   依存はない。
;==============================================================================
; str_space_init — MEMTOP から文字列領域と枠列を張り直す。
str_space_init:
                LDD     MEMTOP
                SUBD    #STRSPC_DEF
                STD     STRBOT          ; 文字領域の下限 = システムスタックの上端
                STD     EXEC_SP         ; 枠列は空 (起動時。warm_start が LDS する)
                LBRA    str_heap_reset  ; 文字列領域の初期化
