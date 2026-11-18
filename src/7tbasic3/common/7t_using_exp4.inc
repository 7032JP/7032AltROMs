; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 指数部の 2 桁化 (共通補助)。B = 0..99 を十の位・一の位で出力。
;   impl_putchar は B/X/U を保存するので B をそのまま持ち回れる。
;==============================================================================
ife_dec2:
        CLRA
ife_dl:
        CMPB    #10
        BLO     ife_d1
        SUBB    #10
        INCA
        BRA     ife_dl
ife_d1:
        ADDA    #'0'
        JSR     impl_putchar
        TFR     B,A
        ADDA    #'0'
        JMP     impl_putchar
