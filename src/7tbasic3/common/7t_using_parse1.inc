; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 捕捉文字列の解析 (本体)
;   捕捉した 10 進表記 ([空白][-]digits[.digits][E/D±nn]) を
;   値 = 0.UF_DIG × 10^UF_DEXP (先頭ゼロ正規化) へ分解する。
;   本片は空きプール。指数部と正規化は ipu_parse2 (igp_exp/igp_strip)。
;==============================================================================
ipu_parse:
        CLR     UF_NEG,U
        CLR     UF_DLEN,U
        CLR     UF_DEXP,U
        CLR     UF_TMP,U        ; 小数点通過フラグ
        LDX     #strf_dbuf
igp_sp:
        LDA     ,X+
        CMPA    #' '
        BEQ     igp_sp
        CMPA    #'-'
        BNE     igp_lp
        LDB     #1
        STB     UF_NEG,U
        LDA     ,X+
igp_lp:
        CMPA    #'.'
        BNE     igp_d
        LDB     #1
        STB     UF_TMP,U
        BRA     igp_nx
igp_d:
        CMPA    #'0'
        BLO     igp_echk
        CMPA    #'9'
        BHI     igp_echk
        LDB     UF_DLEN,U
        CMPB    #17
        BHS     igp_skp         ; バッファ防護 (通常は来ない)
        LEAY    UF_DIG,U
        STA     B,Y
        INCB
        STB     UF_DLEN,U
igp_skp:
        LDB     UF_TMP,U
        BNE     igp_nx
        INC     UF_DEXP,U       ; 小数点前の桁数
igp_nx:
        LDA     ,X+
        BRA     igp_lp
igp_echk:
        CMPA    #'E'
        LBEQ    igp_exp
        CMPA    #'D'
        LBEQ    igp_exp
        JMP     igp_strip
