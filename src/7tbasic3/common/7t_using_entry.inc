; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 文入口 (フォーマット式評価・フレーム確保・最初の値)
;   PRINT 本体 (ip_loop) が USING トークン ($D1) を検出すると ip_using へ来る。
;   分割配置: ROM 内の空き (ZMB パッド) 群へ機能単位で分置し、
;   相互はラベル参照 (JMP/JSR) のみで位置依存なし。本片は空きプール。
;==============================================================================
ip_using:
        JSR     eval_expr_x1
        JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
        LBNE    u_err13         ; 文字列でない → Type Mismatch
        ; 書式文字列の実体は「プログラム本文」「文字列変数の実体」
        ;   「文の一時領域」のいずれかで、後続の値評価では動かない。
        ;   (連結が文の一時領域を使うため、スタックへの退避は要らない)
        LDY     STRPTR
        LEAS    -UF_SIZE,S
        STY     UF_FBASE,S
        LDA     STRLEN
        STA     UF_FLEN,S
        CLR     UF_FLAGS,S
        JSR     skip_sp_x       ; 書式の後は ';' 必須 (',' は Syntax Error)
        CMPA    #';'
        LBNE    u_err2
        JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
        JSR     ipu_endchk
        LBEQ    ipu_pass        ; 値なし
        JSR     impl_eval_expr  ; 最初の値
        LDA     UF_FLAGS,S
        ORA     #1              ; 値保留
        STA     UF_FLAGS,S
        JMP     ipu_pass
