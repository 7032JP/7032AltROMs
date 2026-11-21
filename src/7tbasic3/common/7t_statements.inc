; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 命令ハンドラ実装
;==============================================================================

; 汎用スタブ (未実装ステートメント)
;   未実装命令はその引数を読み飛ばしてステートメント末尾 (':' または $00) まで
;   X を進める。文字列リテラル "..." 内の ':' は区切りとみなさない。
impl_stub:
                CLRB                    ; B=0: 文字列外
istub_lp:
                LDA     ,X
                BEQ     istub_done      ; $00 → 行末
                CMPA    #'"'
                BNE     istub_nq
                EORB    #$01            ; 文字列モードトグル
                LEAX    1,X
                BRA     istub_lp
istub_nq:
                TSTB
                BNE     istub_skip      ; 文字列内 → ':' 無視
                CMPA    #':'
                BEQ     istub_done      ; ステートメント区切り → 終了
istub_skip:
                LEAX    1,X
                BRA     istub_lp
istub_done:
                RTS

; NEW
;   本文を空にして全変数・DEF FN の定義の記録・CONT の再開位置を消す。同じ後始末を
;   LOAD も行うので、共通の prog_clear_new を使う (末尾呼び)。
impl_new:
                CLR     prot_flag       ; 本文を捨てるので保護状態も落とす
                JMP     prog_clear_new

; (impl_end / impl_stop の本体は 7t_deffn.inc)

; CONT
;   停止位置から実行を再開する。再開位置が無ければ Can't Continue (17)。
impl_cont:
                LDX     CONTPTR
                BEQ     ic_err
                JMP     ic_go
ic_err:
                LDB     #17
                JMP     impl_error_handler

; RUN [行番号 | "ファイル名"]
;   行番号指定 (RUN 100) は変数クリア後に指定行から実行を開始する (参考書籍。
;   存在しない行番号は Undefined line number)。
impl_run:
                JSR     play_stop       ; RUN は演奏を中断する (参考書籍)
                LDD     #$FFFF
                STD     RUNLIN          ; 既定 = 行番号指定なし (先頭から)
                ; RUN "ファイル名" : 文字列引数があれば先に LOAD してから実行
                JSR     skip_sp_x       ; A = 先頭の非空白 (X はその位置)
                CMPA    #'"'
                BNE     irun_chknum
                JSR     impl_load               ; X=ファイル名 '"' から解析・ロード
                BRA     irun_exec
irun_chknum:
                ; 行番号引数: ASCII 数字なら解析して RUNLIN へ
                CMPA    #'0'
                BLO     irun_exec
                CMPA    #'9'
                BHI     irun_exec
irun_num:
                JSR     impl_eval_linenum       ; D = 行番号 (X は直後へ)
                STD     RUNLIN
irun_exec:
                JMP     impl_exec_prog  ; 戻らない (走り切れば warm_start へ)

; GO (GOTO/GOSUB)
impl_go:
                JSR     skip_sp_take
                CMPA    #TOK_TO         ; TO
                BEQ     igo_goto
                CMPA    #TOK_SUB        ; SUB
                BEQ     igo_gosub
                JMP     ion_err         ; Syntax Error (尾部併合)
igo_goto:
                JSR     impl_eval_linenum
igo_line:                               ; D = 行番号 (ON GOTO もここへ入る)
                JSR     impl_find_line
                BEQ     igo_undef
                ; 見つかった行へ移る (impl_jump_to_line へ流れ込む)
; impl_jump_to_line: 行レコード先頭 X へ実行を移す。
;   入力: X = 行レコード先頭。CURLINE_PTR/CURLIN を更新し、行本文 (原文) を
;   実行用行バッファへ変換して CURIP = 変換済み本文の先頭。X も同位置へ。
;   GOTO/GOSUB/ON 後に行末で iep_next_line が正しい次行を辿れるようにする。
;   CURLIN も飛び先の行番号へ更新する。
impl_jump_to_line:
                JSR     exec_conv_line  ; 変換 + CURLINE_PTR/CURLIN 更新, X=作業列先頭
                STX     CURIP
                ; 行ジャンプでも事象の発生と BREAK キーを見る (文の切れ目を通らないので、
                ;   iep_stmt_gate を同じ手順で通す)。
                JMP     iep_stmt_gate   ; X = 飛び先の文頭 (分岐すれば X はハンドラ行へ)
