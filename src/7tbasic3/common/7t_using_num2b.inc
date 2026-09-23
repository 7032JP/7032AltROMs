; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 数値フィールド整形 (3/4: あふれ%・詰め・符号・円)
;   必要幅 > 指定幅は '%' を前置して指定幅を超えて出力 (参考書籍、動作観察)。空きは空白、'**' 指定はアスタリスクで充填 (動作観察)。
;   円記号は符号の後・数の直前 (-円160 の形)。
;   本片は空きプール。続きは ifn_int (4/4 整数部)。
;==============================================================================
ifn_ov:
        CMPB    UF_WINT,U
        BLS     ifn_pad
        LDA     #'%'            ; 桁あふれ
        JSR     impl_putchar
        BRA     ifn_sgn
ifn_pad:
        LDA     UF_WINT,U
        PSHS    B
        SUBA    ,S+             ; A = 詰め幅
        BEQ     ifn_sgn
        TFR     A,B
        LDA     UF_SPEC,U
        BITA    #2              ; '**'
        BNE     ifn_fst
        LDA     #' '
        BRA     ifn_fgo
ifn_fst:
        LDA     #'*'
ifn_fgo:
        JSR     ipu_rep
ifn_sgn:
        LDA     UF_TMP2,U
        BEQ     ifn_yen
        JSR     impl_putchar
ifn_yen:
        LDA     UF_SPEC,U
        BITA    #4
        LBEQ    ifn_int
        LDA     #$5C            ; 円記号は数の直前
        JSR     impl_putchar
        JMP     ifn_int
