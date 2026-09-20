; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 捕捉文字列の解析 (指数部・正規化)
;   指数部は自前の整形出力 (符号 + 2 桁固定) だけが入力なので固定長で読む。
;   先頭 '0' を剥いで 0.dig × 10^dexp へ正規化する (dlen=0 は値 0)。
;   本片は空きプール。
;==============================================================================
igp_exp:
        CLR     UF_TMP,U        ; 指数符号 (0=+)
        LDA     ,X+
        CMPA    #'-'
        BNE     igp_e2
        INC     UF_TMP,U
igp_e2:
        LDA     ,X+             ; 十の位
        SUBA    #'0'
        LDB     #10
        MUL                     ; D = 十の位 × 10 (下位 = B)
        LDA     ,X+             ; 一の位
        SUBA    #'0'
        PSHS    A
        ADDB    ,S+             ; B = |指数|
        LDA     UF_TMP,U
        BEQ     igp_e3
        NEGB
igp_e3:
        ADDB    UF_DEXP,U
        STB     UF_DEXP,U
igp_strip:
        ; 先頭 '0' を剥いで正規化
igs_lp:
        LDA     UF_DLEN,U
        BEQ     igp_zero
        LDA     UF_DIG,U
        CMPA    #'0'
        BNE     igp_done
        LEAY    UF_DIG,U
        LDB     UF_DLEN,U
        DECB
        STB     UF_DLEN,U
        DEC     UF_DEXP,U
        TSTB
        BEQ     igs_lp
igs_cp:
        LDA     1,Y
        STA     ,Y+
        DECB
        BNE     igs_cp
        BRA     igs_lp
igp_zero:
        CLR     UF_DEXP,U
igp_done:
        RTS
