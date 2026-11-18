; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
; 7T-BASIC 3.1 — ROM 内の権利表示とライセンス識別子
;   末尾の目印 "7T" (BASVEC_SIG) の手前の空きに ASCII で置く。実行には使わない。
sig_copyright:
                FCC     "7T-BASIC 3.1 (c)2026 N.Tsugiiwa"
                FCB     0
sig_license:
                FCC     "MIT-license"
                FCB     0