igo_gosub:
                ; 行番号を評価 (X = 行番号の後の位置 = 復帰 IP)
                JSR     impl_eval_linenum
                ; igo_tail — D = 飛び先の行番号, X = 復帰 IP (ON GOSUB と共通)。
                ;   飛び先を確かめてから枠を積む (無い行番号で枠を残さない)。
igo_tail:
                PSHS    X               ; 復帰 IP
                JSR     impl_find_line  ; X = 行レコード (D は保存される)
                BEQ     igo_undef
                PSHS    X               ; 行レコード
                LDX     2,S             ; X = 復帰 IP
                JSR     gosub_push      ; 枠を積む (溢れは Out Of Memory)
                PULS    X               ; X = 行レコード
                LEAS    2,S
                BRA     impl_jump_to_line
igo_undef:
                JMP     irst_undef      ; Undefined line number (尾部併合)


; RETURN
impl_return:
                ; 枠列から最も近いサブルーチンの枠を探す。その内側に残った繰り返し /
                ; WHILE の枠は、サブルーチンを抜けるのと一緒に捨てる。
                LDB     #TAG_GOSUB
                JSR     stk_find        ; X = 枠 (Z=0 なら無い)
                BNE     pret_err
                LDU     1,X             ; 復帰 IP
                STU     CURIP           ; frame_del は U を壊すので先に確定させる
                LDD     3,X             ; 復帰行レコード
                STD     CURLINE_PTR
                LEAX    FRM_SMALL,X
                JSR     frame_del       ; 枠とその内側を捨てる (S / EXEC_SP を上げる)
                LDD     >il_start_ln    ; RETURN 行番号 (0 = 指定なし)
                BEQ     pret_here
                JSR     impl_find_line  ; D = 行番号 → X = 行レコード先頭
                LBEQ    errc_8          ; 無い行番号 → Undefined Line Number
                JMP     impl_jump_to_line

pret_here:
                ; CURLIN も復帰行の行番号へ戻す (GOTO 側の更新と対称。エラー行
                ; 表示・TRON・実行行の観測を正しく保つ)。直接モードの行の擬似
                ; レコードは行番号欄が $FFFF なので、そのまま直接モードの標識になる。
                LDX     CURLINE_PTR
                LDD     2,X
                JSR     exec_ensure_curlin
                LDX     CURIP
                RTS
pret_err:
                LDB     #3              ; Return without GOSUB
                JMP     impl_error_handler

; REM (行末まで捨てる。素朴な $00 走査でよい)
; DATA は文の区切り ':' でも終わるので REM とは別実体 (7t_datitem.inc)
impl_rem:
irm_loop:
                LDA     ,X+
                BNE     irm_loop
                LEAX    -1,X
                RTS

; ELSE (IF 真で THEN 節実行後に到達 = ELSE 節を実行しない): 作業列の行末 $00 まで
;   読み飛ばす。作業列に $00 と同値のバイトはトークン列・関数第 2 バイト
;   ($80 以上)・文字列リテラル (印字可能文字) のいずれにも現れないため、
;   REM と同じ素朴な走査でよい (契約も同一: X = 行末 $00 の位置で返す)。
impl_else       EQU     impl_rem

; IF
impl_if:
                JSR     impl_eval_expr
                JSR     impl_is_false   ; Z=1 なら値=0 (偽)
                BNE     iif_true        ; 非0 (真) → THEN 側へ
                ; 偽: ELSE を行末まで探す (tok_find)。見つかれば直後から実行。
iif_skip:
                LDB     #TOK_ELSE+1
                BSR     tok_find
                BCS     iif_atend       ; 行末 → ELSE 無し
                ; X は ELSE 直後。行番号が続けば暗黙 GOTO、文なら実行。
                JMP     iif_target
iif_atend:
                ; 行末 $00 の上で止まる (impl_rem と同じ契約)。
                LEAX    -1,X
iif_true:
                LDA     ,X
                CMPA    #TOK_THEN       ; THEN
                BNE     iif_end         ; THEN 無し → 復帰 (後続を文実行 / 行末なら次行)
                LEAX    1,X             ; THEN を消費
                ; THEN 直後が行番号なら暗黙 GOTO (本体 iif_target)
                JMP     iif_target
iif_end:
                RTS

