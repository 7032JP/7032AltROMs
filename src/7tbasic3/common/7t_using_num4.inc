; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 数値フィールド整形 (4/4b: 小数部・後置符号)
;   小数は declen 桁固定 (不足は '0'。動作観察: "##.##";0.5 → " 0.50")。
;   後置 '+' は '+'/'-'、後置 '-' は負のみ '-' 正は空白 (動作観察)。
;   本片は空きプール。
;==============================================================================
ifn_dot:
        LDA     UF_SPEC,U
        BITA    #16
        BEQ     ifn_post
        JSR     put_dot
        CLR     UF_J,U
ifn_dlp:
        LDB     UF_J,U
        CMPB    UF_DECLEN,U
        BHS     ifn_post
        ADDB    UF_DEXP,U       ; 桁添字 = dexp + i (範囲外は '0')
        BSR     ipu_getd
        JSR     impl_putchar
        INC     UF_J,U
        BRA     ifn_dlp
ifn_post:
        JSR     ifn_pexp        ; 指数形式 '^^^^' の指数部 (非指定時は素通り)
        LDA     UF_POST,U
        BEQ     ifn_ret
        CMPA    #'+'
        BNE     ifn_pm
        LDA     #'+'
        LDB     UF_NEG,U
        BEQ     ifn_pe
        LDA     #'-'
        BRA     ifn_pe
ifn_pm:
        LDA     #' '
        LDB     UF_NEG,U
        BEQ     ifn_pe
        LDA     #'-'
ifn_pe:
        JSR     impl_putchar
ifn_ret:
        RTS
