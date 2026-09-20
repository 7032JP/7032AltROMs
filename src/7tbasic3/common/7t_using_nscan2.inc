; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 数値書式スキャナ (桁 '#'/',' ・小数 '.' ・後置 '+'/'-')
;   ',' は右から 3 桁ごとの区切り指定で桁 1 つ分を確保 (参考書籍+動作観察)。
;   後置 '+' は符号を数の後へ、後置 '-' は負のみ '-' (正は空白) (動作観察)。
;   '^^^^' は指数形式指定 (参考書籍)。判定本体は ins_expchk (別プール)、'^' が
;   4 個そろわなければ指数形式でなく '^' はリテラル扱いのまま (動作観察)。
;   本片は空きプール。
;==============================================================================
ins_digits:
        TSTB
        BEQ    ins_valchk
        LDA     ,U
        CMPA    #'#'
        BEQ     insd_hash
        CMPA    #','
        BNE     ins_dot
        LDA     UF_SPEC,S
        ORA     #8              ; コンマ区切り
        STA     UF_SPEC,S
        BRA     insd_adv
insd_hash:
        LDA     UF_SPEC,S
        ORA     #64             ; 核
        STA     UF_SPEC,S
insd_adv:
        INC     UF_WINT,S
        LEAU    1,U
        DECB
        BRA     ins_digits
ins_dot:
        CMPA    #'.'
        BNE     ins_post
        LDA     UF_SPEC,S
        ORA     #16             ; 小数点
        STA     UF_SPEC,S
        LEAU    1,U
        DECB
insdec_lp:
        TSTB
        BEQ     ins_valchk
        LDA     ,U
        CMPA    #'#'
        BNE     ins_post
        LDA     UF_SPEC,S
        ORA     #64             ; 核 (".##" 形式も有効。動作観察)
        STA     UF_SPEC,S
        INC     UF_DECLEN,S
        LEAU    1,U
        DECB
        BRA     insdec_lp
ins_post:
        ; A = 現在文字, B > 0
        JSR     ins_expchk      ; 指数形式 '^^^^' は後置符号より手前 (別プール)
        CMPA    #'+'
        BNE     insp_m
        LDA     #'+'
        STA     UF_POST,S
        LEAU    1,U
        DECB
        BRA     ins_valchk
insp_m:
        CMPA    #'-'
        BNE     ins_valchk
        LDA     #'-'
        STA     UF_POST,S
        LEAU    1,U
        DECB
ins_valchk:
        LDA     UF_SPEC,S
        BITA    #64
        BEQ     ins_notfield
        STU     UF_FPTR,S
        STB     UF_FREM,S
        JMP     ipu_wchk        ; 桁位置数の上限判定 → ipu_dofield_num (別プール)
ins_notfield:
        ; 核なし → 最初の 1 文字をリテラル扱い
        LDU     UF_FPTR,S
        LDA     ,U
        JMP     ipu_lit
