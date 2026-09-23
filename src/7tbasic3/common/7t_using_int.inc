; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 数値フィールド整形 (4/4a: 整数部の出力)
;   右から 3 桁ごとにコンマを挿入 (コンマ指定時。動作観察)。有効桁を超える下位は '0' (単精度の有効 6 桁丸めの復元)。
;   本片は空きプール。続きは ifn_dot (小数部・後置符号)。
;==============================================================================
ifn_int:
        LDA     UF_SPEC,U
        BMI     ifn_z0          ; '0' 表示 (整数部 0 桁)
        LDB     UF_TMP,U
        LBEQ    ifn_dot
        TFR     B,A             ; 先頭グループ桁数 = ((桁数-1) mod 3)+1
ifn_ml:
        CMPA    #3
        BLS     ifn_mk
        SUBA    #3
        BRA     ifn_ml
ifn_mk:
        STA     UF_TMP2,U
        CLR     UF_J,U
ifn_ilp:
        LDB     UF_J,U
        JSR     ipu_getd
        JSR     impl_putchar
        INC     UF_J,U
        LDB     UF_J,U
        CMPB    UF_TMP,U
        LBHS    ifn_dot         ; 全桁出力済み
        DEC     UF_TMP2,U
        BNE     ifn_ilp
        LDA     UF_SPEC,U
        BITA    #8
        BEQ     ifn_m3
        LDA     #','
        JSR     impl_putchar
ifn_m3:
        LDA     #3
        STA     UF_TMP2,U
        BRA     ifn_ilp
ifn_z0:
        JSR     put_zero
        JMP     ifn_dot
;==============================================================================
; PRINT USING — 数値フィールドの桁位置数の上限判定
;   数値書式が占める桁位置は 24 個
;   までで、超えた書式は Illegal Function Call となる。
;   数える桁位置 = 整数部 (先頭 '+' / '**' / 円記号 / ',' を含む) + 小数点 1 個
;                  + 小数部。指数指定 '^^^^' と後置符号 '+'/'-' は数えない。
;   上限判定を持たないと桁位置計数が 1 バイトで巻き戻り、過大な書式が黙って
;   通ってしまう。
;   入口: ins_valchk から JMP (A=UF_SPEC, S=フレーム基底)。本片は空きプール。
;==============================================================================
ipu_wchk:
        LDB     UF_WINT,S
        ADDB    UF_DECLEN,S
        BITA    #16             ; 小数点も 1 桁位置を占める
        BEQ     ipw_nodot
        INCB
ipw_nodot:
        CMPB    #24
        BLS     ipu_dofield_num
        JMP     errc_5          ; Illegal Function Call
