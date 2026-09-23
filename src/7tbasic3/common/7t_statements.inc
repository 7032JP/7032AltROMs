; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 命令ハンドラ実装
;==============================================================================

; 汎用スタブ (未実装ステートメント)
;   未実装命令はその引数を読み飛ばしてステートメント末尾 (':' または $00) まで
;   X を進める。引数を残すと、数値定数プレフィクス $FE 等が後続ディスパッチで
;   命令トークンと誤認され暴走・誤エラーの原因になるため。
;   文字列リテラル "..." 内の ':' は区切りとみなさない。
;   ($FE 数値定数は文字列外に出現し ':' を含まないため、$FE のデータ追従は
;    不要 — ':'/$00/'"' 以外は単純スキップで安全。)
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
;   本文を空にして全変数・DEF FN 定義表・CONT の再開位置を消す。同じ後始末を
;   LOAD も行うため、実体は共通の prog_clear_new 1 本に寄せてある (末尾呼び)。
impl_new:
                CLR     prot_flag       ; 本文を捨てるので保護状態も落とす
                JMP     prog_clear_new

; (impl_end / impl_stop の本体の置き場所は容量の都合で 7t_cmt_io.inc の空きプール)

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
                JSR     impl_find_line
                BEQ     igo_undef
                JMP     impl_jump_to_line
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
                JMP     impl_jump_to_line
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
                ; 偽: ELSE トークンを行末まで探す。見つかれば直後から実行。
                ; 文字列リテラル中の偶発バイトを避けるため '"' 内はスキップ。
                ; 関数前置 (TOK_FUNC_PFX) の第 2 バイトは値域が命令トークンと
                ; 重なるため 1 バイト読み飛ばす (ELSE と同値のバイトを含み得る)。
iif_skip:
                LDA     ,X+
                BEQ     iif_atend       ; 行末 → ELSE 無し
                CMPA    #'"'
                BNE     iif_sk_chk
iif_sk_str:
                LDA     ,X+
                BEQ     iif_atend
                CMPA    #'"'
                BNE     iif_sk_str
                BRA     iif_skip
iif_sk_chk:
                CMPA    #TOK_FUNC_PFX   ; 関数前置 ($FF) か
                BNE     iif_sk_ne
                LEAX    1,X             ; 関数トークン 1 バイトを読み飛ばす
                BRA     iif_skip
iif_sk_ne:
                CMPA    #TOK_ELSE       ; ELSE
                BNE     iif_skip
                ; X は ELSE 直後。行番号が続けば暗黙 GOTO、文なら実行。
                JMP     iif_target
iif_atend:
                ; 行末 $00 を消費したままにしない (impl_rem と同じ「$00 の上で
                ;   止まる」契約)。消費したまま返すとディスパッチャが次行ヘッダの
                ;   リンクのワード (2 バイト) をトークンとして誤実行する。
                LEAX    -1,X
iif_true:
                LDA     ,X
                CMPA    #TOK_THEN       ; THEN
                BNE     iif_end         ; THEN 無し → 復帰 (後続を文実行 / 行末なら次行)
                LEAX    1,X             ; THEN を消費
                ; THEN 直後が行番号なら暗黙 GOTO (本体 iif_target、空き領域に配置)
                JMP     iif_target
iif_end:
                RTS

; LET (明示 LET / 暗黙代入の共通入口)
;   X = 変数名先頭。impl_eval_expr が U/VARTYP を破壊するため、変数値
;   アドレス(U) と型(VARTYP) を退避してから式評価し、復元して格納する。
impl_let:
                JSR     impl_get_var_ref
                ; 左辺アドレスは CPU スタックでなく let_tgt へ置く。
                ;   右辺の評価が未定義変数を作るとスカラ域が伸び、配列ブロックが
                ;   まるごと上へ動く。スタックに積んだアドレスは誰も直せないが、
                ;   let_tgt なら vars_make_room が移動量ぶん追従させられる。
                ;   追従しないと、確定済みの要素アドレスが移動前のアドレス
                ;   (= 配列エントリのヘッダ) を指したまま書き込み、配列の
                ;   次元数や大きさを壊す。
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
                ; ここで impl_fac_to_int を呼んではならない。
                ;   単精度(FACTYPE=1)の FAC1 に対する impl_fac_to_int は
                ;   float_to_int_trunc へ入り FAC1 を破壊する (終端変換規約)。
                ;   選択値の整数化は impl_on_select 冒頭が一度だけ行うので、
                ;   ここで先に変換すると impl_on_select が破壊済み FAC1 を
                ;   二重変換し、n が不定になる。
                ;   FAC1(選択値)はそのまま impl_on_select へ引き渡す。
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
                JSR     impl_find_line
                JMP     impl_jump_to_line
