; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 反復出力 (共通補助)。A の文字を B 回 (B=0 可)。
;   本片は空きプール。
;==============================================================================
ipu_rep:
        TSTB
        BEQ     ipr_r
        PSHS    A               ; putchar は A を保存しないため毎回再ロード
ipr_l:
        LDA     ,S
        JSR     impl_putchar
        DECB
        BNE     ipr_l
        PULS    A
ipr_r:
        RTS
