; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 倍精度浮動小数点 コア
;
;   参考書籍の倍精度実数 (有効 16 桁, D 指数, 接尾辞 #) に整合する演算基盤。
;   FACTYPE=2 のとき FAC1/FAC2 の 8B は倍精度パック形式をそのまま保持する:
;     +0 指数 (bias128, 0=値零) / +1 bit7=符号, bit6-0=仮数上位 7bit (先頭 1 暗黙)
;     +2..+7 仮数下位 48bit                 (仮数は計 56bit)
;   push_fac1/pop_to_fac2 は FAC 8B+型を丸ごと運ぶため式スタックは無変更で対応。
;   演算は展開形 (指数 16bit + 符号 + 仮数 56bit+ガード 8bit = 8B レジスタ) を
;   RAM 作業域 dw1/dw2/dx_m ($01A1-$01BF, 超越関数 fsc_* と時間排他) で行う。
;
;   丸めは既存単精度コアと同じ「ガード最上位ビットによる最近接丸め」。
;   本ファイルの本体は全てラベル参照のみで位置依存が無く、呼出しは
;   7t_float.inc のトランポリン/分岐スタブから行う。
;==============================================================================

;------------------------------------------------------------------------------
; 多倍長ヘルパ群 (X/U = 対象先頭、末尾が LSB)。DECB は C を変えない 6809 の
; 仕様を利用し、多倍長キャリーをループで連鎖する。
;   8B 固定の入口 (madd8 / msub8 / msubr8) と、B でバイト数を指定する入口
;   (maddb / msubb / msubrb) を持つ。後者は単精度の 4B 仮数など 8B 以外の
;   桁数でも同じ 1 本を使うためにある (同じ並びを各所へ展開しない)。
;------------------------------------------------------------------------------
; madd8: [X..X+7] += [U..U+7]。復帰 C=桁上げ。破壊: A,B,X,U
; maddb: 同, バイト数 = B。
madd8:
                LDB     #8
maddb:
                ABX                     ; X = 先頭 + B (LSB の次)
                LEAU    B,U
                ANDCC   #$FE
ma8_l:
                LDA     ,-U
                ADCA    ,-X
                STA     ,X
                DECB
                BNE     ma8_l
                RTS

; msub8: [X..X+7] -= [U..U+7]。復帰 C=借り。破壊: A,B,X,U
; msubb: 同, バイト数 = B。
msub8_dw:                               ; dw1 -= dw2 の入口
                LDX     #dw1_m
                LDU     #dw2_m
msub8:
                LDB     #8
msubb:
                ABX
                LEAU    B,U
                ANDCC   #$FE
ms8_l:
                LDA     ,-X
                SBCA    ,-U
                STA     ,X
                DECB
                BNE     ms8_l
                RTS

; msubr8: [X..X+7] = [U..U+7] - [X..X+7]。破壊: A,B,X,U
; msubrb: 同, バイト数 = B。
msubr8:
                LDB     #8
msubrb:
                ABX
                LEAU    B,U
                ANDCC   #$FE
mr8_l:
                LDA     ,-U
                SBCA    ,-X
                STA     ,X
                DECB
                BNE     mr8_l
                RTS

; mshr8: [X..X+7] >>= 1 (論理)。復帰 C=押し出しビット。破壊: A,B,X
; mror8: 同 (C を bit63 へ入れる回転)。C 連鎖はループで保存される。
mshr8:
                ANDCC   #$FE
mror8:
                LDB     #8
mrr_l:
                ROR     ,X+
                DECB
                BNE     mrr_l
                RTS

; mshl8: [X..X+7] <<= 1。復帰 C=あふれビット。破壊: A,B,X
; mrol8: 同 (C を LSB へ入れる回転)。
mshl8_pd:                               ; pd_mant を 1 桁左へ
                LDX     #pd_mant
                BRA     mshl8
mshl8_dw1:                              ; dw1_m を 1 桁左へ
                LDX     #dw1_m
mshl8:
                ANDCC   #$FE
mrol8:
                LEAX    8,X
                LDB     #8
mrl_l:
                ROL     ,-X
                DECB
                BNE     mrl_l
                RTS

; mcmp8: [X..X+7] と [U..U+7] を MSB から比較。復帰 CC (BHI/BLO/BEQ)。破壊: A,B,X,U
dw1_exp_inc:                            ; dw1 の指数を +1
                LDD     dw1_exp
                ADDD    #1
                STD     dw1_exp
                RTS
ld_dw1_dw2:                             ; X=dw1_m / U=dw2_m
                LDX     #dw1_m
                LDU     #dw2_m
                RTS
mcmp8_dw:                               ; dw1/dw2 を比べる入口
                LDX     #dw1_m
                LDU     #dw2_m
mcmp8:
                LDB     #8
mc8_l:
                LDA     ,X+
                CMPA    ,U+
                BNE     mc8_d
                DECB
                BNE     mc8_l
mc8_d:
                RTS

; mcopy8: [X..X+7] = [U..U+7]。破壊: A,B,X,U
mcopy8:
                LDB     #8
mcp_l:
                LDA     ,U+
                STA     ,X+
                DECB
                BNE     mcp_l
                RTS

; mclr8: [X..X+7] = 0。破壊: B,X
mclr8:
                LDB     #8
mz_l:
                CLR     ,X+
                DECB
                BNE     mz_l
                RTS

; mtst8: [X..X+7] 全零判定 (Z=1 で零)。破壊: A,B,X
mtst8:
                LDB     #8
                CLRA
mt_l:
                ORA     ,X+
                DECB
                BNE     mt_l
                TSTA
                RTS

; minc8: [X..X+7] += 1。復帰 C=1 なら全桁あふれ (結果全零)。破壊: A,B,X
; mincb: 同, バイト数 = B (X = 先頭、X+B-1 が LSB)。多倍長の +1 と桁上げ伝播を
;        必要とする箇所は全てここを呼ぶ (同じ INC/BNE の並びを各所へ展開しない)。
minc8_dx:                               ; dx_m を +1
                LDX     #dx_m
minc8:
                LDB     #8
mincb:
                ABX                     ; X = 先頭 + B (LSB の次)
mi_l:
                INC     ,-X
                BNE     mi_ok
                DECB
                BNE     mi_l
                ORCC    #$01
                RTS
mi_ok:
                ANDCC   #$FE
                RTS

;------------------------------------------------------------------------------
; dfadd_core: dw1 = dw1 + dw2。指数合わせ→仮数加減→正規化。破壊: A,B,X,U
;------------------------------------------------------------------------------
dfadd_core:
                LDD     dw2_exp
                BEQ     dfa_done        ; dw2=0 → 結果 dw1
                LDD     dw1_exp
                BNE     dfa_both
                ; dw1=0 → 結果 = dw2
                LDD     dw2_exp
                STD     dw1_exp
                LDA     dw2_sgn
                STA     dw1_sgn
                BSR     ld_dw1_dw2
                BRA     mcopy8
dfa_both:
                LDD     dw1_exp
                SUBD    dw2_exp
                BGE     dfa_algn
                ; dw1 の指数が小 → dw1 ↔ dw2 全交換
                LDD     dw1_exp
                LDX     dw2_exp
                STD     dw2_exp
                STX     dw1_exp
                LDA     dw1_sgn
                LDB     dw2_sgn
                STA     dw2_sgn
                STB     dw1_sgn
                BSR     ld_dw1_dw2
                LDB     #8
dfa_swl:
                LDA     ,X
                PSHS    A
                LDA     ,U
                STA     ,X+
                PULS    A
                STA     ,U+
                DECB
                BNE     dfa_swl
                LDD     dw1_exp
                SUBD    dw2_exp
dfa_algn:
                ; D = 指数差 (0..)
                CMPD    #64
                BLT     dfa_sh
                RTS                     ; 小さい方は影響なし → dw1 が結果
dfa_sh:
                TSTB
                BEQ     dfa_add0
dfa_shl:
                PSHS    B
                LDX     #dw2_m
                JSR     mshr8
                PULS    B
                DECB
                BNE     dfa_shl
dfa_add0:
                LDA     dw1_sgn
                EORA    dw2_sgn
                BMI     dfa_sub
                ; 同符号: 加算
                JSR     ld_dw1_dw2
                JSR     madd8
                BCC     dfa_done
                LDX     #dw1_m          ; あふれ: C を bit63 へ入れ右 1 シフト
                JSR     mror8
                JSR     dw1_exp_inc
dfa_done:
                RTS
dfa_sub:
                ; 異符号: 大きさ比較して大-小
                JSR     mcmp8_dw
                LBEQ    dbl_zero
                BHI     dfa_ge
                LDA     dw2_sgn         ; |dw2| が大 → 符号は dw2 側
                STA     dw1_sgn
                JSR     ld_dw1_dw2
                JSR     msubr8
                BRA     dfa_norm
dfa_ge:
                JSR     msub8_dw
dfa_norm:
                LDA     dw1_m
                BMI     dfa_done
                JSR     dbl_shl_dec     ; dw1 を 1 桁左送りし指数を 1 減らす (共通化)
                BLE     dbl_zero        ; 指数 ≤0 → アンダーフロー零 (共通の零生成へ)
                BRA     dfa_norm

;------------------------------------------------------------------------------
; dfmul_core: dw1 = dw1 × dw2。64×64→128bit shift-add の上位 64bit + 最近接丸め。
;   [dx_m|dw1_m] の 16B 連続レジスタで積を右送りし、乗数 (dw1_m) の空きに積下位が
;   入る。破壊: A,B,X,U
;------------------------------------------------------------------------------
dfmul_core:
                LDD     dw1_exp
                BEQ     dfm_zero
                LDD     dw2_exp
                BEQ     dfm_zero
                LDD     dw1_exp
                ADDD    dw2_exp
                SUBD    #128
                STD     dw1_exp
                LDA     dw1_sgn
                EORA    dw2_sgn
                STA     dw1_sgn
                LDX     #dx_m
                JSR     mclr8
                LDA     #64
                STA     d_cnt
dfm_l:
                LDA     dw1_m+7
                ANDA    #1
                BEQ     dfm_na
                LDX     #dx_m
                LDU     #dw2_m
                JSR     madd8           ; C = bit64 桁上げ
                BRA     dfm_sh
dfm_na:
                ANDCC   #$FE
dfm_sh:
                LDX     #dx_m           ; [dx_m|dw1_m] 16B を C から右回転
                JSR     mror8
                LDX     #dw1_m
                JSR     mror8
                DEC     d_cnt
                BNE     dfm_l
                ; 正規化 (積は [0.25,1) → 高々 1 シフト)
                LDA     dx_m
                BMI     dfm_nok
                JSR     mshl8_dw1
                LDX     #dx_m
                JSR     mrol8
                LDD     dw1_exp
                SUBD    #1
                STD     dw1_exp
dfm_nok:
                ; 丸め: 積下位 (dw1_m) の bit63
                LDA     dw1_m
                BPL     dfm_nr
                JSR     minc8_dx
                BCC     dfm_nr
; >>FOLD dbl_ovf1
;------------------------------------------------------------------------------
; dbl_ovf1 / dbl_asm / dbl_zero — 倍精度の乗除算に共通する後尾。
;   dbl_ovf1 : 丸めで全桁あふれ → 仮数 1.0 / 指数++ してから dbl_asm へ落ちる。
;   dbl_asm  : 作業仮数 dx_m を dw1_m へ写して復帰 (指数域検査は詰め直し側が担う)。
;   dbl_zero : 零を作って復帰。
;   乗算と除算が同じ 12 命令を並べ、加減算も零生成だけ別に持っていたので
;   1 本にまとめた (容量捻出)。命令の並びは乗算側のものをそのまま
;   残してあり、落ち込み経路も元のままである。
;------------------------------------------------------------------------------
dbl_ovf1:
                LDA     #$80            ; 全桁あふれ → 1.0, 指数++
                STA     dx_m
                JSR     dw1_exp_inc
dbl_asm:
dfm_nr:
                LDX     #dw1_m
                LDU     #dx_m
                JMP     mcopy8          ; 指数域チェックは d_pack が担う
dbl_zero:
dfm_zero:
                CLRA
                CLRB
                STD     dw1_exp
                CLR     dw1_sgn
                RTS
; <<FOLD dbl_ovf1

;------------------------------------------------------------------------------
; dfdiv_core: dw1 = dw1 / dw2。64bit 復元法 + 最近接丸め。破壊: A,B,X,U
;   除数零 → Division By Zero (ERR 11)。
;------------------------------------------------------------------------------
dfdiv_core:
                LDD     dw2_exp
                BNE     dfd_dok
                JMP     errc_11
dfd_dok:
                LDD     dw1_exp
                BEQ    dbl_zero
                SUBD    dw2_exp             ; (D はすぐ上で読んだ dw1_exp のまま)
                ADDD    #128
                STD     dw1_exp
                LDA     dw1_sgn
                EORA    dw2_sgn
                STA     dw1_sgn
                LDX     #dx_m           ; 商 = 0
                JSR     mclr8
                ; 事前減算: rem(dw1_m) ≥ 除数 → rem-=除数, 整数ビット=1, 指数++
                JSR     mcmp8_dw
                BLO     dfd_nopre
                JSR     msub8_dw
                JSR     dw1_exp_inc
                LDA     #1
                BRA     dfd_pre
dfd_nopre:
                CLRA
dfd_pre:
                PSHS    A               ; 0,S = 整数ビット
                LDA     #64
                STA     d_cnt
dfd_l:
                LDX     #dx_m
                JSR     mshl8
                JSR     mshl8_dw1
                BCS     dfd_sub
                JSR     mcmp8_dw
                BLO     dfd_ns
dfd_sub:
                JSR     msub8_dw
                INC     dx_m+7          ; 商ビット = 1
dfd_ns:
                DEC     d_cnt
                BNE     dfd_l
                ; 丸めビット決定
                PULS    A
                TSTA
                BEQ     dfd_fr
                ; 整数ビット=1: 商 >>1 (押し出しが丸め), 先頭 1 を差し込む
                LDX     #dx_m
                JSR     mshr8
                LDA     dx_m
                ORA     #$80
                STA     dx_m
                BCS     dfd_r1
                BRA     dbl_asm
dfd_fr:
                ; 丸め = (2*rem ≥ 除数)
                JSR     mshl8_dw1
                BCS     dfd_r1
                JSR     mcmp8_dw
                LBLO    dbl_asm
dfd_r1:
                JSR     minc8_dx
                LBCC    dbl_asm
                JMP     dbl_ovf1        ; 全桁あふれ → 共通後尾 (共通化)

;------------------------------------------------------------------------------
; dcmp_core: dw1 (左) と dw2 (右) を比較。A = +1(左>右) / -1(左<右) / 0(=)、
;   TSTA で N/Z 確定 (fcmp_core と同契約)。破壊: A,B,X,U
;------------------------------------------------------------------------------
dcmp_core:
                LDD     dw1_exp
                BNE     dcm_1nz
                CLR     dw1_sgn         ; 値零の符号は正に正規化
dcm_1nz:
                LDD     dw2_exp
                BNE     dcm_2nz
                CLR     dw2_sgn
dcm_2nz:
                LDA     dw1_sgn
                CMPA    dw2_sgn
                BEQ     dcm_same
                TSTA
                BMI     dcm_lt
dcm_gt:
                LDA     #1              ; LDA が N/Z/V を確定
                RTS
dcm_lt:
                LDA     #$FF
                RTS
dcm_same:
                LDD     dw1_exp
                CMPD    dw2_exp
                BHI     dcm_mgt
                BLO     dcm_mlt
                LDD     dw1_exp
                BEQ     dcm_eq          ; 零同士
                JSR     mcmp8_dw
                BHI     dcm_mgt
                BLO     dcm_mlt
dcm_eq:
                CLRA
                RTS
dcm_mgt:
                LDA     dw1_sgn
                BMI     dcm_lt          ; 共に負なら大きさ大 = 値小
                BRA     dcm_gt
dcm_mlt:
                LDA     dw1_sgn
                BMI     dcm_gt
                BRA     dcm_lt

;------------------------------------------------------------------------------
; 定数ロード/比較 (表示・十進スケール用)
;------------------------------------------------------------------------------
; d_ld10: dw2 = 10.0。破壊: A,B,X
d_ld10:
                LDD     #132            ; 10 = 0.625 × 2^4
                STD     dw2_exp
                CLR     dw2_sgn
                LDA     #$A0
                STA     dw2_m
                LDX     #dw2_m+1
                LDB     #7
dl10_l:
                CLR     ,X+
                DECB
                BNE     dl10_l
                RTS

; d_cmpc: dw1 (正) と X 指す定数 (指数 2B + 仮数 8B) を比較。復帰 CC
;   (BLO = dw1 < 定数)。破壊: A,B,X,U
d_cmpc:
                LDD     dw1_exp
                CMPD    ,X
                BNE     dcc_r
                TFR     X,U
                LEAU    2,U
                LDX     #dw1_m
                JMP     mcmp8
dcc_r:
                RTS

; div64by10: dx_m (8B) /= 10, 余り (0-9) → A。破壊: A,B
div64by10:
                CLRA
                LDB     #64
d64_l:
                ASL     dx_m+7
                ROL     dx_m+6
                ROL     dx_m+5
                ROL     dx_m+4
                ROL     dx_m+3
                ROL     dx_m+2
                ROL     dx_m+1
                ROL     dx_m
                ROLA
                CMPA    #10
                BLO     d64_s
                SUBA    #10
                INC     dx_m+7
d64_s:
                DECB
                BNE     d64_l
                RTS

; 十進スケール定数 (指数 16bit + 仮数 8B, 正規化済み)
dc_1e15:        FDB     178                             ; 1E15 = f×2^50
                FCB     $E3,$5F,$A9,$31,$A0,$00,$00,$00
dc_1e16:        FDB     182                             ; 1E16 = f×2^54
                FCB     $8E,$1B,$C9,$BF,$04,$00,$00,$00
; 整数 10^16 / 10^15 (表示の繰上げ検査用, 8B 整数)
dc_i1e16:       FCB     $00,$23,$86,$F2,$6F,$C1,$00,$00
dc_i1e15:       FCB     $00,$03,$8D,$7E,$A4,$C6,$80,$00
; 十進仮数飽和上限: (2^64-10)/10 = $1999999999999998 (これ以下なら *10+9 が収まる)
dc_max10:       FCB     $19,$99,$99,$99,$99,$99,$99,$98


;------------------------------------------------------------------------------
; 既存経路の入口スタブ (呼出元は 3B の JSR/分岐差替えのみでアドレス不変)
;------------------------------------------------------------------------------
; pv_flt: PRINT 数値出力の実数分岐 (A=FACTYPE, 1 or 2)。
pv_flt:
                CMPA    #2
                BNE     pvf_s
                JMP     print_double
pvf_s:
                JMP     print_float

;------------------------------------------------------------------------------
; dbl_demote: FACTYPE=2 のときだけ FAC1 を展開形単精度 (FACTYPE=1) へ落とす。
;   仮数は上位 32bit へ最近接丸め。他の型は無変化。破壊: A,B。X/Y/U 温存。
;   単精度変数への代入・整数変換・超越関数引数など、既存単精度経路の入口で呼ぶ。
;------------------------------------------------------------------------------
dbl_demote:
                LDA     FACTYPE
                CMPA    #2
                BEQ     dd_go
                RTS
dd_go:
                LDA     FAC1
                BNE     dd_nz
dd_uf:
                ; 値零 → 展開形の零
                CLRA
                CLRB
                STD     FAC1
                STD     FAC1+2
                STD     FAC1+4
                JMP     fac_type_sng    ; 型=単精度にして復帰 (共通化)
dd_nz:
                DEC     FAC1            ; 指数 -1: 倍精度 f∈[0.5,1) → 単精度 f∈[1,2) 規約
                BEQ     dd_uf           ; 指数 1 → 0 はアンダーフロー零
                LDB     FAC1+4          ; 丸めビット源 (単精度の有効桁 24bit の直下)
                PSHS    B
                LDA     FAC1+1
                ANDA    #$80
                PSHS    A               ; 符号
                LDA     FAC1+1
                ORA     #$80            ; 先頭 1 復元 → 仮数 b31..b24
                STA     FAC1+1
                PULS    A
                STA     FAC1+5
                CLR     FAC1+4          ; 24bit 未満の桁は残さない
                CLR     FAC1+6
                CLR     FAC1+7
                PULS    B
                TSTB
                BPL     dd_done
                ; 最近接丸め: 24bit 仮数 +1
                INC     FAC1+3          ; 桁上がりだけ上位へ伝播
                BNE     dd_done
                INC     FAC1+2
                BNE     dd_done
                INC     FAC1+1
                BNE     dd_done
                LDA     #$80            ; 仮数あふれ → 1.0, 指数++
                STA     FAC1+1
                INC     FAC1
dd_done:
                JMP     fac_type_sng    ; 型=単精度にして復帰 (共通化)