ion_done:
                RTS
ion_gosub:
                JSR     impl_on_select  ; D=選択行番号, X=ON文末 (復帰IP)
                BEQ     ion_done
                JMP     igo_tail        ; D = 選択行番号, X = ON 文末 (復帰 IP)

; FOR
impl_for:
                JSR     impl_get_var_ref
                ; 制御変数アドレスを for_var_ptr へ退避する。
                ;   INT(%) 変数: 値先頭 U に 16bit 整数 → 16bit 整数の経路。
                ;   単精度変数: 値は内部形式 4B。bit15 を立てて印を付け、NEXT/初期化は
                ;     内部形式で読み書きし float 加算/比較の実数 FOR へ分岐する。
                STU     for_var_ptr
                LDA     VARTYP
                CMPA    #VTYPE_INT
                ; 整数 (%) 制御変数の構築は for_setup_int (7t_arrays.inc の
                ; 空きプール) が担う (容量の都合)。
                LBEQ    for_setup_int
                ; 単精度: for_var_ptr の bit15 を立て、実数 FOR セットアップへ
                LDD     for_var_ptr
                ORA     #$80
                STD     for_var_ptr
                JMP     for_setup_sng

; NEXT
;   ループ継続時: X = ループ本体先頭 IP (pool_next_loop が CURIP を設定)。
;   ループ終了時: X = NEXT トークンの直後 (本入口の X)。ディスパッチャは
;     ハンドラ復帰時の X を次 IP とするため、終了経路で X を復元する。
;   NEXT の変数引数 (NEXT I / NEXT J,I) は本実装では読み飛ばす (本体は
;     最内ループのスタック先頭で判定するため変数名は無視して安全)。
in_name_end:
                PSHS    X               ; この時点の X (次のカンマ/文末) を退避
                JSR     pool_next_loop  ; NEXT 本体 (別区画)
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
                ; 1 変数ぶんの名前を読み飛ばす (':'/','/$00/空白 以外を消費)
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

; WEND

; READ
;   impl_read_data は DATA 走査のため X を破壊する (DATPTR を辿る)。
;   プログラム本文側のポインタ X は変数名直後を指したままにしておく必要が
;   あるため、read_data の前後で X を退避/復元する。
impl_read:
ird_lp:
                JSR     impl_get_var_ref
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

; DEFSTR/DEFINT/DEFSNG/DEFDBL の本体は空きプールへ配置する。
; 4 入口を別々に持つ必要がある一方、この区間には余裕がないため。

; TRON/TROFF
impl_tron:
                LDA     #1
                STA     trace_flag
                RTS
impl_troff:
                CLR     trace_flag
                RTS

; SWAP
;   本体は当区間に残量が無いため空きプールへ置く (参照はラベル経由のみで
;   位置依存なし)。

; PRINT
;   本体の置き場所は容量の都合で 7t_deffn.inc 末尾のプール。参照はラベル経由のみ。

; POKE アドレス, 値
;   書込先アドレスは一時的に U に退避し、本文ポインタ X は最後まで保持する。
;   (PULS X で X を書込先アドレスに上書きしたまま RTS すると、呼び出し元
;    ディスパッチャが書込先を次の実行 IP と誤認する)
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
; 本体 (引数解析・bare 検出・ML JSR・復帰) は exec_body ($9722 空き領域) に置く。
; ここを JMP 1 個に留めるのは、当区間に残量がほとんど無く、
; 本体を置くと区画からあふれるためである (参照はラベル経由のみで位置依存はない)。
                include "7t_using_num2a.inc"    ; PRINT USING 数値整形 2/4 (プール)
                ; --- 区画の区切り (容量管理のための区切り)。ここまでの空きを吸収して次の区画を切り出す。
