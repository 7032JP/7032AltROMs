; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 数値フィールド整形 (2/4: 必要幅の算出)
;   必要幅 = 整数桁 + コンマ数 + 前置符号 + 円記号。整数部が 0 桁のときは
;   幅に収まる場合のみ '0' を表示 (幅に収まらなければ '0' を省く。
;   動作観察)。
;   本片は空きプール。続きは ifn_ov (3/4)。
;==============================================================================
ifn_need:
        LDB     UF_TMP,U        ; 整数部桁数
        LDA     UF_SPEC,U
        BITA    #8              ; コンマ区切り
        BEQ     ifn_nc
        TSTB
        BEQ     ifn_nc
        PSHS    B
        DECB
        CLRA
ifn_ncl:
        CMPB    #3              ; コンマ数 = (桁数-1)/3
        BLO     ifn_ncd
        SUBB    #3
        INCA
        BRA     ifn_ncl
ifn_ncd:
        ADDA    ,S+
        TFR     A,B
ifn_nc:
        LDA     UF_TMP2,U       ; 前置符号
        BEQ     ifn_n1
        INCB
ifn_n1:
        LDA     UF_SPEC,U
        BITA    #4              ; 円記号
        BEQ     ifn_n2
        INCB
ifn_n2:
        LDA     UF_TMP,U
        LBNE    ifn_ov
        INCB                    ; 整数部 0 桁 → '0' を仮に数える
        LDA     UF_DECLEN,U     ; 小数部が無いと '0' は省けない (幅を
        BEQ     ifn_n0          ;   超えて出す。動作観察)
        CMPB    UF_WINT,U
        BHI     ifn_nz
ifn_n0:
        LDA     UF_SPEC,U
        ORA     #$80            ; '0' 表示
        STA     UF_SPEC,U
        JMP     ifn_ov
ifn_nz:
        DECB                    ; 入らない → '0' なし
        JMP     ifn_ov
