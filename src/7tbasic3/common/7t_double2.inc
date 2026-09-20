; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 倍精度浮動小数点 — オペランド展開/パック (本体は 7t_double.inc)
;   空きプールへ配置 (ラベル参照のみ, 位置依存なし)。
;==============================================================================
;------------------------------------------------------------------------------
; オペランド展開 (FAC → dw)。型 0/1/2 を吸収して統一の展開形にする。
;------------------------------------------------------------------------------
; dget1: FAC1 (FACTYPE) → dw2 (右オペランド)。破壊: A,B,X,U
dget1:
                LDA     FACTYPE
                CMPA    #2
                BEQ     dg1_dbl
                CMPA    #1
                BEQ     dg1_sng
                ; 整数 16bit
                LDD     FAC1+2
                BNE     dg1_int
dg1_zero:
                CLRA
                CLRB
                STD     dw2_exp
                CLR     dw2_sgn
                LDX     #dw2_m
                JMP     mclr8
dg1_int:
                CLR     dw2_sgn
                TSTA
                BPL     dg1_ipos
                LDA     #$80
                STA     dw2_sgn
                LDD     FAC1+2
                JSR     negd
dg1_ipos:
                STD     dw2_m
                LDX     #dw2_m+2
                LDB     #6
dg1_cl:
                CLR     ,X+
                DECB
                BNE     dg1_cl
                LDD     #144            ; 指数 = 128+16 (仮数を bit63..48 に置いた前提)
                STD     dw2_exp
dg1_nrm:
                LDA     dw2_m
                BMI     dg1_done
                LDX     #dw2_m
                JSR     mshl8
                LDD     dw2_exp
                SUBD    #1
                STD     dw2_exp
                BRA     dg1_nrm
dg1_done:
                RTS
dg1_sng:
                ; 展開形単精度 (指数/仮数32bit/符号) → dw2
                ;   単精度は v=(M/2^31)*2^(exp-128) (f∈[1,2)) のため指数を +1 して
                ;   f∈[0.5,1) 規約へ合わせる
                LDB     FAC1
                BEQ     dg1_zero
                CLRA
                ADDD    #1
                STD     dw2_exp
                LDA     FAC1+5
                STA     dw2_sgn
                LDD     FAC1+1
                STD     dw2_m
                LDD     FAC1+3
                STD     dw2_m+2
                CLRA
                CLRB
                STD     dw2_m+4
                STD     dw2_m+6
                RTS
dg1_dbl:
                LDB     FAC1
                BEQ     dg1_zero
                CLRA
                STD     dw2_exp
                LDA     FAC1+1
                ANDA    #$80
                STA     dw2_sgn
                LDA     FAC1+1
                ORA     #$80            ; 暗黙の先頭 1 を復元
                STA     dw2_m
                LDD     FAC1+2
                STD     dw2_m+1
                LDD     FAC1+4
                STD     dw2_m+3
                LDD     FAC1+6
                STD     dw2_m+5
                CLR     dw2_m+7
                RTS

; dget2: FAC2 (FAC2TYPE) → dw1 (左/結果オペランド)。破壊: A,B,X,U
dget2:
                LDA     FAC2TYPE
                CMPA    #2
                BEQ     dg2_dbl
                CMPA    #1
                BEQ     dg2_sng
                LDD     FAC2+2
                BNE     dg2_int
dg2_zero:
                CLRA
                CLRB
                STD     dw1_exp
                CLR     dw1_sgn
                LDX     #dw1_m
                JMP     mclr8
dg2_int:
                CLR     dw1_sgn
                TSTA
                BPL     dg2_ipos
                LDA     #$80
                STA     dw1_sgn
                LDD     FAC2+2
                JSR     negd
dg2_ipos:
                STD     dw1_m
                LDX     #dw1_m+2
                LDB     #6
dg2_cl:
                CLR     ,X+
                DECB
                BNE     dg2_cl
                LDD     #144
                STD     dw1_exp
dg2_nrm:
                LDA     dw1_m
                BMI     dg2_done
                JSR     dbl_shl_dec     ; dw1 を 1 桁左送りし指数を 1 減らす (共通化)
                BRA     dg2_nrm
dg2_done:
                RTS
dg2_sng:
                LDB     FAC2            ; 指数 +1 (dg1_sng と同じ規約合わせ)
                BEQ     dg2_zero
                CLRA
                ADDD    #1
                STD     dw1_exp
                LDA     FAC2+5
                STA     dw1_sgn
                LDD     FAC2+1
                STD     dw1_m
                LDD     FAC2+3
                STD     dw1_m+2
                CLRA
                CLRB
                STD     dw1_m+4
                STD     dw1_m+6
                RTS
dg2_dbl:
                LDB     FAC2
                BEQ     dg2_zero
                CLRA
                STD     dw1_exp
                LDA     FAC2+1
                ANDA    #$80
                STA     dw1_sgn
                LDA     FAC2+1
                ORA     #$80
                STA     dw1_m
                LDD     FAC2+2
                STD     dw1_m+1
                LDD     FAC2+4
                STD     dw1_m+3
                LDD     FAC2+6
                STD     dw1_m+5
                CLR     dw1_m+7
                RTS

; d_prep: 二項演算の両オペランドを展開 (dw1←FAC2, dw2←FAC1)。破壊: A,B,X,U
d_prep:
                JSR     dget2
                JMP     dget1