; tok_find: 作業列の X から、トークン ($80 以上) を行末まで探す。B = 探すトークン + 1。
;   文字列リテラルの中は探さない。関数前置 (TOK_FUNC_PFX = $FF) の第 2 バイトは命令の
;   トークンと値が重なるので読み飛ばす。
;   戻り: C = 0 → 見つけた (X = トークンの直後)。C = 1 → 行末 (X = 行末 $00 の直後)。
;   壊す: A。
tok_find:
                PSHS    B               ; ,S = 探すトークン + 1
                CLRB                    ; B = 引用符の数
tkf_next:
                LDA     ,X+
                BLE     tkf_tok         ; 行末か、bit7 の立つ字
                CMPA    #'"'
                BNE     tkf_next
                INCB                    ; 引用符を数える (数の bit0 = 引用符の中)
                BRA     tkf_next
tkf_eol:
                COMA                    ; C = 1 (見つからない)
tkf_ret:
                PULS    B,PC
tkf_tok:
                BEQ     tkf_eol
                BITB    #1
                BNE     tkf_next        ; 引用符の中の字は読み流す
                INCA                    ; 関数前置 ($FF) は 0 になる
                BEQ     tkf_pfx
                CMPA    ,S
                BNE     tkf_next
                BRA     tkf_ret         ; 見つけた (C = 0)
tkf_pfx:
                LEAX    1,X             ; 第 2 バイトを読み飛ばす
                BRA     tkf_next

; LET (明示 LET / 暗黙代入の共通入口)
;   X = 変数名先頭。impl_eval_expr が U/VARTYP を破壊するため、変数値
;   アドレス(U) と型(VARTYP) を退避してから式評価し、復元して格納する。
impl_let:
                JSR     skip_sp_x       ; LET の後ろは変数名で始まること
                JSR     chk_alpha       ; 英字でなければ Syntax Error
impl_let_var:                           ; 暗黙の代入の入口 (X = 変数名の先頭)
                JSR     impl_get_var_ref
                ; 左辺アドレスは let_tgt に置く (vars_make_room が追従させる)。
                STU     let_tgt         ; 変数値アドレスを退避
                LDA     VARTYP
                PSHS    A               ; 変数型を退避
                JSR     skip_sp_take
                CMPA    #TOK_EQ         ; = トークン
                BNE     ilet_err
                JSR     impl_eval_expr
                PULS    A               ; 変数型復元
                STA     VARTYP
                LDU     let_tgt         ; 変数値アドレス復元 (移動済なら追従済)
                JMP     impl_assign_var ; 末尾呼び (X は保存されて返る)
ilet_err:
                BRA     ion_err         ; Syntax Error (尾部併合)

; RESTORE [行番号]
;   行番号省略時はプログラム先頭 (TXTTAB) から DATA を探す。
;   行番号指定時はその行の行レコード先頭から探す。
;   いずれも DATLINE=対象行, DATPTR=0 (未探索) に初期化する。
impl_restore:
                JSR     skip_sp_x       ; RESTORE と行番号の間の空白を読み飛ばす
                ; (skip_sp_x: X=最初の非空白, A=そのバイト)
                ; ASCII 数字が続けば行番号指定
                CMPA    #'0'
                BLO     irst_top
                CMPA    #'9'
                BHI     irst_top
irst_lineno:
                JSR     impl_eval_linenum   ; D = 行番号, X = オペランド直後
                PSHS    X                   ; オペランド直後の本文ポインタを退避
                JSR     impl_find_line      ; X = 行レコード先頭 (本文ポインタを破壊)
                BEQ     irst_undef_pop
                STX     DATLINE
                PULS    X                   ; 本文ポインタ復元 (RESTORE 文の続き)
                BRA     irst_clrptr
irst_top:
                LDD     TXTTAB
                STD     DATLINE
irst_clrptr:
                CLRA
                CLRB
                STD     DATPTR              ; 未探索
                RTS
irst_undef_pop:
irst_undef:
                JMP     errc_8

; ON
impl_on:
                ; ON ERROR GOTO 行番号 (エラートラップ有効化) を先に判定
                JSR     skip_sp_x
                CMPA    #TOK_ERROR      ; ERROR トークン
                LBEQ    ion_onerr
                JSR     impl_eval_expr
                ; 選択値は impl_on_select が整数化する。
                JSR     skip_sp_take
                CMPA    #TOK_GO         ; GO
                BNE     ion_err
                JSR     skip_sp_take
                CMPA    #TOK_TO         ; TO
                BEQ     ion_goto
                CMPA    #TOK_SUB        ; SUB
                BEQ     ion_gosub
