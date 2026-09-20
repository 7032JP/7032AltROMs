; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
; >>FOLD eval_chk_str
;------------------------------------------------------------------------------
; eval_chk_str — 式を評価し、結果が文字列型かどうかを Z フラグで返す定型。
;   Z=1 なら文字列 (FACTYPE=3)。SYMBOL / TIME$ / DATE$ / PLAY の 4 箇所が同じ
;   3 命令を並べていたので 1 本にまとめた (容量捻出)。
;   RTS は CC を変えないので、呼出側は元どおり分岐できる。
;------------------------------------------------------------------------------
eval_chk_str:
                JSR     impl_eval_expr
                JMP     chk_type_str    ; FACTYPE を文字列型と比較 (共通化、末尾呼び)
; <<FOLD eval_chk_str

;==============================================================================
; 7t_evalhelp.inc: 式評価まわりの共用ヘルパ (空きプール配置)
;
;   本来の区間に収まらない小さな共用ルーチンを置く。参照はいずれもラベル
;   経由で、位置には依存しない。
;==============================================================================

; sym_eval_str: SYMBOL/GCURSOR の文字列引数を汎用の文字列式として評価し
;   sym_buf へ複写する。リテラル "..." だけでなく文字列変数・連結式
;   ("..."+CHR$(13) 等) にも対応する (呼出し元は 7t_save_list.inc の
;   impl_symbol / igcur_symbol)。本体は空きプールへ配置し、
;   本来の区間を圧迫しない。
;   入力: X = CURIP (文字列式の先頭)
;   出力:
;     文字列 (FACTYPE=3): C=0, B = 文字数 (0-80), sym_buf[0..B-1] に本体,
;                         X は式の直後 (次の ',' 等)。
;     非文字列        : C=1 (呼出側は描画せず done へ分岐して安全終了)。
;   注記:
;     - 演奏は止めない。参考書籍が挙げる PLAY の中断事由に
;       SYMBOL は含まれず、参考書籍は他の処理を進めながら背景で演奏できると
;       定める。sym_buf は str_work_buf の先頭であり、音符キュー (PLAYQ) とは
;       領域が重ならない。
;     - 連結結果は str_work_buf に置かれ sym_buf とは別領域なので
;       STRPTR→sym_buf の複写は安全。
;     - X (=CURIP) は破壊しない。impl_eval_expr が式直後へ前進させた値を
;       そのまま返すため、後続の倍率・色の解析はリテラルの経路と互換。
;     - 複写には U(ソース=STRPTR)/Y(宛先=sym_buf) を使い X を温存する。
sym_eval_str:
                BSR     eval_chk_str    ; Z=1 なら文字列型 (共通化)
                BNE     ses_notstr
                LDB     STRLEN
                CMPB    #80                 ; sym_buf 有効長にクランプ
                BLS     ses_len_ok
                LDB     #80
ses_len_ok:
                PSHS    B                   ; 文字数退避
                LDU     STRPTR              ; ソース (str_work_buf 等)
                LDY     #sym_buf            ; 宛先
                TSTB
                BEQ     ses_copy_end        ; 0文字なら複写不要
ses_copy:
                LDA     ,U+
                STA     ,Y+
                DECB
                BNE     ses_copy
ses_copy_end:
                PULS    B                   ; B = 文字数 (後続コードの前提)
                ANDCC   #$FE                ; C=0 (成功)
                RTS
ses_notstr:
                ORCC    #$01                ; C=1 (非文字列 → 呼出側で安全終了)
                RTS
