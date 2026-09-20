; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
; 保守用 (ワーク RAM の別配置を組むための未定義スイッチ)。連続参照と共用を保ち、ブロックの大きさから順次割付する。
STAGE_PTR          EQU     $0002
STAGE_NUMBER       EQU     STAGE_PTR+44
STAGE_IO           EQU     STAGE_NUMBER+45
STAGE_EXPR         EQU     STAGE_IO+101
STAGE_DRAW         EQU     STAGE_EXPR+22
STAGE_SAVE         EQU     STAGE_DRAW+26
STAGE_ZP_END       EQU     STAGE_SAVE+5
STAGE_INPUT        EQU     $0100
STAGE_EXEC         EQU     STAGE_INPUT+256
STAGE_SUBCMD       EQU     STAGE_EXEC+259
STAGE_RUNTIME      EQU     STAGE_SUBCMD+128
STAGE_FILE         EQU     STAGE_RUNTIME+173
STAGE_BUFFER       EQU     STAGE_FILE+152
STAGE_TEXT         EQU     STAGE_BUFFER+595
STAGE_IRQ       EQU     STAGE_RUNTIME+81
                IFGT    STAGE_ZP_END-$0100
                ERROR   "stage direct page overflow"
                ENDC