ion_err:
                JMP     errc_2
ion_goto:
                JSR     impl_on_select
                BEQ     ion_done
                ; 選んだ行番号 D へは GOTO と同じ本体で飛ぶ (無い行は Undefined Line Number)
                JMP     igo_line
ion_done:
                RTS
ion_gosub:
                JSR     impl_on_select  ; D=選択行番号, X=ON文末 (復帰IP)
                BEQ     ion_done
                JMP     igo_tail        ; D = 選択行番号, X = ON 文末 (復帰 IP)

; FOR
impl_for:
                JSR     get_var_ref_chk ; 変数名で始まらない綴りは Syntax Error
                ; 制御変数アドレスを for_var_ptr へ退避する。
                ;   INT(%) 変数: 値先頭 U に 16bit 整数 → 16bit 整数の経路。
                ;   単精度変数: 値は内部形式 4B。bit15 を立てて印を付け、NEXT/初期化は
                ;     内部形式で読み書きし float 加算/比較の実数 FOR へ分岐する。
                STU     for_var_ptr
                LDA     VARTYP
                CMPA    #VTYPE_INT
                ; 整数 (%) 制御変数の構築は for_setup_int (7t_arrays.inc) が担う。
                LBEQ    for_setup_int
                ; 単精度: for_var_ptr の bit15 を立て、実数 FOR セットアップへ
                LDD     for_var_ptr
                ORA     #$80
                STD     for_var_ptr
                JMP     for_setup_sng

; NEXT
;   ループ継続時: X = ループ本体先頭 IP (pool_next_loop が CURIP を設定)。
;   ループ終了時: X = 変数の並びを読み終えた位置 (カンマか文末)。ディスパッチャは
;     ハンドラ復帰時の X を次 IP とするため、終了経路でこの X を戻す。
;   NEXT の変数引数は読み飛ばす。
in_name_end:
                PSHS    X               ; この時点の X (次のカンマ/文末) を退避
                JSR     pool_next_loop  ; NEXT 本体
                LDA     in_cont_flag
                BEQ     in_this_done    ; この段は終了 → 次の変数へ
                LEAS    2,S             ; 継続: 退避 X 破棄, X=本体IP のまま復帰
                RTS
in_this_done:
                PULS    X               ; X = カンマ/文末位置
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     in_all_done     ; カンマ無し → NEXT 全体終了
                LEAX    1,X             ; ',' を消費し次変数へ
                ; カンマ区切りの NEXT 変数リスト (NEXT / NEXT I / NEXT J,I) を処理。
                ; 各変数につき最内ループを 1 段判定する。継続する段が見つかれば
                ; その本体 IP (X) で復帰。全段終了なら NEXT 直後 (X) で復帰。
impl_next:
in_var_loop:
                ; 1 変数ぶんの名前を読み飛ばす (':'/','/$00 で止まる。空白も読み飛ばす)
in_skipname:
                LDA     ,X
                BEQ     in_name_end
                CMPA    #':'
                BEQ     in_name_end
                CMPA    #','
                BEQ     in_name_end
                LEAX    1,X
                BRA     in_skipname
in_all_done:
                RTS                     ; X = NEXT 直後 (文末/':' の手前)

; WHILE
;   条件を評価し、真なら本体へ (制御フレームを 1 回だけ積む)、
;   偽なら対応する WEND の直後へスキップする (未 push なら積まず、
;   WEND 経由の再評価で積み済みなら skip_wend 側が 1 段 pop する)。
;   ※ impl_is_false は Z=1 で値 0 (=偽)。
impl_while:
                JSR     impl_eval_expr
                JSR     impl_is_false
                BEQ     iw_false        ; 値 0 = 偽 → WEND スキップ
                JMP     impl_push_while ; 真 → フレーム確保して本体へ
iw_false:
                JMP     impl_skip_wend

