; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; PRINT USING — 数値の 10 進文字列化 (putchar 捕捉)
;   PRINT と同一の整形 (pv_direct: 整数=正確 / 単精度=有効 6 桁 /
;   倍精度=有効 16 桁) を捕捉バッファへ写し取り、ipu_parse で桁列へ分解する。
;   動作観察: "#######,";1234567 → "%1,234,570" (単精度の有効 6 桁丸めが現れる)。
;   本片は $8030 手前の空き。
;==============================================================================
ipu_getdigits:
        PSHS    X,U
        LDA     #1
        STA     pc_capt
        LDX     #strf_dbuf
        STX     pc_capptr
        JSR     pv_direct       ; 行送りの判定を通さない入口 (捕捉のみ)
        CLR     pc_capt
        LDX     pc_capptr
        CLR     ,X              ; 終端
        PULS    X,U
        LDA     #'E'
        LDB     FACTYPE
        CMPB    #2
        BNE     igt_e
        LDA     #'D'            ; 倍精度は D 指数 (動作観察)
igt_e:
        STA     UF_TEXP,U
        JMP     ipu_parse
