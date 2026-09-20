; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 倍精度浮動小数点 — 型降格/ディスパッチ (本体は 7t_double.inc)
;   空きプールへ配置 (ラベル参照のみ, 位置依存なし)。
;==============================================================================
;------------------------------------------------------------------------------
; d_pack: dw1 → FAC1 (倍精度パック, FACTYPE=2)。ガードバイト bit7 で最近接丸め。
;   指数 ≤0 → 値零 / >255 → Overflow (ERR 6)。破壊: A,B
;------------------------------------------------------------------------------
d_pack:
                LDA     dw1_m+7         ; ガードバイト
                BPL     dp_rok
                ; 56bit 仮数 +1 (bytes 0..6。桁上がりだけ上位へ伝播)
                INC     dw1_m+6
                BNE     dp_rok
                INC     dw1_m+5
                BNE     dp_rok
                INC     dw1_m+4
                BNE     dp_rok
                INC     dw1_m+3
                BNE     dp_rok
                INC     dw1_m+2
                BNE     dp_rok
                INC     dw1_m+1
                BNE     dp_rok
                INC     dw1_m
                BNE     dp_rok
                LDA     #$80            ; 全桁あふれ → 仮数 1.0, 指数++
                STA     dw1_m
                JSR     dw1_exp_inc
dp_rok:
                LDD     dw1_exp
                BLE     dp_zero
                CMPD    #255
                BLE     dp_expok
                JMP     errc_6
dp_expok:
                STB     FAC1
                LDA     dw1_m
                ANDA    #$7F
                ORA     dw1_sgn
                STA     FAC1+1
                LDD     dw1_m+1
                STD     FAC1+2
                LDD     dw1_m+3
                STD     FAC1+4
                LDD     dw1_m+5
                STD     FAC1+6
                JMP     dw_t2           ; FACTYPE=2 を立てて復帰 (共通尾部)
dp_zero:
                CLRA
                CLRB
                STD     FAC1
                STD     FAC1+2
                STD     FAC1+4
                STD     FAC1+6
                JMP     dw_t2           ; FACTYPE=2 を立てて復帰 (共通尾部)

;------------------------------------------------------------------------------
; 四則/比較ディスパッチ本体 (7t_float.inc のトランポリンから分岐)。
;   X/Y/U を温存して既存式評価の契約 (X=本文ポインタ) を守る。
;------------------------------------------------------------------------------
; dchk: FACTYPE/FAC2TYPE のどちらかが倍精度なら C=1。破壊: A
dchk:
                LDA     FACTYPE
                CMPA    #2
                BEQ     dc_y
                LDA     FAC2TYPE
                CMPA    #2
                BEQ     dc_y
                ANDCC   #$FE
                RTS
dc_y:
                ORCC    #$01
                RTS

; 倍精度加算の入口 fpa_dbl は 7t_core.s の末尾に置く。

fps_dbl:
                PSHS    X,Y,U
                JSR     d_prep
                LDD     dw2_exp
                BEQ     fps_n0
                LDA     dw2_sgn         ; 右オペランド符号反転 → 左 + (-右)
                EORA    #$80
                STA     dw2_sgn
fps_n0:
                JSR     dfadd_core
                BRA     dbl_pack_ret

fpm_dbl:
                PSHS    X,Y,U
                JSR     d_prep
                JSR     dfmul_core
                BRA     dbl_pack_ret

fpd_dbl:
                PSHS    X,Y,U
                JSR     d_prep
                JSR     dfdiv_core
                ; 倍精度の加減乗除は結果の丸め詰めと復帰が共通なので 1 本にまとめる
                ; (容量の都合。動作は変わらない)。
dbl_pack_ret:
                JSR     d_pack
                PULS    X,Y,U,PC

fpc_dbl:
                PSHS    X,U
                JSR     d_prep
                JSR     dcmp_core
                PULS    X,U
                TSTA
                RTS