; READ
;   impl_read_data は DATA 走査のため X を破壊する (DATPTR を辿る)。
;   プログラム本文側のポインタ X は変数名直後を指したままにしておく必要が
;   あるため、read_data の前後で X を退避/復元する。
impl_read:
ird_lp:
                JSR     get_var_ref_chk ; 変数名で始まらない綴りは Syntax Error
                ; impl_read_data は U/VARTYP を破壊するため変数アドレス/型も退避。
                PSHS    X               ; 2,S:3,S = 本文ポインタ (後で復元)
                PSHS    U               ; 0,S:1,S = 変数値アドレス
                LDA     VARTYP
                PSHS    A               ; 0,S = 変数型
                JSR     impl_read_data
                PULS    A               ; 変数型復元
                STA     VARTYP
                PULS    U               ; 変数値アドレス復元
                JSR     impl_assign_var
                PULS    X               ; 本文ポインタを復元
                ; DATA 走査が別の行を作業列へ載せているため、READ 文自身の行を
                ; 復元してから続き (X) を読む (値の代入は復元前に済んでいる)。
                JSR     exec_ensure
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     ird_done
                LEAX    1,X
                BRA     ird_lp
ird_done:
                RTS

; DIM
impl_dim:
idm_lp:
                JSR     impl_decl_array
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     idm_done
                LEAX    1,X
                BRA     idm_lp
idm_done:
                RTS


; TRON/TROFF
impl_tron:
                LDA     #1
                STA     trace_flag
                RTS
impl_troff:
                CLR     trace_flag
                RTS

; POKE アドレス, 値
;   書込先アドレスは U に退避し、本文ポインタ X は保持する。
impl_poke:
                ; 書込みアドレスは 0〜65535、値は 0〜255 (参考書籍 POKE)。
                ; 範囲外は Illegal Function Call。アドレスはゼロ方向切り捨て、
                ; 値は最近接丸め。
                JSR     eval_to_uaddr   ; 書込みアドレス (0..65535 の検査つき)
                PSHS    D               ; 書込先アドレスを退避
                LDA     ,X+
                CMPA    #','
                BNE     ipk_err
                JSR     eval_to_int_255 ; 値 (0..255 検査つき)
                PULS    U               ; U = 書込先アドレス (X=本文ポインタは保持)
                STB     ,U
                RTS
ipk_err:
                PULS    D
                JMP     ion_err         ; Syntax Error (尾部併合)

; EXEC
; 本体は exec_body。
                include "7t_using_num2a.inc"    ; PRINT USING 数値整形 2/4
;------------------------------------------------------------------------------
; iget_sel — GET@ / GET@A の形式判別と発行 (impl_get から末尾呼出で入る)。
;   書式指定子 G の有無が形式1/2 と形式3/4 を分ける。サブ CPU 側のキャラクタ矩形読出
;   ($06/$08) と図形矩形読出 ($1B/$1D) はどちらも「$D383=計数 / $D384〜=データ、
;   領域が尽きたら継続要求 $64」という同じ応答形式なので、受取本体 (iget_body) は
;   4 形式で共通にできる。
;------------------------------------------------------------------------------
iget_sel:
                LDU     gp_arr_ptr          ; 受け皿の配列が無ければ何もしない
                BEQ     iget_none
                LDA     #$1B                ; 形式3 = 単色ビットイメージ読出
                LDB     gp_optn             ; 配列(1)+書式指定子 G(1) が揃ったか
                CMPB    #2
                BHS     iget_pl
                BSR     igc_chr_rect        ; 矩形をキャラクタ座標の並びへ組み直す
                LDA     #$06                ; 形式1 = キャラクタ矩形読出
iget_pl:
                LDB     gp_attr             ; @A (形式2/4) は 1 つ上のコマンド
                BEQ     iget_go
                ADDA    #2                  ; $06→$08 (属性付き) / $1B→$1D (3 面)
iget_go:
                STA     SUBCMD_CMD
                JMP     iget_body           ; 応答の受取 (分割応答の継続要求ループ)
iget_none:
                RTS

;------------------------------------------------------------------------------
; igc_chr_rect — 矩形をキャラクタ矩形コマンド ($06-$09) の並びへ組み直す。
;   図形矩形コマンド ($1B-$1E) は 4 つの座標を 16 bit で $D383/$D385/$D387/$D389
;   に採るが、キャラクタ矩形コマンドは桁・行が 1 byte なので
;   $D383=x1 / $D384=y1 / $D385=x2 / $D386=y2 と詰めて並べる。
;   igp_common が組んだ 16 bit の並びを、この 1 byte の並びへ置き換える。
;   破壊: A, B。
;------------------------------------------------------------------------------
igc_chr_rect:
                LDA     tmpX16+1
                LDB     tmpY16+1
                STD     SUBCMD_P1           ; $D383=x1 (桁), $D384=y1 (行)
                LDA     tmpX16_2+1
                LDB     tmpY16_2+1
                STD     SUBCMD_P3           ; $D385=x2 (桁), $D386=y2 (行)
                RTS

