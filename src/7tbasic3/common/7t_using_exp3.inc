; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 指数部の出力 (小数部の後・後置符号の前)
;   表記文字は値の精度で決まる ('E' / 倍精度は 'D')。指数は符号 + 2 桁固定
;   (動作観察)。
;   指数形式でなければ何もしない。2 桁化は ife_dec2 (別プール)。
;   本片は空きプール。
;==============================================================================
ifn_pexp:
        TST     UF_EXPF,U
        BEQ     ife_x
        LDA     UF_TEXP,U
        JSR     impl_putchar
        LDB     UF_TEXPV,U
        LDA     #'+'
        TSTB
        BPL     ife_s
        NEGB
        LDA     #'-'
ife_s:
        JSR     impl_putchar
        JMP     ife_dec2
ife_x:
        RTS
