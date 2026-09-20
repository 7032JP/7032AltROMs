; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; べき乗の処理と PRINT USING の文末検査を置く。
; pool_pow_body: べき乗の本体 (impl_fp_pow から JMP。復帰は fpow_sgn へ)。
;   FAC1 = |底|, fsc_u = 指数 (単精度), fsc_flag = 奇偶フラグ。
;   指数が 1..255 の整数値なら |底| の反復乗算で求める。EXP(指数*LOG(|底|)) の
;   合成は単精度 24bit の丸めが数回重なるため整数のべきが最終桁で崩れる
;   (5^2 が 25 に満たない等)。反復乗算なら小さい指数で厳密に一致する。
;   それ以外 (非整数・負・256 以上) は合成で求める。
pool_pow_body:
                JSR     fx_st1_fsc_x            ; FAC1 → fsc_x
ppb_have:                               ; 負の底はここへ (fsc_x 退避済)
                LDX     #fsc_u
                JSR     fx_ld1          ; FAC1 = 指数
                JSR     float_to_int_trunc ; D = trunc(指数)
                STD     fsc_t
                TSTA
                BNE     ppb_tor         ; 上位バイト非 0 = 負 or 256 以上
                TSTB
                BEQ     ppb_tor         ; n=0 (前段で処理済だが防御)
                JSR     impl_int_to_fac
                JSR     impl_int_to_float ; FAC1 = float(trunc(指数))
                JSR     fx_ld2_u
                JSR     fcmp_core       ; 一致 (Z) なら整数指数
                BNE     ppb_tor
                LDX     #c_one
                JSR     fx_ld1          ; r = 1.0
ppb_lp:
                JSR     fx_ld2mul_x
                DEC     fsc_t+1
                BNE     ppb_lp
                JMP     fpow_sgn
ppb_tor:
                JSR     fx_ld1_x
                ; 合成経路 (本体は別プール) (ここへ展開)
; ppb_real — べき乗の合成経路 (指数が整数でない / 底が正の実数のとき)。
;   FAC1 = |底| で入り、EXP(指数 * LOG(|底|)) を組んで符号処理へ落とす。
;   (置き場所は容量の都合。参照はラベル経由のみで位置依存はない)
ppb_real:
                JSR     flog_core       ; FAC1 = LOG(|底|) (>0 保証済)
                LDX     #fsc_u
                JSR     fx_ld2mul       ; FAC1 = 指数*LOG(|底|)
                JSR     fexp_core       ; FAC1 = EXP(同上)
                JMP     fpow_sgn

ipu_endchk:
        ; A が文末 (0 / ':' / ELSE) なら Z=1
        TSTA
        BEQ     iec_r
        CMPA    #':'
        BEQ     iec_r
        CMPA    #TOK_ELSE
iec_r:
        RTS