;------------------------------------------------------------------------------
; iget_sel — GET@ / GET@A の形式判別と発行 (impl_get から末尾呼出で入る)。
;   書式指定子 G の有無が形式1/2 と形式3/4 を分ける。サブ CPU 側のキャラクタ矩形読出
;   ($06/$08) と図形矩形読出 ($1B/$1D) はどちらも「$D383=計数 / $D384〜=データ、
;   領域が尽きたら継続要求 $64」という同じ応答形式なので、受取本体 (iget_body) は
;   4 形式で共通にできる。
;   形式3/4 だけを備えて G の無い形 (= 形式1/2) を捨てると、参考書籍どおりに
;   書いた GET@ が配列へ一切書き込めなくなる。
;   (impl_get の区画にも空きプールにも残量が無いため本プールへ置く。参照はラベル
;    経由のみで位置依存はない)
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

; CLEAR [文字列領域サイズ][,最上位アドレス]
;   本体 (clear_body / ic_setbot) の置き場所は容量の都合で空きプール (7t_startup.inc)。
;   参照はラベル経由のみで位置依存はない。

;------------------------------------------------------------------------------
; clear_relocate_stack: CLEAR n,m の m に合わせてシステムスタックを移設する。
;   入力 D = m (BASIC が使う最上位アドレス, このアドレス以降は機械語ロード用に解放)。
;   現スタック上端 STACK_TOP から現 S までの使用中フレームを、上端が m となる
;   新領域へそのまま複写し、S を張り直す。フレームの相対配置は保たれるため、
;   復帰アドレス連鎖はそのまま機能する。
;   破壊: A,B,X,U。S は新値に差し替わる (本ルーチンの復帰アドレスも移設後の
;   スタックから取り出すため、移設には呼び出し元の復帰フレームも含める)。
;   (本体は 7t_strheap.inc の区画にある)
;------------------------------------------------------------------------------

                ; 区画の区切り $C605 (容量管理のための区切り)
;------------------------------------------------------------------------------
; 周期タイマ割込の所有 (7t_time.inc の所有規約の実体。当該区間に余白が無いため
; 本プールへ置く。参照はラベル経由のみで位置依存なし)。
;   控え fd02_shadow の bit7 = 「BASIC がハードの許可 ($FD02 bit2) を所有中」。
;   持っていない時は $FD02 へ 1 バイトも書かない。書けば、利用者の機械語が
;   周期割込みのために行った設定を壊す
;   ($FD02 は書込専用で読み戻せないため復元できない)。
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
;   戻してから所有を取る。委譲先が呼出元へ戻らずウォームスタートへ飛ぶと、控えの
;   許可が倒れたまま BASIC の実行が再開する。そこで許可を確かめずに所有だけ取ると、
;   ハードの刻みは入るのに tick ハンドラが ROM 側へ入らないため、演奏キューが
;   1 件も消費されず満杯待ちが明けない。BASIC が文を実行している時点で委譲は
;   終わっているので、ここで許可を戻すのが正しい状態である。
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
;   一時領域は「文の切れ目でまとめて返す」造りなので、値を使い終えた場所で
;   最上位の 1 個だけでも返しておかないと、1 つの文の中で作った一時が積み上がって
;   文字列領域を食い潰す。使い終わった時点で返すのが参考書籍の
;   FRE("") の勘定と合う。
;   返せるのは最上位 (末尾が TMPPTR に接している) の 1 個だけである。それより下に
;   在る一時は、外側の式がまだ抱えている可能性があるので触らない。
;   STRLEN=0 のときは「先頭 - 1」が TMPPTR と一致しても書き戻す値が同じなので
;   何も起きない (判定を別に置く必要はない)。
;   破壊: D。X/U は触らない。
;   (本片は空きプールに置く。参照はラベル経由で位置依存なし)
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

; CLS

; (clear_relocate_stack の本体の置き場所は容量の都合で 7t_strheap.inc (空きプール)。
;  参照はラベル経由のみで位置依存なし)

; --- 空きプール ---
;   LOCATE の本体は第 3 引数 ,S (カーソルスイッチ) を扱う impl_locate3
;   (7t_color_video.inc)。ジャンプ表 $A3 もそちらを指す。
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
;     出力  $D384 = 文字数, $D385〜 = 文字列 (16 byte 枠の未使用部は $00)
;   ステージング側では SUBCMD_P1 / SUBCMD_P2 / SUBCMD_P3 が同位置に対応する。
;   未使用部が $00 で埋まるので、文字列はそのまま 0 終端として扱える。
;
;   X はディスパッチャの続行位置 (復帰後 CURIP へ格納される) なので必ず
;      保存・復元する。C=1 で戻り、呼出元 impl_key は割当て処理をせず復帰する。
;   本ルーチンは単独プールに収まらないため $C614 / $C6BA / $EF45 / $D2CC の
;      4 つの空きプールへ分割配置している。分割点は JSR/JMP のみで位置依存なし。
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
                include "7t_arr_vncopy.inc"     ; 変数名の複写 (区画の都合でここへ)

