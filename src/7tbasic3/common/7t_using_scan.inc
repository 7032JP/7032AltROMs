; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 書式走査ループ (1 周 = 書式 1 回分)
;   書式制御文字以外はそのまま出力 (参考書籍)。値が残っていれば書式を再利用
;   (動作観察)。'_' は直後 1 文字のリテラル化 (参考書籍)。
;   文字列フィールド: '!'=先頭1文字 / '&空白&'=n+2桁 / '@'=全体 (参考書籍+動作観察)。
;   本片は空きプール。
;==============================================================================
ipu_pass:
        LDA     UF_FLAGS,S
        ANDA    #$FD            ; 当周消費フラグをクリア
        STA     UF_FLAGS,S
        LDD     UF_FBASE,S
        STD     UF_FPTR,S
        LDA     UF_FLEN,S
        STA     UF_FREM,S
ipu_scanlp:
        LDA     UF_FREM,S
        LBEQ    ipu_fmt_end
        LDU     UF_FPTR,S
        LDA     ,U
        CMPA    #'_'
        BEQ     ipu_esc
        CMPA    #'!'
        BEQ     ipu_f_excl
        CMPA    #'&'
        BEQ     ipu_f_amp
        CMPA    #'@'
        BEQ     ipu_f_at
        CMPA    #'#'
        LBEQ    ipu_nscan
        CMPA    #'.'
        LBEQ    ipu_nscan
        CMPA    #'+'
        LBEQ    ipu_nscan
        CMPA    #'*'
        LBEQ    ipu_nscan
        CMPA    #$5C            ; 円記号
        LBEQ    ipu_nscan
ipu_lit:
        JSR     impl_putchar
ipu_adv1:
        LDU     UF_FPTR,S
        LEAU    1,U
        STU     UF_FPTR,S
        DEC     UF_FREM,S
        BRA     ipu_scanlp
ipu_esc:
        ; '_' の次の 1 文字をそのまま出力 (参考書籍)。最終文字なら '_' 自身
        LDA     UF_FREM,S
        CMPA    #1
        BHI     ipe2
        LDA     #'_'
        BRA     ipu_lit
ipe2:
        LDA     1,U
        JSR     impl_putchar
        LDU     UF_FPTR,S
        LEAU    2,U
        STU     UF_FPTR,S
        LDA     UF_FREM,S
        SUBA    #2
        STA     UF_FREM,S
        BRA     ipu_scanlp
ipu_f_excl:
        ; '!' = 先頭 1 文字
        LEAU    1,U
        STU     UF_FPTR,S
        DEC     UF_FREM,S
        LDB     #1
        JMP     ipu_dofield_str
ipu_f_at:
        ; '@' = 文字列全体
        LEAU    1,U
        STU     UF_FPTR,S
        DEC     UF_FREM,S
        CLRB
        JMP     ipu_dofield_str
ipu_f_amp:
        ; '&' + 空白 n 個 + '&' = n+2 桁 (参考書籍)。対が無ければリテラル (動作観察)
        LDB     UF_FREM,S
        DECB
        BEQ     ipu_lit         ; '&' が最終文字 → リテラル (A='&')
        LEAU    1,U
        CLR     UF_TMP,S
ifa_lp:
        LDA     ,U+
        CMPA    #'&'
        BEQ     ifa_match
        CMPA    #' '
        BNE     ifa_lit
        INC     UF_TMP,S
        DECB
        BNE     ifa_lp
ifa_lit:
        LDU     UF_FPTR,S
        LDA     ,U
        BRA     ipu_lit
ifa_match:
        LDB     UF_TMP,S
        ADDB    #2              ; B = フィールド幅 = 書式消費長
        CLRA                    ; D = 幅 (LEAU の負符号事故を避け 16bit で加算)
        LDU     UF_FPTR,S
        LEAU    D,U
        STU     UF_FPTR,S
        LDA     UF_FREM,S
        SUBA    UF_TMP,S
        SUBA    #2
        STA     UF_FREM,S
        JMP     ipu_dofield_str
