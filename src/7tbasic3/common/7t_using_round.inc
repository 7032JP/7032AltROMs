; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 四捨五入 (共通補助)
;   入力: U=フレーム, B=残す桁数 keep (符号付き)。UF_DIG/UF_DLEN/UF_DEXP 更新。
;   丸め位置の桁が '5' 以上で繰上げ。全桁繰上げは "1"+ゼロ列で dexp++
;   (動作観察: "#.##";0.005 → "0.01" / "###";2.5 → "  3")。keep<0 は値 0。
;   本片は空きプール。
;==============================================================================
ipu_round:
        TSTB
        BMI     ir_zero
        CMPB    UF_DLEN,U
        BHS     ir_ret
        STB     UF_J,U
        LEAY    UF_DIG,U
        LDA     B,Y
        CMPA    #'5'
        BLO     ir_trunc
ir_clp:
        DECB
        BMI     ir_cout
        LDA     B,Y
        INCA
        STA     B,Y
        CMPA    #'9'
        BLS     ir_trunc
        LDA     #'0'
        STA     B,Y
        BRA     ir_clp
ir_cout:
        ; 全桁繰上げ → "1" + '0' × keep, dexp++
        LDB     UF_J,U
        LDA     #'0'
ir_col:
        TSTB
        BEQ     ir_c1
        STA     B,Y
        DECB
        BRA     ir_col
ir_c1:
        LDA     #'1'
        STA     ,Y
        LDB     UF_J,U
        INCB
        STB     UF_DLEN,U
        INC     UF_DEXP,U
        RTS
ir_trunc:
        LDB     UF_J,U
        STB     UF_DLEN,U
ir_ret:
        RTS
ir_zero:
        CLR     UF_DLEN,U
        CLR     UF_DEXP,U
        RTS
