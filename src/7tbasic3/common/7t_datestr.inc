; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 7t_datestr.inc — DATE$ 代入の本体 (date_set_str)
;
;   書式 (参考書籍「DATE$」): DATE$="YY/MM/DD"
;     年は 2 けた (00-99)。全体でちょうど 8 文字でなければ受け付けない。
;     数字 6 個の間に挟まる 2 文字は区切りとして読み飛ばすだけで種類は問わず、
;     保持する値では '/' に揃える (動作観察: "26-08-13" も受理し "26/08/13" を返す)。
;     月は 1-12、日は 1 からその月の日数まで。2 月は年が 4 の倍数なら 29 日。
;     長さ違い・数字でない・範囲外はいずれも Illegal Function Call とする。
;
;   検証を全て通してから一括で格納する。途中で撥ねた代入が保持中の日付を
;     半端に書き換えないようにするため。
;
;   本片は空きプールに置く。DATE$ 代入の在る空きプール には
;   もう空きが無いため。参照はラベル経由のみで位置依存はない。
;==============================================================================

;------------------------------------------------------------------------------
; date_set_str: STRPTR/STRLEN の "YY/MM/DD" を検証して date_buf へ格納する
;   入力: STRPTR/STRLEN = 右辺の文字列 (呼出側が評価済)
;   壊す: A,B,X,U
;------------------------------------------------------------------------------
date_set_str:
                LDA     STRLEN
                CMPA    #8              ; 2 けたの年を含めてちょうど 8 文字
                BNE     dss_err
                LDX     STRPTR
                JSR     tss_2dig        ; B = 年 (00-99 はすべて可)
                PSHS    B
                LEAX    1,X             ; 区切り 1 文字 (種類は問わない)
                JSR     tss_2dig        ; B = 月
                TSTB
                BEQ     dss_err         ; 0 月は無い
                CMPB    #12
                BHI     dss_err
                PSHS    B
                LEAX    1,X             ; 区切り 1 文字
                JSR     tss_2dig        ; B = 日
                TSTB
                BEQ     dss_err         ; 0 日は無い
                LDA     ,S              ; A = 月 (1-12)
                LDX     #dss_mdays-1    ; 月は 1 起点なので 1 手前を基点にする
                LDA     A,X             ; A = その月の日数 (2 月は 28)
                CMPA    #28
                BNE     dss_dcmp        ; 2 月以外はそのまま
                LDA     1,S             ; A = 年 (00-99)
                ANDA    #$03            ; 4 の倍数なら閏年
                BNE     dss_feb28
                LDA     #29
                BRA     dss_dcmp
dss_feb28:
                LDA     #28
dss_dcmp:
                PSHS    A
                CMPB    ,S+             ; 日 > その月の日数 → 撥ねる
                BHI     dss_err
                ; --- 検証完了 → 8 文字を複写し、区切りを '/' に揃える ---
                LEAS    2,S             ; 月・年の作業値を捨てる
                LDX     STRPTR
                LDU     #date_buf
                LDB     #8
dss_cp:
                LDA     ,X+
                STA     ,U+
                DECB
                BNE     dss_cp
                LDA     #'/'
                STA     date_buf+2
                STA     date_buf+5
                LDX     #date_buf       ; 日付 ($02FA-$02FF) を控える
                LDU     #SYS_DATE
                JMP     sixdig_save
dss_err:
                ; 保持中の日付は書き換えずにエラーハンドラへ直行する
                ; (呼出し深さのスタック整理は行わない。time_set_str と同じ既存規約)
                JMP     errc_5          ; Illegal Function Call
; 月ごとの日数表は 7t_save_list.inc に置く。
