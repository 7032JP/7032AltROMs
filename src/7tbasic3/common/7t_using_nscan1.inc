; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 数値書式スキャナ (前置部: 先頭 '+' / '**' / 円 2 個 / '**円')
;   参考書籍: '**' は上位空きをアスタリスク充填 (2 桁分)。円記号 2 個は数の直前に
;   円記号 (2 桁分のうち 1 個を円が使用)。'**円' は両機能 (3 桁分)。
;   核 (数字位置) が無ければ最初の 1 文字をリテラル扱いに戻す。
;   本片は空きプール。続き (桁・小数・後置) は ins_digits。
;==============================================================================
ipu_nscan:
        CLR     UF_SPEC,S
        CLR     UF_WINT,S
        CLR     UF_DECLEN,S
        CLR     UF_POST,S
        CLR     UF_EXPF,S
        LDU     UF_FPTR,S
        LDB     UF_FREM,S
        ; --- 先頭 '+' (符号を数の直前に浮動させる。動作観察: "+###";12 → " +12") ---
        LDA     ,U
        CMPA    #'+'
        BNE     ins_pfx
        LDA     #1              ; 先頭+
        STA     UF_SPEC,S
        INC     UF_WINT,S
        LEAU    1,U
        DECB
        LBEQ    ins_valchk
ins_pfx:
        LDA     ,U
        CMPA    #'*'
        BNE     ins_yen0
        CMPB    #2
        LBLO    ins_digits
        LDA     1,U
        CMPA    #'*'
        LBNE    ins_digits
        LDA     UF_SPEC,S
        ORA     #2+64           ; ** + 核
        STA     UF_SPEC,S
        LDA     UF_WINT,S
        ADDA    #2
        STA     UF_WINT,S
        LEAU    2,U
        SUBB    #2
        LBEQ    ins_valchk
        LDA     ,U              ; '**円' ?
        CMPA    #$5C
        LBNE    ins_digits
        LDA     UF_SPEC,S
        ORA     #4              ; 円
        STA     UF_SPEC,S
        INC     UF_WINT,S
        LEAU    1,U
        DECB
        LBEQ    ins_valchk
        JMP     ins_digits
ins_yen0:
        CMPA    #$5C
        LBNE    ins_digits
        CMPB    #2
        LBLO    ins_digits
        LDA     1,U
        CMPA    #$5C
        LBNE    ins_digits
        LDA     UF_SPEC,S
        ORA     #4+64           ; 円 + 核
        STA     UF_SPEC,S
        LDA     UF_WINT,S
        ADDA    #2
        STA     UF_WINT,S
        LEAU    2,U
        SUBB    #2
        LBEQ    ins_valchk
        JMP     ins_digits