; (COLOR の本体は impl_color_disp。ジャンプ表 $93 はそちらを指す)

; CONSOLE のコマンド表は本体を直接参照する。中継 JMP は不要。
impl_console    EQU     impl_console_ext

; PSET / PRESET
; 構文:
;   PSET   (水平位置, 垂直位置 [, [パレットコード] [, 機能]])
;   PRESET (水平位置, 垂直位置 [, [パレットコード] [, 機能]])
; パレットコードと機能は括弧の内側に書く。省略時はパレットコードが現在の
; 描画色 (PRESET は背景色)、機能が PSET となる。
; 引数列の解析は pool_gr_opts (7t_startup.inc の空きプール) が担う。
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
                ; CL/F は先に確定させる。以降で呼ぶ skip_sp_x は A に現在文字を
                ;   返す (= A を壊す) ため、後回しにするとパレットコードが化ける
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
;   (7t_arrays.inc の arr_alloc / vars_make_room から呼ぶ。参照はラベル経由の
;    みで位置依存なし)
;------------------------------------------------------------------------------
alloc_limit:
                TFR     S,D
                ADDD    #2-STK_RESERVE          ; 呼出時点の S − 最小の空き
                RTS
;------------------------------------------------------------------------------
; kl_init — KEY LIST の初期化断片。impl_key_list から JSR で呼ぶ。
;   (1) print_uint16 は out_vec 経由で出力する。SAVE が出力先をテープ側へ
;       向けたまま残すため、一覧表示のたびにコンソールへ向け直す。
;   (2) $2B の応答は SUBCMD_P3〜 の 15 byte ちょうどで、割当てが 15 文字だと
;       0 終端が無い。ステージングの直後 1 byte を 0 にしておくと、
;       impl_sub_send_cmd がステージング全体を共有 RAM と往復させる際も
;       この 0 が保たれる (サブ側はここを書かない) ので、以後の全キーで
;       0 終端が保証される。
;   戻り A = 1 (最初のキー番号)。
kl_init:
                JSR     out_vec_screen  ; 出力先を画面へ戻す (共通化)
                CLR     SUBCMD_P3+15    ; 15 文字割当て時の 0 終端 (保険)
                JMP     kl_init2
                ; 区画の区切り (容量管理のための区切り)
impl_preset:
                JSR     impl_parse_xy_cmd
                ; 消去 = 背景色 (CL=0 相当) で打点 → 全プレーンのビットが消える。
                ; パレットコード/機能を明記した場合はそちらが優先される (PSET と同文法)
                LDA     cur_bg_color
                BRA     ipset_opt
;------------------------------------------------------------------------------
; crs_fix_execsp: clear_relocate_stack の移設量だけ EXEC_SP をずらす。
;   移設量 = 新 MEMTOP - 旧 MEMTOP (= 新 S - 旧 S)。
;   (clear_relocate_stack から JSR。移設元区間には余白が無いため
;    本体は当プールへ置く。参照はラベル経由のみで位置依存なし)
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
;   ドットパターン形式の書式指定子は参考書籍では予約語ではないため、トークン化
;   されずに本文へ 1 文字のまま残る。これを数式として評価すると、
;     (1) 未定義変数として値 0 が渡る
;     (2) スカラ変数が新規に作られ、後続の配列ブロック全体が後方へ移動する
;   の 2 つが同時に起き、解析済みの配列アドレスの指す先がずれて隣接する配列まで
;   壊す。そこで書式指定子は式評価に回さず、ここで消費して値なし (0) を返す。
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
; MOTOR — BASIC の文としての MOTOR は cmd_jt から impl_stub へ振ってあり、
;   引数 (`MOTOR ON` / `MOTOR OFF`) を読み捨てる。読み残すと OFF
;   トークン ($D5) が文の位置に残って Syntax Error になるため。
