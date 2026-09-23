; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 文字列フィールド整形
;   入力: U=フレーム, B=フィールド幅 (0='@' 全体), 値は STRPTR/STRLEN。
;   幅より短い文字列は空白で埋める (参考書籍、動作観察)。
;   本片は PRINT 本体と同じ 空きプールに置く。
;==============================================================================
ipu_fmtstr:
        LDX     STRPTR
        TSTB
        BNE     ifs_fixed
        LDB     STRLEN          ; '@' = 全体
        BEQ     ifs_done
ifs_alp:
        JSR     put_x_inc
        DECB
        BNE     ifs_alp
ifs_done:
        RTS
ifs_fixed:
        LDA     STRLEN
        STA     UF_TMP,U        ; 残り実文字数
ifs_flp:
        LDA     UF_TMP,U
        BEQ     ifs_pad
        DEC     UF_TMP,U
        JSR     put_x_inc
        DECB
        BNE     ifs_flp
        RTS
ifs_pad:
        JSR     put_space               ; 空白 1 個を出す
        DECB
        BNE     ifs_pad
        RTS