;------------------------------------------------------------------------------
; dsv_free_chain — FAT バッファ (str_work_buf) 上の割当鎖を $FF (未使用) へ開放する
;   入力: A = 開始クラスタ ($98 以上 = 終端値なら何もしない)。
;   壊れた鎖 (循環) でも停止する: 訪れたエントリは先に $FF へ書き換わるため、
;   同じ位置へ戻った時点で終端値 ($FF≥152) を読んで抜ける。破壊: A,B,X
;------------------------------------------------------------------------------
dsv_free_chain:
dfc_lp:
                CMPA    #152
                BHS     dfc_done        ; 終端値 ($C0-$C7/$FF ほか) → 完了
                LDX     #str_work_buf+5
                TFR     A,B
                ABX                     ; X = 当該クラスタの割当表位置
                LDA     ,X              ; A = 次クラスタ
                LDB     #$FF
                STB     ,X              ; 開放
                BRA     dfc_lp
dfc_done:
                RTS

;------------------------------------------------------------------------------
; 周期タイマ割込の所有 (7t_time.inc の所有規約の実体)。
;   控え fd02_shadow の bit7 = 「BASIC がハードの許可 ($FD02 bit2) を所有中」。
;   持っていない時は $FD02 へ書かない。
;   いずれも A のみ使用 (退避・復元)。X/Y/U/B は保存。CC は変化する。
;------------------------------------------------------------------------------
; time_hw_arm: 所有していなければ取得する ($FD02 へ $04)。
time_hw_arm:
                PSHS    A
                LDA     >fd02_shadow
                BMI     tha_ret         ; bit7 = 既に所有中
                ORA     #$80
                STA     >fd02_shadow
                LDA     #$04
                STA     >PORT_CMT
tha_ret:
                PULS    A,PC
; time_hw_free: 所有していれば返す ($FD02 へ $00)。未所有なら無書込。
time_hw_free:
                PSHS    A
                LDA     >fd02_shadow
                BPL     thf_ret         ; 未所有 → 触らない
                ANDA    #$7F
                STA     >fd02_shadow
                CLR     >PORT_CMT
thf_ret:
                PULS    A,PC
; time_hw_sync: BASIC が刻みを要るか (割込演奏中 / INTERVAL 計時中) を見て
;   所有を合わせる。要らなくなっていれば返す。
time_hw_sync:
                TST     >play_active
                BNE     time_hw_arm     ; 割込演奏中 → 要る
                PSHS    A
                LDA     >intv_sub       ; INTERVAL 計時中 (16bit が 0 でない) か
                ORA     >intv_sub+1
                PULS    A               ; PULS は CC を変えない (Z は ORA のまま)
                BNE     time_hw_arm
                BRA     time_hw_free
; time_rearm: 委譲から復帰した時の本体。
;   ROM 側 tick 処理の許可 (bit2) を戻し、要否に応じて所有を直す。
time_rearm:
                BSR     time_perm_on
                BRA     time_hw_sync
; intv_arm_tick: INTERVAL の秒未満計数を刻み直し、所有を取る (計時開始)。
;   計時開始を先にしてから所有を取る (タイマ割込との競合回避)。破壊: D。
intv_arm_tick:
                LDD     #INTV_TPS
                STD     >intv_sub
; time_hw_claim: 刻みを要り始める側の所有取得。ROM 側 tick 処理の許可 (bit2) を
;   戻してから所有を取る。
time_hw_claim:
                BSR     time_perm_on
                BRA     time_hw_arm
; time_perm_on: 控えの ROM 側 tick 処理の許可 (bit2) を立てる。ハードは触らない。
time_perm_on:
                PSHS    A
                LDA     >fd02_shadow
                ORA     #$04
                STA     >fd02_shadow
                PULS    A,PC

