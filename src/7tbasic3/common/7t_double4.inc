; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 倍精度浮動小数点 — 表示 (print_double)。本体は 7t_double.inc 参照。
;   FACTYPE=2 の FAC1 を有効 16 桁で出力する:
;     ・|v| を十進スケールで [1D15,1D16) に収め、16 桁整数 N と十進シフト数
;       cnt を得る (先頭桁の十進指数 e10 = cnt+15)。
;     ・e10 ≥ 16 → D 形式 d[.ddd…]D±nn / e10 ≥ 0 → 固定表記 /
;       e10 < 0 は最下位有効桁が小数第 16 位以内 (cnt+末尾ゼロ数 ≥ -16) なら
;       固定表記、それ未満は D 形式 (単精度の第 6 位規則の倍精度の形)。
;     ・末尾ゼロ抑制・先頭ゼロ省略 (.01 等)・符号位置空白は単精度と同じ。
;==============================================================================
print_double:
                PSHS    X,Y,U
                LDA     FAC1            ; 指数 0 = 値零
                BNE     pd_nz
                JSR     put_space               ; 空白 1 個を出す
                JSR     put_zero
                LBRA    pd_end
pd_nz:
                LDA     FAC1+1
                ANDA    #$80
                BEQ     pd_pos
                LDA     #'-'
                BRA     pd_sgo
pd_pos:
                LDA     #' '
pd_sgo:
                JSR     impl_putchar
                ; dw1 = |v| (展開形) (ここへ展開)
; d_unp1: FAC1 (倍精度パック, 非零) → dw1 (符号は正へ)。破壊: A,B
d_unp1:
                CLRA
                LDB     FAC1
                STD     dw1_exp
                CLR     dw1_sgn
                LDA     FAC1+1
                ORA     #$80
                STA     dw1_m
                LDD     FAC1+2
                STD     dw1_m+1
                LDD     FAC1+4
                STD     dw1_m+3
                LDD     FAC1+6
                STD     dw1_m+5
                CLR     dw1_m+7
                CLR     dp_cnt
pd_sd:                                  ; while v ≥ 1D16 : v/=10, cnt++
                LDX     #dc_1e16
                JSR     d_cmpc
                BLO     pd_su
                JSR     d_ld10
                JSR     dfdiv_core
                INC     dp_cnt
                BRA     pd_sd
pd_su:                                  ; while v < 1D15 : v*=10, cnt--
                LDX     #dc_1e15
                JSR     d_cmpc
                BHS     pd_scaled
                JSR     d_ld10
                JSR     dfmul_core
                DEC     dp_cnt
                BRA     pd_su
pd_scaled:
                ; N = round(v): v = m/2^64 × 2^e (e=50..54) → N = m >> (64-e)
                LDX     #dx_m
                LDU     #dw1_m
                JSR     mcopy8
                LDA     #192            ; シフト量 = 64-(e-128) = 192-e (e=178..182)
                SUBA    dw1_exp+1
                STA     d_cnt
pd_shr:
                LDX     #dx_m
                JSR     mshr8           ; C = 落ちたビット
                LDA     #0
                ADCA    #0
                STA     dp_tmp          ; 最後に落ちたビット = 丸めビット
                DEC     d_cnt
                BNE     pd_shr
                LDA     dp_tmp
                BEQ     pd_novf
                JSR     minc8_dx
pd_novf:
                ; 繰上げで N = 1D16 になったら N = 1D15, cnt++
                LDX     #dx_m
                LDU     #dc_i1e16
                JSR     mcmp8
                BNE     pd_dig
                LDX     #dx_m
                LDU     #dc_i1e15
                JSR     mcopy8
                INC     dp_cnt
pd_dig:
                ; 16 桁抽出 → dp_dig[0..15] (MSB 先頭)
                LDB     #16
                LDX     #dp_dig+16
pd_dl:
                PSHS    B,X
                JSR     div64by10       ; A = N%10
                PULS    B,X
                ADDA    #'0'
                STA     ,-X
                DECB
                BNE     pd_dl
                ; 末尾ゼロ数 t (0..15, 先頭桁は非零)
                CLRB
                LDX     #dp_dig+16
pd_tz:
                LDA     ,-X
                CMPA    #'0'
                BNE     pd_tzd
                INCB
                CMPB    #15
                BLO     pd_tz
pd_tzd:
                STB     dp_tmp          ; t
                LDA     dp_cnt
                ADDA    #15             ; A = e10 (符号付き)
                CMPA    #16
                BGE     pd_dfmt         ; e10 ≥ 16 → D 形式
                TSTA
                BMI     pd_small
                ; --- 固定表記 (e10 ≥ 0): 整数部 dp_dig[0..e10] ---
                PSHS    A               ; 0,S = e10
                LDX     #dp_dig
                LDB     ,S
                INCB
pd_fi:
                JSR     put_x_inc
                DECB
                BNE     pd_fi
                LDA     #15
                SUBA    dp_tmp          ; 15 - t = 最終有効桁指標
                SUBA    ,S              ; 小数桁数 = (15-t) - e10
                LEAS    1,S             ; e10 破棄
                TSTA
                BLE    pd_endsp
                TFR     A,B
                JSR     put_dot
pd_ff:
                JSR     put_x_inc
                DECB
                BNE     pd_ff
                BRA    pd_endsp
pd_small:
                ; --- e10 < 0: 最下位有効桁 (cnt+t) が -16 以上なら固定表記 ---
                LDB     dp_cnt
                ADDB    dp_tmp
                CMPB    #-16
                BLT     pd_dfmt
                PSHS    A               ; e10 (負)
                JSR     put_dot
                PULS    B
                NEGB                    ; ゼロ数 = -e10 - 1
                DECB
                BEQ     pd_zd
pd_zl:
                JSR     put_zero
                DECB
                BNE     pd_zl
pd_zd:
                LDA     #16
                SUBA    dp_tmp          ; 桁数 = 16 - t
                TFR     A,B
                LDX     #dp_dig
pd_sl:
                JSR     put_x_inc
                DECB
                BNE     pd_sl
                BRA     pd_endsp
pd_dfmt:
                ; --- D 形式: d[.ddd…]D±nn (A = e10) ---
                PSHS    A
                LDA     dp_dig
                JSR     impl_putchar
                LDA     #15
                SUBA    dp_tmp          ; 小数桁数 = 15 - t
                BEQ     pd_dx
                TFR     A,B
                JSR     put_dot
                LDX     #dp_dig+1
pd_dfl:
                JSR     put_x_inc
                DECB
                BNE     pd_dfl
pd_dx:
                LDA     #'D'
                JSR     impl_putchar
                PULS    A               ; e10
                JSR     pf_put_e2       ; 符号 + 2 桁 (単精度側と共用)
pd_endsp:
                JSR     put_space               ; 空白 1 個を出す
pd_end:
                PULS    X,Y,U,PC
