; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 数値フィールド整形 (1/4: 丸め・整数桁数・前置符号)
;   前置符号: 先頭 '+' 指定は常に符号を数の直前へ ('+'/'-')。指定なしの負値は
;   '-' (後置符号があるときは前置しない。動作観察: "###+";-12 → " 12-")。
;   本片は空きプール。続きは ifn_need (2/4)。
;==============================================================================
ipu_fmtnum:
        JSR     ipu_getdigits
        JSR     ifn_expprep     ; 指数形式 '^^^^' の桁合わせ (非指定時は素通り)
        ; --- 小数 declen 桁で丸め ---
        LDB     UF_DEXP,U
        ADDB    UF_DECLEN,U
        JSR     ipu_round
        ; --- 整数部桁数 ---
        LDB     UF_DEXP,U
        BGT     ifn_il
        CLRB
ifn_il:
        STB     UF_TMP,U        ; 整数部桁数
        ; --- 前置符号文字 ---
        CLR     UF_TMP2,U
        LDA     UF_SPEC,U
        BITA    #1              ; 先頭 '+'
        BEQ     ifn_s1
        LDA     #'+'
        LDB     UF_NEG,U
        BEQ     ifn_ss
        LDA     #'-'
ifn_ss:
        STA     UF_TMP2,U
        JMP     ifn_need
ifn_s1:
        LDA     UF_NEG,U
        LBEQ    ifn_need
        LDA     UF_POST,U
        LBNE    ifn_need        ; 後置符号あり → 前置しない (動作観察)
        LDA     #'-'
        STA     UF_TMP2,U
        JMP     ifn_need