;------------------------------------------------------------------------------
; str_tmp_free_top — STRPTR/STRLEN の実体が一時領域の最上位なら、その一時を返す。
;   値を使い終えた場所で最上位 (末尾が TMPPTR に接している) の 1 個だけを返す。
;   破壊: D。X/U は触らない。
;------------------------------------------------------------------------------
str_tmp_free_top:
                LDB     STRLEN
                CLRA
                ADDD    STRPTR
                SUBD    #1              ; D = 実体の末尾
                CMPD    TMPPTR
                BNE     stft_ret        ; 最上位ではない → 返せない
                LDD     STRPTR
                SUBD    #1
                STD     TMPPTR          ; 実体の 1 つ下まで一時を返す
stft_ret:
                RTS


;   LOCATE の本体は第 3 引数 ,S (カーソルスイッチ) を扱う impl_locate3
;   (7t_color_video.inc)。ジャンプ表もそちらを指す。
;------------------------------------------------------------------------------
; KEY LIST — ファンクションキー (PF1〜PF10) の割当て一覧を表示する
;
;   `KEY LIST` は各キーの割当て文字列を
;   1 行 1 キーで一覧表示する。書式は
;       各キーごとに [CR LF]"PF"<キー番号><桁 9 まで空白><割当て文字列>
;   を PF1 から PF10 まで 10 回、最後にもう 1 回 CR LF、である。
;   先頭の改行は無条件 (行途中でも必ず行を送ってから一覧を始める)。
;   割当て文字列は桁 9 から始まるので、PF10 の行だけラベルが 4 桁になり
;   空白が 1 つ少なくなる。桁合わせは PRINT_COL を見ながら空白を出して行う。
;
;   割当て文字列はサブシステム側の PF 定義テーブルにあり、メイン CPU からは
;   直接読めない。サブシステムコマンド $2B (Get String of PF) を
;   キー番号ごとに発行し、共有 RAM 経由で読み戻して出力する。
;     入力  $D383 = キー番号
;     出力  $D384 = 文字数, $D385〜 = 文字列 (最大 15 文字。16 byte 枠の未使用部は $00)
;   ステージング側では SUBCMD_P1 / SUBCMD_P2 / SUBCMD_P3 が同位置に対応する。
;   15 文字のときは枠の中に 0 終端が無いので、kl_init が枠の直後を 0 にしておく。
;
;   X はディスパッチャの続行位置 (復帰後 CURIP へ格納される) なので必ず
;      保存・復元する。C=1 で戻り、呼出元 impl_key は割当て処理をせず復帰する。
impl_key_list:
                PSHS    X               ; ディスパッチャ続行位置を保護
                BSR     kl_init         ; out_vec ← コンソール, A = 1 (最初のキー番号)
kl_line:
                PSHS    A
                JSR     print_inline    ; (戻り A=0 → print_uint16 の上位に流用)
                FCB     $0D,$0A
                FCC     "PF"
                FCB     0
                LDB     ,S
                JSR     print_uint16    ; キー番号を 10 進出力
kl_pad:
                JSR     put_space               ; 空白 1 個を出す
                LDA     PRINT_COL
                CMPA    #9
                BLO     kl_pad
                JSR     kl_body         ; $2B 発行 → 割当て文字列を出力
                PULS    A
                INCA
                CMPA    #11
                BNE     kl_line
                JSR     kl_fin          ; 一覧の後に 1 行送り、表示状態を控えへ戻す
                COMA                    ; C=1 (呼出元 impl_key は何もせず戻る)
                PULS    X,PC
                include "7t_arr_vncopy.inc"     ; 変数名の複写

; (COLOR の本体は impl_color_disp。ジャンプ表はそちらを指す)

impl_console    EQU     impl_console_ext

; PSET / PRESET
; 構文:
;   PSET   (水平位置, 垂直位置 [, [パレットコード] [, 機能]])
;   PRESET (水平位置, 垂直位置 [, [パレットコード] [, 機能]])
; パレットコードと機能は括弧の内側に書く。省略時はパレットコードが現在の
; 描画色 (PRESET は背景色)、機能が PSET となる。
; 引数列の解析は pool_gr_opts が担う。
; subsys $17 POINT パラメータ:
;   $D383 = N (点数=1)
;   $D384-$D385 = X1 (16bit)
;   $D386-$D387 = Y1 (16bit)
;   $D388 = CL (カラー)
;   $D389 = F  (ファンクション)
impl_pset:
                JSR     impl_parse_xy_cmd   ; tmpX16, tmpY16 に座標
                LDA     cur_fg_color        ; 既定パレットコード = 現在の描画色
