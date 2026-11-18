; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 文字列変数が値を所有するための複写。詰め直しは 7t_strgc.inc にある。
;==============================================================================
; str_own — STRPTR/STRLEN の文字列を恒久ブロックへ複写して所有する。
;   出力: D = 実体先頭。X と U は保存する。
;   複写元が恒久ブロックのままだと確保の途中の詰め直しで動いてしまうので、
;   先に一時領域へ退避してから確保する。
str_own:
                PSHS    U,X             ; X = 呼出元の本文ポインタ (壊してはならない)
                ; (旧実体がごみになる印は str_asg_mark が立てる。空文字列や
                ;  本文中の定数の代入は str_own を通らないため、ここでは間に合わない)
                LDD     STRPTR
                CMPD    FRETOP
                BLS     sto_alloc
                JSR     str_to_tmp
sto_alloc:
                LDB     STRLEN
                JSR     str_perm_alloc          ; X = 実体先頭
                JSR     str_cpy_in              ; STRPTR から STRLEN 複写 (X 保存)
                TFR     X,D                     ; D = 実体先頭
                PULS    X,U,PC

