; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 指数形式 '^^^^' の前処理 (仮数の桁合わせと指数値の確定)
;   仮数の整数部桁数 k は桁指定数 - 1。ただし後置符号があるときは符号が桁位置を
;   使わないので k = 桁指定数 (動作観察)。
;   値 = 0.dig × 10^dexp を仮数 (整数部 k 桁) へ移すので、指数値 = dexp - k。
;   値が 0 のときは指数も 0 (動作観察)。
;   コンマ区切りは桁位置としては数えるが区切り自体は出さない (動作観察) ため、ここで指定を落とす。
;   本片は空きプール。
;==============================================================================
ifn_expprep:
        TST     UF_EXPF,U
        BEQ     iep_x
        LDA     UF_SPEC,U
        ANDA    #$F7            ; コンマ区切りは出さない (幅は計上済)
        STA     UF_SPEC,U
        LDB     UF_WINT,U
        LDA     UF_POST,U
        BNE     iep_k           ; 後置符号あり → k = 桁指定数
        DECB
iep_k:
        LDA     UF_DLEN,U
        BEQ     iep_z           ; 値 0 → 指数 0
        LDA     UF_DEXP,U
        STB     UF_DEXP,U       ; 仮数の整数部桁数 = k
        PSHS    B
        SUBA    ,S+             ; A = 元の dexp - k
        STA     UF_TEXPV,U
iep_x:
        RTS
iep_z:
        CLR     UF_TEXPV,U
        RTS