ipset_opt:
                ; A = 既定パレットコード。復帰時 A = パレットコード, B = 機能
                JSR     pool_gr_opts
                ; CL/F は先に確定させる。
                STD     SUBCMD_P6           ; CL (subsys $17 は 0 = 全プレーン消去) と F
                ; 括弧内引数形 "(x,y,c,f)" では閉じ括弧が残る。ここで畳む
                JSR     skip_sp_x
                CMPA    #')'
                BNE     ipset_send
                LEAX    1,X
ipset_send:
                LDD     #$1701
                STD     SUBCMD_CMD          ; cmd=$17 / N=1 (隣り合うので 16 ビットで置く)
                LDD     tmpX16
                STD     SUBCMD_P2           ; X1 (16bit)
                LDD     tmpY16
                STD     SUBCMD_P4           ; Y1 (16bit)
                JMP     impl_sub_send_cmd
;------------------------------------------------------------------------------
; alloc_limit: 変数/配列を伸ばしてよい上限アドレスを D に返す。
;   上限 = 呼出時点のシステム S − STK_RESERVE。システムスタックの上端は文字領域の
;   下限 (STRBOT) なので、S が変数・配列と文字領域の間の未使用領域の境界そのもの
;   である (参考書籍)。STK_RESERVE は 7t_defs.inc。
;   本ルーチンは JSR で呼ばれる。TFR S,D で得られる値は復帰アドレス 2B を
;     積んだ後の S なので +2 して呼出時点の S に戻す。
;   破壊: D (CC)。X/Y/U は保存される。
;------------------------------------------------------------------------------
alloc_limit:
                TFR     S,D
                ADDD    #2-STK_RESERVE          ; 呼出時点の S − 最小の空き
                RTS
;------------------------------------------------------------------------------
; kl_init — KEY LIST の初期化断片。impl_key_list から JSR で呼ぶ。
;   (1) print_uint16 は out_vec 経由で出力する。SAVE が出力先をテープ側へ
;       向けたまま残すため、一覧表示のたびにコンソールへ向け直す。
;   (2) 割当てが 15 文字だと 0 終端が無いので、ステージングの直後 1 byte を 0 にしておく。
;   戻り A = 1 (最初のキー番号)。
kl_init:
                JSR     out_vec_screen  ; 出力先を画面へ戻す (共通化)
                CLR     SUBCMD_P3+15    ; 15 文字割当て時の 0 終端 (保険)
                JMP     kl_init2
impl_preset:
                JSR     impl_parse_xy_cmd
                ; 消去 = 背景色 (CL=0 相当) で打点する (背景色が 0 なら全プレーンのビットが消える)。
                ; パレットコード/機能を明記した場合はそちらが優先される (PSET と同文法)
                LDA     cur_bg_color
                BRA     ipset_opt
;------------------------------------------------------------------------------
; crs_fix_execsp: clear_relocate_stack の移設量だけ EXEC_SP をずらす。
;   移設量 = 新 STRBOT - 旧 STRBOT (crs_oldtop)。
;   破壊: D (CC)。
;------------------------------------------------------------------------------
crs_fix_execsp:
                LDD     STRBOT                  ; 新スタック上端
                SUBD    crs_oldtop              ; D = 移設量 (負もあり得る)
                ADDD    EXEC_SP
                STD     EXEC_SP                 ; 保存済み S も同量だけ移設
                RTS
;------------------------------------------------------------------------------
; igc_optnum: GET@/PUT@ のオプション引数を 1 個読み取り B へ整数で返す。
;   書式指定子は式評価に回さず、ここで消費して値なし (0) を返す。
;   入力: X = 本文ポインタ (先頭空白は除去済) / 出力: B = 値, X = 引数の直後。
;------------------------------------------------------------------------------
igc_optnum:
                LDA     ,X
                CMPA    #'G'                    ; ドットパターン形式の書式指定子
                BNE     igon_expr
                LEAX    1,X                     ; 消費のみ (変数を作らない)
                CLRB                            ; 値なし = 0
                RTS
igon_expr:
                ; 通常の数値式。GET@/PUT@ の数値オプション (PUT@ の機能指定と
                ; GET@ の色リスト) はいずれも 0..15 で、範囲外は Illegal
                ; Function Call (動作観察)。
                JMP     eval_to_int_15          ; 末尾呼出 (B = 整数値 / 値域検査つき)
