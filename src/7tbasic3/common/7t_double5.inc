; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 倍精度浮動小数点 — 数値リテラル分類/倍精度構築。本体は 7t_double.inc 参照。
;   参考書籍の倍精度定数仕様 (動作観察確認):
;     ・有効桁 (先行ゼロを除く) 8 桁以上 → 倍精度 / 7 桁以下 → 単精度
;     ・接尾辞 # と D 指数 → 倍精度を強制 / 接尾辞 ! と E 指数 → 単精度を強制
;   単精度・整数の既存経路 (pn_decimal) には一切手を入れず、分類で倍精度と
;   判定した場合のみ先頭からワイド仮数 (64bit) で再解析する。
;==============================================================================

; pd_add10: pd_mant (8B) = pd_mant*10 + A (A=桁 0-9)。飽和/あふれ時は取り込まず
;   C=1。受理時 C=0。破壊: CC のみ (A/B/X/U 温存)。
pd_add10:
                TST     pd_sat
                BNE     pda_rej
                PSHS    A,X,U
                LDX     #pd_mant
                LDU     #dc_max10
                JSR     mcmp8
                BHI     pda_sat
                LDX     #dx_m           ; dx_m = M (複製)
                LDU     #pd_mant
                JSR     mcopy8
                JSR     mshl8_pd
                JSR     mshl8_pd
                LDX     #pd_mant
                LDU     #dx_m
                JSR     madd8           ; M*5
                JSR     mshl8_pd
                LDA     ,S              ; 桁
                ADDA    pd_mant+7
                STA     pd_mant+7
                BCC     pda_ok
                INC     pd_mant+6
                BNE     pda_ok
                INC     pd_mant+5
                BNE     pda_ok
                INC     pd_mant+4
                BNE     pda_ok
                INC     pd_mant+3
                BNE     pda_ok
                INC     pd_mant+2
                BNE     pda_ok
                INC     pd_mant+1
                BNE     pda_ok
                INC     pd_mant
pda_ok:
                PULS    A,X,U
                ANDCC   #$FE
                RTS
pda_sat:
                INC     pd_sat
                PULS    A,X,U
pda_rej:
                ORCC    #$01
                RTS

; pn_double: 倍精度リテラルを X から解析し FAC1 (パック形, FACTYPE=2) へ。
;   pn_decimal と同じ構文 (整数部 [.小数部] [D/E[±]nn] [接尾辞 %!#])。
;   入力: X = 先頭。出力: FAC1 = 値, X = 直後。破壊: A,B,CC。Y/U 温存。
pn_double:
                PSHS    Y,U
                CLR     pn_dexp
                CLR     pd_sat
                PSHS    X
                LDX     #pd_mant
                JSR     mclr8
                PULS    X
pdb_il:                                 ; --- 整数部 ---
                JSR     nxt_digit       ; A=0-9 / C=1 で数字でない (A=その文字)
                BCS     pdb_idone
                BSR     pd_add10
                BCC     pdb_il
                INC     pn_dexp         ; 仮数あふれ: 桁を落とし十進指数で補償
                BRA     pdb_il
pdb_idone:
                CMPA    #'.'
                BNE     pdb_exp
                LEAX    1,X
pdb_fl:                                 ; --- 小数部 ---
                JSR     nxt_digit       ; A=0-9 / C=1 で数字でない (A=その文字)
                BCS     pdb_exp
                JSR     pd_add10
                BCS     pdb_fl          ; あふれ桁 (有効桁外) は切り捨て
                DEC     pn_dexp
                BRA     pdb_fl
pdb_exp:                                ; --- 指数部 D/E[±]nn ---
                LDA     ,X
                CMPA    #'D'
                BEQ     pdb_e
                CMPA    #'E'
                BNE     pdb_sfx
pdb_e:
                JSR     pn_exp_tail     ; 指数部 + 型接尾辞 (単精度側と共用)
                BRA     pdb_build
pdb_sfx:
                JSR     pn_sfx_skip     ; 型接尾辞のみ
pdb_build:                              ; --- 値の構築 ---
                PSHS    X               ; 本文ポインタ退避 (以降ヘルパが X を使う)
                LDX     #pd_mant
                JSR     mtst8
                BEQ     pdb_zero
                LDX     #dw1_m
                LDU     #pd_mant
                JSR     mcopy8
                LDD     #192            ; 指数 = 128+64 (仮数 64bit 整数)
                STD     dw1_exp
                CLR     dw1_sgn
pdb_nrm:
                LDA     dw1_m
                BMI     pdb_sc
                JSR     dbl_shl_dec     ; dw1 を 1 桁左送りし指数を 1 減らす (共通化)
                BRA     pdb_nrm
pdb_sc:                                 ; --- 十進スケール pn_dexp を適用 ---
                LDA     pn_dexp
                BEQ     pdb_pack
                BMI     pdb_sneg
                JSR     d_ld10
                JSR     dfmul_core
                DEC     pn_dexp
                BRA     pdb_sc
pdb_sneg:
                JSR     d_ld10
                JSR     dfdiv_core
                INC     pn_dexp
                BRA     pdb_sc
pdb_pack:
                JSR     d_pack
                PULS    X,Y,U,PC
pdb_zero:
                LDX     #FAC1           ; 値零の倍精度 (パック全零)
                JSR     mclr8
                LDA     #2
                STA     FACTYPE
                PULS    X,Y,U,PC
