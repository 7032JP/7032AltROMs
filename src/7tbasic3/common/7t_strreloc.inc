; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 文字列領域 — システムスタックの移設
;   ROM の空きはアドレスアンカで区切られた区画ごとに独立しているため、文字列領域の
;   実装は複数の空きプールへ分けて配置してある。参照は全てラベル経由で位置
;   依存はない。
;==============================================================================
;------------------------------------------------------------------------------
clear_relocate_stack:
                ; 使用中の範囲は「現在の S から旧上端 (crs_oldtop = 旧 STRBOT) まで」
                ;   (入れ子の枠と直接モードの行の擬似レコードを含む)。これを新上端
                ;   (STRBOT。呼出前に新値へ更新済) の下へそのまま写し、S を張り直す。
                ;   移動量 = 新 STRBOT − 旧 STRBOT (負もあり得る)。
                ;   下げるときは S を先に下げてから下から上へ写し (割込のフレームが
                ;   写し先を踏まない)、上げるときは上から下へ写す (写し元と写し先が
                ;   重なっても壊さない)。
                LDD     STRBOT
                SUBD    crs_oldtop
                STD     crs_new                 ; 移動量
                BMI     crs_down
                ; --- 上げる: 上端から下へ ---
                STS     ARR_NEWEND              ; 写し元の下限 (現在の S)
                LDU     crs_oldtop              ; 写し元 (排他上端)
                LDX     STRBOT                  ; 写し先 (排他上端)
crs_up:
                CMPU    ARR_NEWEND
                BEQ     crs_done
                LDA     ,-U
                STA     ,-X
                BRA     crs_up
crs_down:
                ; --- 下げる: 先に S を下げ、下から上へ ---
                TFR     S,U                     ; 写し元
                TFR     S,D
                ADDD    crs_new
                TFR     D,S                     ; 新 S
                TFR     D,X                     ; 写し先
crs_dn:
                CMPX    STRBOT
                LBHS    crs_fix_execsp          ; EXEC_SP も同量ずらす
                LDA     ,U+
                STA     ,X+
                BRA     crs_dn
crs_done:
                TFR     S,D
                ADDD    crs_new
                TFR     D,S                     ; 新 S
                JMP     crs_fix_execsp          ; EXEC_SP も同量ずらす
