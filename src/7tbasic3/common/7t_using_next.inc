; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — フィールド実行と次値評価
;   値の型検査 (数値フィールドに文字列 / 文字列フィールドに数値 → Type
;   Mismatch。動作観察)。値が尽きた状態でフィールドへ到達したら打切り (ipu_starve)。
;   区切りは ';' と ',' (どちらも密着連結。動作観察)。
;   本片は空きプール。
;==============================================================================
ipu_dofield_num:
        LDA     UF_FLAGS,S
        BITA    #1
        LBEQ    ipu_starve
        JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
        BEQ    u_err13
        LEAU    ,S              ; U = フレーム基底
        PSHS    X
        JSR     ipu_fmtnum
        PULS    X
        BRA     ipu_consumed
ipu_dofield_str:
        LDA     UF_FLAGS,S
        BITA    #1
        LBEQ    ipu_starve
        JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
        BNE    u_err13
        LEAU    ,S
        PSHS    X
        JSR     ipu_fmtstr
        PULS    X
ipu_consumed:
        LDA     UF_FLAGS,S
        ORA     #2              ; 当周で消費
        ANDA    #$FE            ; 値保留クリア
        STA     UF_FLAGS,S
        JSR     skip_sp_x
        CMPA    #';'
        BEQ     ipn_sep
        CMPA    #','
        BEQ     ipn_sep
        JSR     ipu_endchk
        BNE    u_err2
        JMP     ipu_scanlp      ; 文末: 残りのリテラルは出力して終わる (動作観察)
ipn_sep:
        JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
        JSR     ipu_endchk
        BNE     ipn_val
        ; 区切り直後が文末 → 末尾 ';' = 改行抑止 (動作観察)
        LDA     UF_FLAGS,S
        ORA     #4
        STA     UF_FLAGS,S
        JMP     ipu_scanlp
ipn_val:
        JSR     impl_eval_expr
        LDA     UF_FLAGS,S
        ORA     #1              ; 値保留
        STA     UF_FLAGS,S
        JMP     ipu_scanlp
; ipu_getd — B=添字 (符号付き) → A=UF_DIG[B]。範囲外は '0'。Y 破壊
ipu_getd:
        TSTB
        BMI     igd_z
        CMPB    UF_DLEN,U
        BHS     igd_z
        LEAY    UF_DIG,U
        LDA     B,Y
        RTS
igd_z:
        LDA     #'0'
        RTS
; エラー出口
u_err13:
        LDB     #13             ; Type Mismatch
        JMP     impl_error_handler
u_err2:
        JMP     errc_2
