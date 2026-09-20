; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; DEF FN (使用者定義関数) — 参考書籍「DEF FN」準拠
;
;   書式:  DEF FN 名前 [(パラメータリスト)] = 式
;   意味:  `FN`＋名前 が関数名。パラメータは呼出時の実引数で置換される
;          「別扱いの変数」で、呼出前に定義されていなければならない。
;
; 実装概要:
;   - DEF 実行時 (idef_fn): FN 名前位置へのポインタを fn_def_tbl に登録するだけ。
;     以降のパラメータ/本体は登録せず、呼出時に同ポインタから再解析する。
;   - FN 呼出時 (epr_fn, 式プライマリから): 名前で定義スロットを引き当て、
;     実引数を評価 → パラメータ変数の旧値を退避 → 束縛 → 本体式を評価 →
;     パラメータ変数を旧値へ復元 → 結果 (FAC1) を返す。
;
; 入れ子/再帰呼出し:
;   パラメータ束縛の作業域 fn_call_argptr / fn_def_scan / fn_nparam は $03F8-
;   $03FC に連続配置した固定 RAM である。epr_fn はこの 5 バイトを入口でハード
;   ウェアスタックへ退避し、出口で戻す。よって本体式や実引数の中から別の FN を
;   呼んでも外側の呼出しの走査位置・束縛数は保存される。
;   (退避を欠くと内側の呼出しが fn_nparam を 0 に戻してしまい、外側がパラメータ
;    退避フレームをスタックから降ろさないまま RTS して復帰先を見失っていた。)
;
; 制限:
;   - 定義数は FN_NSLOTS 個まで。
;   - 入れ子の深さはハードウェアスタックの残量まで (1 段あたり 5 + 11×引数個)。
;   本領域は空きプールの余白へ配置する。参照はラベル経由のみ。
;==============================================================================

; fn_clear_tbl: DEF FN 定義表を全消去 (未定義=0)。コールドスタート/NEW から呼ぶ。
;   破壊: A なし / X, B。
fn_clear_tbl:
                LDX     #fn_def_tbl
                LDB     #FN_NSLOTS*FN_SLOTSZ
                JMP     mz_l            ; B バイトのゼロ詰め (多倍長ヘルパと共用)

;------------------------------------------------------------------------------
; idef_fn: DEF FN 定義の登録 (impl_def から分岐)。
;   入力: X = FN トークン ($CF) の位置。
;   処理: 名前位置を fn_def_tbl の空き/同名スロットへ格納し、DEF 文の残りを
;         ':'/行末まで読み飛ばして dispatcher へ復帰する。
;------------------------------------------------------------------------------
idef_fn:
                ; 直接モードでは定義できない (Illegal Direct 12。参考書籍の F-BASIC
                ; 参考書籍「直接モードで使えないステートメントを用いた」)。定義は
                ; 定義行の位置で保持するので、直接モードの行 (実行が終われば消える) を
                ; 指したままにできない。
                LDD     CURLIN
                CMPD    #$FFFF
                BNE     idf_ok
                LDB     #12             ; Illegal Direct
                JMP     impl_error_handler
idf_ok:
                LEAX    1,X             ; X = DEF FN 名前位置 (作業列内の def_ptr)
                PSHS    X               ; def_ptr 退避
                JSR     parse_var_name  ; VNPTR/VNLEN/VARTYP = FN 名前+型
                BSR     fn_find_or_alloc ; U = 格納先スロット (走査は他行を作業列へ載せ得る)
                ; スロットへ (行レコード, 行内オフセット) を格納する (E2)。
                ; def_ptr は作業列内の値なので、作業列の先頭からの差をオフセットとして
                ; 控える。変換は決定的なので、呼出時に同じ行を載せ直せば
                ; 先頭+オフセット が同じ名前位置になる。
                LDD     CURLINE_PTR
                STD     ,U              ; +0 行レコード先頭
                LDD     ,S              ; D = def_ptr
                SUBD    #EXECBUF
                STB     2,U             ; +2 行内オフセット (256 未満)
                JSR     exec_ensure     ; 表走査で他行が載った可能性 → 自行を復元
                PULS    X               ; X = def_ptr (名前位置)
                JMP     idef_skip       ; DEF 文の残りを ':'/行末まで読み飛ばす

;------------------------------------------------------------------------------
; fn_find_or_alloc: VNPTR/VNLEN/VARTYP の FN 名前に対応するスロットを返す。
;   既存同名スロットがあれば再利用、無ければ空きスロット、満杯なら先頭を上書き。
;   出力: U = &slot。破壊: A, B, X, fn_tmp_*。
;------------------------------------------------------------------------------
fn_find_or_alloc:
                BSR     fn_name_save
                BSR     fn_scan_tbl     ; pass1: 既存同名 (Z=1 → U=該当スロット)
                BEQ     ffa_ret
                ; pass2: 空きスロット
                LDU     #fn_def_tbl
                LDB     #FN_NSLOTS
ffa2:
                LDX     ,U
                BEQ     ffa_ret
                LEAU    FN_SLOTSZ,U
                DECB
                BNE     ffa2
                ; 満杯 → 先頭スロットを上書き
                LDU     #fn_def_tbl
ffa_ret:
                RTS

;------------------------------------------------------------------------------
; fn_name_save: 直前に解析した名前 (VNPTR/VNLEN/VARTYP) を照合用プローブへ複写する。
;   定義側と呼出側の名前は別々の行に在り、単一の実行用行バッファでは同時に
;   参照できない (E2)。そこで呼出側の名前を fn_probe_buf (16B) へ複写し、
;   fn_tmp_ptr はプローブを指す。プローブは名前解析〜表照合の間だけ生きる。
;   破壊: A, U。
;------------------------------------------------------------------------------
fn_name_save:
                PSHS    X,B
                LDX     VNPTR
                LDU     #fn_probe_buf
                LDB     VNLEN
                BEQ     fns_key         ; 長さ 0 は複写なし (防御)
fns_cp:
                LDA     ,X+
                STA     ,U+
                DECB
                BNE     fns_cp
fns_key:
                LDU     #fn_probe_buf
                STU     fn_tmp_ptr
                LDA     VNLEN
                ORA     VARTYP          ; 照合キー = 型|名前長 (1 バイトに畳む)
                STA     fn_tmp_ty
                PULS    B,X,PC

;------------------------------------------------------------------------------
; fn_scan_tbl: fn_def_tbl を走査し fn_tmp_* と同名の定義スロットを探す。
;   出力: Z=1 → 発見 (U = &slot, X = def_ptr) / Z=0 → 未発見。
;   破壊: A, B, X, U。
;------------------------------------------------------------------------------
fn_scan_tbl:
                LDU     #fn_def_tbl
                LDB     #FN_NSLOTS
fst_lp:
                LDX     ,U
                BEQ     fst_next
                BSR     fn_name_eq      ; Z=1 なら一致 (U/B/X 保存)
                BEQ     fst_ret
fst_next:
                LEAU    FN_SLOTSZ,U
                DECB
                BNE     fst_lp
                ANDCC   #$FB            ; Z=0 (未発見)
fst_ret:
                RTS

;------------------------------------------------------------------------------
; fn_name_eq: スロット U の指す FN 名前を解析し fn_tmp_* (プローブ) と比較。
;   スロットの行を作業列へ載せてから、行内オフセット位置の名前を解析する。
;   出力: Z=1 一致 (X = 名前位置) / Z=0 不一致。U, B は保存する (照合ループ用)。
;------------------------------------------------------------------------------
fn_name_eq:
                PSHS    U,B             ; 走査状態を保存
                ; X = スロットの名前位置 (行を作業列へ) (ここへ展開)
;------------------------------------------------------------------------------
; fn_slot_ptr — スロット U の (行レコード, オフセット) から名前位置 X を得る。
;   スロットの行を作業列へ載せる。
;   入力: U = &slot。出力: X = 名前位置。破壊: B, D。U/Y は保存。
;------------------------------------------------------------------------------
fn_slot_ptr:
                LDX     ,U              ; X = 行レコード
                JSR     exec_load_rec
                LDX     #EXECBUF
                LDB     2,U             ; 行内オフセット
                ABX
                PSHS    X               ; 解析入力を保存 (parse で前進するため)
                JSR     parse_var_name  ; VNPTR/VNLEN/VARTYP = 格納名前 (A,B,X,U 破壊)
                PULS    X               ; 格納名前位置を復元
                LDA     VNLEN
                ORA     VARTYP          ; 照合キー = 型|名前長
                CMPA    fn_tmp_ty
                BNE     fne_no
                ; 名前本体を比較する。U は入口で退避済なので破壊してよい。
                ; vn_cmp は「U の指すエントリの U+1 から」を読む規約。
                LDU     fn_tmp_ptr
                LEAU    -1,U
                JSR     vn_cmp
                BNE     fne_no
                PULS    B,U
                ORCC    #$04            ; Z=1 (一致)
                RTS
fne_no:
                PULS    B,U
                ANDCC   #$FB            ; Z=0 (不一致)
                RTS


;------------------------------------------------------------------------------
; fn_ens_def / fn_ens_ent — 定義行 / 呼出時に載っていた行を作業列へ復元する。
;   fn_def_rec / fn_entry_rec = 0 (載せるべき行なし = 直接モード等) は何もしない。
;   X/U/Y は保存。破壊: D。
;------------------------------------------------------------------------------
fn_ens_def:
                PSHS    X
                LDX     fn_def_rec
                BRA     fe_com
fn_ens_ent:
                PSHS    X
                LDX     fn_entry_rec
fe_com:
                BEQ     fe_ret
                JSR     exec_load_rec
fe_ret:
                PULS    X,PC

;------------------------------------------------------------------------------
; epr_fn: FN 呼出し (式プライマリから)。
;   入力: X = FN トークン ($CF) の位置。
;   出力: FAC1 = 関数値, X = 実引数の直後 (呼出しの直後)。
;         未定義 FN は Error 18 (Undefined user function)。
;------------------------------------------------------------------------------
epr_fn:
                JSR     chk_stack       ; FN の再帰 1 段ごとにシステムスタックの空きを確かめる
                ; --- 作業域 ($0771-$0779 の 9B) をスタックへ退避 (入れ子対応) ---
                ;   fn_call_argptr(2) / fn_def_scan(2) / fn_nparam(1) /
                ;   fn_def_rec(2) / fn_entry_rec(2) は連続配置。
                ;   高位アドレスから積み、efn_done で低位アドレスから戻す。
                LDU     #fn_entry_rec+2
                LDB     #9
efn_ctxsave:
                LDA     ,-U
                PSHS    A
                DECB
                BNE     efn_ctxsave
                ; 呼出時に作業列へ載っていた行を控える。呼出側 (実引数) の走査は
                ; この行の上で行われており、定義行を載せた後の復元先になる。
                ; EXECREC=0 (直接モード等) なら「復元すべき行なし」。
                LDD     EXECREC
                STD     fn_entry_rec
                LEAX    1,X             ; X = 呼出側 FN 名前
                JSR     parse_var_name  ; VNAME/VARTYP = 呼出 FN 名前+型
                STX     fn_call_argptr  ; 実引数走査開始位置 ('(' or 名前直後)
                JSR     fn_name_save    ; 名前をプローブへ複写 (照合キー込み)
                ; 定義スロットを名前で検索 (定義行を作業列へ載せ得る)
                BSR     fn_scan_tbl
                BEQ     efn_found
                ; 未定義 → Undefined user function
                ; Undefined user function (ここへ展開)
; iget_colors (GET@ ドットパターン形式の色指定組立) は本区間に残量が無いため
;   空きプール (7t_startup.inc) へ移設した。参照はラベル経由のみで位置依存なし。
err_c18:
                LDB     #18             ; Undefined user function
                JMP     errc_go
efn_found:
                ; U = &slot, X = def_ptr (定義行は作業列に載っている)
                LDD     ,U
                STD     fn_def_rec      ; 定義行 (以後の定義側 ensure 用)
                JSR     parse_var_name  ; DEF FN 名前を消費 → X が '(' or '=' へ
                STX     fn_def_scan
                CLR     fn_nparam       ; (extended。X は不変なので再読込は不要)
                JSR     skip_sp_lparen
                BNE    efn_body        ; パラメータ無し
                ; パラメータ有り: DEF 側 '(' と呼出側 '(' をスキップ
                JSR     fn_swap_to_call ; DEF 側を控えて呼出側へ (共通化)
                CMPA    #'('
                BNE     efn_e2
                LEAX    1,X
                STX     fn_call_argptr
efn_ploop:
                ; --- 実引数を評価 (この時点でパラメータ未束縛=大域値は健在) ---
                BSR     fn_ens_ent      ; 呼出側の行を作業列へ
                LDX     fn_call_argptr
                JSR     impl_eval_expr  ; FAC1 = 実引数
                STX     fn_call_argptr
                ; --- パラメータ変数を解決 (無ければ作成) ---
                BSR     fn_ens_def      ; 定義行を作業列へ
                LDX     fn_def_scan
                JSR     impl_get_var_ref ; U=値アドレス, VARTYP=型, VNPTR/VNLEN=名前, X=名前直後
                STX     fn_def_scan
                ; --- 旧値(8B)+名前(位置2B/長さ/型) を 12B フレームで退避 ---
                LEAX    8,U             ; U+8 から後方へ 8 バイト読む
                LDB     #8
efn_pushv:
                LDA     ,-X
                PSHS    A               ; value[7]..value[0] の順で積む (value[0] が上)
                DECB
                BNE     efn_pushv
                ; U は impl_get_var_ref が返した値アドレスで、直後の impl_assign_var が
                ;   使う。ここで LDU を挟むと束縛先を見失うので A だけで積む。
                LDA     VARTYP
                PSHS    A
                LDA     VNLEN
                PSHS    A
                LDA     VNPTR+1
                PSHS    A
                LDA     VNPTR
                PSHS    A               ; スタック上端 = 名前位置 上位 (12B フレーム)
                ; --- 実引数 (FAC1) をパラメータへ束縛 ---
                JSR     impl_assign_var ; U/VARTYP/FAC1 → 変数へ格納
                INC     fn_nparam
                ; --- 区切り: ',' なら次パラメータ, ')' なら終了 ---
                LDX     fn_def_scan
                JSR     skip_comma
                BCC     efn_pdone
                STX     fn_def_scan
                JSR     fn_ens_ent      ; 呼出側の行を作業列へ
                LDX     fn_call_argptr
                JSR     skip_comma
                BCC     efn_e2
                STX     fn_call_argptr
                BRA     efn_ploop
; errc_2 への近距離中継。DEF FN の書式を咎める 5 箇所は errc_2 まで短分岐が
;   届かないので、無条件分岐の直後 (落ち込みの無い位置) に 1 本置く。
efn_e2:
                JMP     errc_2
efn_pdone:
                CMPA    #')'
                BNE     efn_e2
                JSR     fn_swap_to_call ; DEF 側を控えて呼出側へ (共通化)
                CMPA    #')'
                BNE     efn_e2
                LEAX    1,X             ; 呼出側 ')' をスキップ (= 戻り X)
                STX     fn_call_argptr
efn_body:
                ; DEF 側は '=' (TOK_EQ) が続くはず → 本体式を評価 (定義行の上)
                JSR     fn_ens_def
                LDX     fn_def_scan
                JSR     skip_sp_x
                CMPA    #TOK_EQ
                BNE     efn_e2
                JSR     eval_expr_x1
                ; --- パラメータを旧値へ復元 (LIFO) ---
                LDA     fn_nparam
                BEQ     efn_done
efn_rlp:
                PULS    U
                STU     VNPTR
                PULS    A
                STA     VNLEN
                PULS    A
                STA     VARTYP
                LDX     #0
                JSR     gvr_find_scalar ; U = 値アドレス (再解決。表移動に耐える)
                LDA     VARTYP
                JSR     vn_valsize      ; B = 値サイズ (2/4/8/3), A/X 破壊
                LEAX    8,S             ; X = 8B 値フレームの直後 (破棄後の S)
efn_rcopy:
                PULS    A
                STA     ,U+             ; value[0],value[1],... を書き戻す
                DECB
                BNE     efn_rcopy
                TFR     X,S             ; 残りの退避バイトを一括破棄
                DEC     fn_nparam
                BNE     efn_rlp
efn_done:
                JSR     fn_ens_ent      ; 呼出時の行を作業列へ戻す (X は同値で有効)
                LDX     fn_call_argptr  ; 戻り X = 呼出しの直後 (退避を戻す前に取る)
                ; --- 作業域を復元 ---
                LDU     #fn_call_argptr
                LDB     #9
efn_ctxrest:
                PULS    A
                STA     ,U+
                DECB
                BNE     efn_ctxrest
                RTS
; (efn_synerr は errc_2 への 3 バイト中継でしかなかったので撤去し、
;  参照元を errc_2 へ直結した)

; (impl_end / impl_stop は容量の都合で本プールへ移設した)
; END
;   END も停止位置を記録する (表示はしない)。CONT すると END の次の文から続く。
;   入れ子の枠はシステムスタックに残したまま Ready へ戻る (CONT が続きを使う)。
;   END は開いているファイルを全て閉じる (参考書籍「END」)。STOP は閉じない。
impl_end:
                JSR     file_close_all  ; 開いているファイルを全て閉じる
                JSR     cont_save       ; X = END の直後 = 再開位置
                JMP     warm_start

; STOP
;   停止位置を記録したうえで "Break In <行番号>" を出す。直接モードの STOP は
;   記録も行番号表示も行わない (cont_save が CURLIN = $FFFF を見て抑止する)。
impl_stop:
                ; brk_pre = 現在行を閉じる改行 1 回 + err_pre (演奏中断 + 表示前の
                ; 行送り)。Break 表示の前だけ空行が 2 行になるのは、この「現在行を
                ; 閉じる」改行が行頭でも無条件に出るためである。
                JSR     brk_pre
                JSR     cont_save       ; X = STOP の直後 = 再開位置, D = 停止行
                PSHS    D               ; 表示中に壊れる停止行番号を退避
                JSR     print_inline    ; 改行は付けない。STOP の表示は誤りの
                FCC     "Break"         ;   表示と同じ尾部 (iet_d) を通り、
                FCB     0               ;   " In <行番号>" を付けて終わるため
                PULS    D
                JMP     iet_d           ; " In <行番号>" + 改行 + Ready

;==============================================================================
; PRINT@(x,y),コード[;コード...] — グラフィック座標指定の文字/漢字コード描画
;   参考書籍の PRINT@ (座標形式): グラフィック座標 (x,y) を基点に、
;   続く数値コード列 (';' 区切り) を 2 バイト JIS 漢字コードとして描画する。
;   各コードは漢字グリフ (16x16) を漢字 ROM から読み、当該座標へ描く。
;
;   本体は空きプールに配置する (impl_print 本体は動かさない)。ジャンプ表 $B9 は
;   impl_print_disp を指し、'@' 形式なら impl_print_at、それ以外は原 impl_print
;   へ委譲する (impl_color_disp と同一方式)。
;
;   漢字 ROM アクセス (FM-7): 漢字 ROM はメイン CPU バス上の I/O ポートに接続。
;     $FD20/$FD21 = アドレスレジスタ (上位/下位, 文字 16 行ぶんを連番でアクセス)
;     $FD22/$FD23 = データ (1 行 = 左 8 ドット/右 8 ドットの 2 バイト)
;   16 行 × 2 バイト = 32 バイトのモノクロ ビットイメージを読み出す。
;
;   グリフ描画はサブシステムのブロック描画コマンド $1C (Put Block 2, 単色) へ
;     渡す (共有 RAM 経由・impl_sub_send_cmd)。パラメータ配置:
;       SUBCMD_CMD=$1C, P1:P2=X1, P3:P4=Y1, P5:P6=X2(=X1+15), P7:P8=Y2(=Y1+15),
;       P9=描画色, P10=モード(0), P11.. =32 バイト グリフ
;   1 文字ごとに X 基点を 16 ドット進める (漢字桁送り)。
;==============================================================================
impl_print_disp:
                JSR     skip_sp_x       ; PRINT 直後の空白を許容 (標準 SAVE 形式)
                CMPA    #'@'            ; PRINT@ (座標形式)?
                BEQ     impl_print_at
                JMP     impl_print

impl_print_at:
                JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
                CMPA    #'('
                BNE    ipat_err        ; '(' 必須
                JSR     eval_to_addr_x1
                STD     tmpX16          ; px = X1
                JSR     skip_comma
                BCC    ipat_err
                JSR     eval_to_int    ; 式評価→整数化 (共通化)
                STD     tmpY16          ; py = Y1
                JSR     skip_sp_x
                CMPA    #')'
                BNE    ipat_err
                LEAX    1,X
                ; ')' の後は ',' に続くコード列 (省略時は何も描かず終了)
                JSR     skip_comma
                BCC     ipat_done
ipat_codeloop:
                JSR     eval_to_int    ; 式評価→整数化 (共通化)
                PSHS    X               ; 本文ポインタ退避 (ipat_draw_char が X を壊す)
                BSR     ipat_draw_char  ; (px,py) へ描画し px を桁送り
                PULS    X
                ; 次: ';' 継続 / それ以外は終了 (PRINT@ は末尾改行なし)
                JSR     skip_sp_x
                CMPA    #';'
                BNE     ipat_done
                JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
                LDA     ,X
                BEQ     ipat_done       ; 行末
                CMPA    #':'
                BEQ     ipat_done
                CMPA    #TOK_ELSE       ; ELSE
                BEQ     ipat_done
                BRA     ipat_codeloop
ipat_done:
                RTS
ipat_err:
                JMP     errc_2      ; Syntax Error (尾部併合)

;------------------------------------------------------------------------------
; ipat_draw_char — D = コード。(px,py) へ 16x16 漢字グリフを描画し px を進める。
;   D 上位=J1 (JIS 第1バイト), 下位=J2 (第2バイト)。
;   一時: tmpX16_2 (下位寄与/計算), tmpY16_2 (漢字 ROM 走査アドレス)。
;   px=tmpX16 / py=tmpY16 は impl_eval_expr/impl_sub_send_cmd を跨いで保持。
;------------------------------------------------------------------------------
ipat_draw_char:
                TSTA
                BEQ     ipdc_adv8       ; 1 バイトコード (暫定: 8 ドット桁送りのみ)
                BSR     kanji_jis2adr   ; D = 漢字 ROM のアドレス。キャリー = 範囲外
                BCS     ipdc_adv16      ; 範囲外 → 描画せず 16 ドット送り
                ; --- グリフ 32 バイトを SUBCMD_P11.. へ読込 ---
                LDY     #SUBCMD_P11+1   ; グリフ格納先 (P11=バイト数, P11+1.. =データ)
                JSR     kanji_read32
                ; --- サブシステム $1C (Put Block 2) を組み立て発行 ---
                LDA     #$1C
                JSR     sub_stage_x0    ; SH_ERR/SH_CNT=0, P1=tmpX16 (共通化)
                ADDD    #15
                STD     SUBCMD_P5       ; X2 = X1 + 15
                LDD     tmpY16          ; Y1 = py
                STD     SUBCMD_P3
                ADDD    #15
                STD     SUBCMD_P7       ; Y2 = Y1 + 15
                LDA     cur_fg_color
                STA     SUBCMD_P9       ; 描画色 = 現在の前景色
                CLR     SUBCMD_P10      ; モード 0 (単純書込)
                LDA     #32
                STA     SUBCMD_P11      ; データバイト数 = 32 (16x16 モノクロ)
                JSR     impl_sub_send_cmd
ipdc_adv16:
                LDD     tmpX16
                ADDD    #16
                STD     tmpX16          ; px を漢字 1 文字ぶん進める
                RTS
ipdc_adv8:
                LDD     tmpX16
                ADDD    #8
                STD     tmpX16
                RTS

;------------------------------------------------------------------------------
; kanji_jis2adr — D = JIS コード (A = 第 1 バイト、B = 第 2 バイト) を漢字 ROM の
;   アドレス (文字パターン 32 バイトの先頭。1 行 1 アドレス) に変換する。
;   キャリー = 1 のとき JIS コードの範囲外 (第 1 バイト < $21、または ≧ $50 の
;   第 2 水準は FM-7 の漢字 ROM に無い)。PRINT@ と BIOS の KANJIR が使う。
;   壊す: A, B, X。一時: tmpX16_2。
;     ten   = J2 - $20
;     base  = KHB[(J1>>4-2)*3 + (ten>>5)] + (J1 & $0F)*$200 + (ten & $1F)*$10
;------------------------------------------------------------------------------
kanji_jis2adr:
                CMPA    #$21
                BLO     kja_ret         ; 範囲外 (キャリー = 1 のまま)
                CMPA    #$50
                BHS     kja_bad         ; level-2 漢字域 (FM-7 非搭載)
                PSHS    A               ; [0,S] = J1
                SUBB    #$20            ; B = ten
                PSHS    B               ; [0,S] = ten, [1,S] = J1
                ; 下位寄与 = (ten & $1F)*$10
                ANDB    #$1F            ; B = low5
                CLRA
                LSLB
                ROLA
                LSLB
                ROLA
                LSLB
                ROLA
                LSLB
                ROLA                    ; D = low5 << 4
                STD     tmpX16_2
                ; + (J1 & $0F)*$200  (= (J1&$0F)*2 を上位バイトへ)
                LDA     1,S             ; J1
                ANDA    #$0F
                ASLA
                CLRB                    ; D = (J1&$0F)*$200
                ADDD    tmpX16_2
                STD     tmpX16_2        ; 下位寄与合計
                ; KHB 表引き: idx = (J1>>4 - 2)*3 + (ten>>5)
                LDA     1,S             ; J1
                LSRA
                LSRA
                LSRA
                LSRA                    ; A = J1>>4 (nibble 2..4)
                SUBA    #2              ; 0..2
                LDB     #3
                MUL                     ; D = nibble*3 (B に収まる, A=0)
                LDA     ,S              ; ten
                LSRA
                LSRA
                LSRA
                LSRA
                LSRA                    ; A = chunk (ten>>5, 0..2)
                PSHS    B               ; nibble*3 退避
                ADDA    ,S+            ; A = chunk + nibble*3
                TFR     A,B
                ASLB                    ; ワード添字 *2
                LDX     #ipat_khb
                LDD     B,X             ; D = KHB ワード
                ADDD    tmpX16_2        ; + 下位寄与 = base (桁上がりは無い = キャリー 0)
                LEAS    2,S             ; ten, J1 を破棄
kja_ret:
                RTS
kja_bad:
                ORCC    #$01            ; 範囲外 (第 2 水準は FM-7 の漢字 ROM に無い)
                RTS

;------------------------------------------------------------------------------
; kanji_read32 — D = 漢字 ROM のアドレス、Y = 格納先。文字パターン 32 バイト
;   (16 行 × 左右 2 バイト) を読んで Y から順に置く。PRINT@ と BIOS の KANJIR
;   (参考書籍) が使う。壊す: A, B, X, Y。
;------------------------------------------------------------------------------
kanji_read32:
                TFR     D,X
                LDB     #16
krd_lp:
                STX     $FD20           ; 漢字 ROM アドレス設定 (上位→$FD20/下位→$FD21)
                LDA     $FD22           ; 左 8 ドット
                STA     ,Y+
                LDA     $FD23           ; 右 8 ドット
                STA     ,Y+
                LEAX    1,X             ; 次の行 (アドレス +1)
                DECB
                BNE     krd_lp
                RTS

; KHB: JIS→漢字 ROM アドレスの上位ブロック基点表。動作観察に基づく。
;   添字 = (J1>>4 - 2)*3 + (ten>>5)。行 nibble 2 (JIS 記号/かな行) の
;   chunk2 のみ配置が不規則 ($1000)。
ipat_khb:
                FDB     $0000,$2000,$1000       ; J1 nibble 2 ($21-$2F)
                FDB     $4000,$6000,$8000       ; J1 nibble 3 ($30-$3F)
                FDB     $A000,$C000,$E000       ; J1 nibble 4 ($40-$4F)



; (CONSOLE の本体 impl_console_ext は、値域検査を足すと当区間に収まらないため
;  7t_startup.inc の空きプールへ移した。参照は impl_console からの
;  JMP だけで位置依存はない)

;==============================================================================
; pool_line_input — LINE INPUT [;] ["プロンプト";] 文字列変数
;   (impl_line から分岐。本プールに配置)
;
;   参考書籍「LINE INPUT 文」: キーボードから 1 行を、カンマや
;   引用符で区切らずそのまま 1 個の文字列変数へ読み込む。INPUT 文と違い
;   プロンプトの後に "? " を自動付加しない。
;   入力: X = 関数トークン $FF の位置 (LINE トークンの直後)
;   出力: X = 文の直後。読み取った行は INPBUF を指す文字列として変数へ格納。
;==============================================================================
pool_line_input:
                LDA     1,X                     ; 関数トークン第2バイト
                CMPA    #TOK2_INPUT
                BNE     pli_syn                 ; LINE + 別関数 → Syntax Error
                LEAX    2,X                     ; $FF $A6 を消費
                JSR     in_prompt               ; ';' 読み飛ばし + プロンプト表示 (共用)
                BCC     pli_read                ; プロンプト無し
                ; 区切り ';' / ',' を消費する ("? " は付けない)
                JSR     skip_sp_x
                CMPA    #';'
                BEQ     pli_sep
                CMPA    #','
                BNE     pli_read
pli_sep:
                LEAX    1,X
pli_read:
                PSHS    X                       ; 本文ポインタ退避
                JSR     impl_input_line         ; 1 行入力 → INPBUF (0 終端)
                PULS    X
                JSR     skip_sp_x
                JSR     impl_get_var_ref        ; U = 値アドレス, VARTYP, X = 名前直後
                LDA     VARTYP
                CMPA    #VTYPE_STR
                BNE     pli_syn                 ; 文字列変数以外は Syntax Error
                PSHS    X                       ; 本文ポインタ退避 (長さ算出で X 使用)
                LDX     #INPBUF
                CLRB
pli_len:
                LDA     ,X+
                BEQ     pli_len_e
                INCB
                BRA     pli_len
pli_len_e:
                STB     STRLEN
                LDD     #INPBUF
                STD     STRPTR                  ; 入力行そのものを指す (次の入力まで有効)
                LDA     #3
                STA     FACTYPE
                PULS    X
                JMP     impl_assign_var
pli_syn:
                JMP     errc_2              ; Syntax Error (尾部併合)

; pool_instr_arg1 / pool_fn_sgn は本プールの払底に伴い
; 7t_startup.inc の空きプールへ移設した。呼出は JSR/JMP の絶対アドレス
; なので配置先は自由。直前 (pli_syn) が JMP で終わるため落ち込み経路もない。

; pool_fn_rnd — RND の本体。7t_funcs.inc の当該区間は ZMB 予算が
;   足りないため、ここ (ZMB $C000 が吸収するプール) へ置く。
;   RND [(引数)] — 0 <= x < 1 の単精度乱数 (参考書籍):
;     省略 / 正 … 系列を 1 歩進めて次の値を返す (引数の大きさは使わない)
;     0          … 系列を進めず直前に返した値をもう一度返す
;     負         … 引数 (単精度化後) で系列を初期化し直して 1 歩進める
;                  (= 同じ負数からは常に同じ決定的な列)
;   生成規則は本実装独自のもので、32bit の排他的論理和と桁送りだけで回す
;   (Marsaglia が公表した xorshift の公知パラメータ 13/17/5):
;       x ^= x << 13 ;  x ^= x >> 17 ;  x ^= x << 5
;   状態 x は 32bit ($0745 の rnd_state 4B) で、0 以外から始めれば 0 に落ちない。
;   値は「指数 $7F・仮数 = x の上位 24bit・ガード = x の最下位 8bit・符号正」を
;   単精度へ正規化して作る (fadd の正規化・丸め尾部 fac_normalize を共用)。
;   これにより 24bit 全体が動く粒度になる。
impl_fn_rnd:
pool_fn_rnd:
                JSR     skip_sp_lparen
                BNE     ifr_bare        ; 括弧なし = 引数省略形 (後続は引数ではない)
                JSR     eval_p_num
                JSR     fx_ensure       ; 整数/倍精度も単精度へ揃える
                LDA     FAC1            ; 指数 0 = 値ゼロ
                BEQ     ifr_value       ; 引数 0 → 状態を進めず同じ値をもう一度
                LDA     FAC1+5          ; 符号バイト (bit7)
                BPL     ifr_step        ; 引数 正 → 系列を 1 歩進める
                LDD     FAC1+1          ; 引数 負 → 仮数 3B + 指数 1B を新しい
                STD     rnd_x           ;   状態にして 1 歩進める (系列の初期化)。
                LDA     FAC1+3          ;   指数は非 0 なので状態が全ビット 0 に
                STA     rnd_x+2         ;   ならない。
                LDA     FAC1
                STA     rnd_x+3
                BSR     rnd_step_x
                BRA     ifr_value
ifr_bare:
                STX     fn_xsave        ; 引数を取らないので本文位置はここ
ifr_step:
                BSR     rnd_step_seed
ifr_value:
                ; 指数 $7F・仮数 = 状態の上位 24bit・ガード = 最下位 8bit・符号正
                ; を単精度に正規化して返す (fadd の正規化・丸め尾部と共用。
                ; ガードのビットは正規化の左送りで仮数へ繰り入り、残りの
                ; 最上位ビットで丸める)
                LDD     rnd_state
                STD     fa_a            ; 仮数 上位 16bit
                LDD     rnd_state+2
                STD     fa_a+2          ; 仮数 下位 8bit + ガード 8bit
                LDA     #$7F            ; 値 = 状態/2^32 (0 <= x < 1) になる指数
                STA     fa_exp
                CLR     fa_sgn
                JMP     fac_normalize

; pool_iran_st — RANDOMIZE の格納実体。入力: D = 16bit 整数化済み引数。
;   引数を状態の上位 16bit に置き、下位 16bit は固定値にする (引数 0 でも状態が
;   全ビット 0 にならないようにするため)。同じ引数からは常に同じ列になる。
;   最後に 1 歩進めるところまでが RANDOMIZE の仕事。
;   rnd_step_seed は X を壊すので本文ポインタを守って戻る。
; pool_iran_ask — RANDOMIZE の式の省略時の問合せ (参考書籍
;   RANDOMIZE)。"Random Number Seed (-32768 to 32767)?" を表示して 1 行入力し、
;   その数値を種にする。入力: X = 本文ポインタ (保存して pool_iran_st へ渡す)。
pool_iran_ask:
                PSHS    X
                JSR     print_inline
                FCC     "Random Number Seed (-32768 to 32767)? "
                FCB     0
                JSR     impl_input_line ; INPBUF に 1 行 ($00 終端)
                LDX     #INPBUF
                JSR     impl_parse_num  ; FAC1 = 入力値 (空なら 0)
                JSR     impl_fac_to_int_round
                PULS    X
pool_iran_st:
                STD     >rnd_state
                LDD     #$2AF1          ; 下位 16bit の固定値 (状態の全 0 回避)
                STD     >rnd_state+2
                PSHS    X
                BSR     rnd_step_seed
                PULS    X,PC

; rnd_step_seed — 系列を 1 歩進める (pool_iran_st からも呼ばれる)。
;   状態を直接ページの作業域へ写してから rnd_step_x へ落ちる。
rnd_step_seed:
                LDD     >rnd_state
                STD     rnd_x
                LDD     >rnd_state+2
                STD     rnd_x+2
; rnd_step_x — 作業域 rnd_x に置いた 32bit 状態を 1 歩進め、rnd_state へ戻す。
rnd_step_x:
                LDB     #13             ; x ^= x << 13
                BSR     rnd_xshl
                ; x ^= x >> 17  (16bit の桁ずらし + 1bit の右送り)
                CLR     rnd_t
                CLR     rnd_t+1
                LDD     rnd_x
                STD     rnd_t+2
                LSR     rnd_t+2
                ROR     rnd_t+3
                BSR     rnd_eor
                LDB     #5              ; x ^= x << 5
                BSR     rnd_xshl
                LDD     rnd_x
                STD     >rnd_state
                LDD     rnd_x+2
                STD     >rnd_state+2
                RTS
; rnd_xshl — 一時 rnd_t へ x を写して B ビット左へ送り、x ^= t を行う。
;   破壊: A,X (そのまま rnd_eor へ落ち込む。B は送り回数として消費する)。
rnd_xshl:
                LDX     rnd_x           ; 写しは X で行う (B は送り回数を保つ)
                STX     rnd_t
                LDX     rnd_x+2
                STX     rnd_t+2
rnd_shl:
                ASL     rnd_t+3
                ROL     rnd_t+2
                ROL     rnd_t+1
                ROL     rnd_t
                DECB
                BNE     rnd_shl
                ; 落ち込んで rnd_eor
; rnd_eor — rnd_x ^= rnd_t (32bit)。破壊: A。
rnd_eor:
                LDA     rnd_x
                EORA    rnd_t
                STA     rnd_x
                LDA     rnd_x+1
                EORA    rnd_t+1
                STA     rnd_x+1
                LDA     rnd_x+2
                EORA    rnd_t+2
                STA     rnd_x+2
                LDA     rnd_x+3
                EORA    rnd_t+3
                STA     rnd_x+3
                RTS

; PAINT (7t_save_list.inc から移設。空白 skip 追加で同区間の 当区間の空きが
;   不足したため、本プールへ置く)
impl_paint:
                JSR     impl_parse_xy_cmd   ; (x,y) → tmpX16, tmpY16
                ; ',' ペイントカラー
                JSR     skip_sp_x           ; ')' と ',' の間の空白を許容 (A 破壊のため色設定前に)
                LDA     cur_fg_color        ; 塗色デフォルト=前景色 (A に温存)
                LDB     ,X                  ; カンマ判定は B で行い A を壊さない
                CMPB    #','
                BNE     ipaint_send
                LEAX    1,X
                ; 色引数は最近接丸め (動作観察: PAINT(x,y),6.7,5.7 は塗色 7 / 境界色 6)
                JSR     eval_to_int_15      ; 丸め + 値域検査 (0..15)
                TFR     B,A                 ; A = ペイントカラー
ipaint_send:
                PSHS    A                   ; ペイントカラー保存
                ; 境界色 (省略時=ペイントカラー)
                JSR     skip_sp_x           ; ',' 前の空白を許容 (A は直後に ,S から再ロード)
                LDA     ,S                  ; 境界色デフォルト=ペイントカラー (A に温存)
                LDB     ,X                  ; カンマ判定は B で行い A を壊さない
                CMPB    #','
                BNE     ipaint_bc1
                JSR     eval_to_int_15_x1
                TFR     B,A
ipaint_bc1:
                PSHS    A                   ; 境界色1
                ; subsys $18 PAINT
                ; $D383-$D384 = X (16bit)
                ; $D385-$D386 = Y (16bit)
                ; $D387 = PC (ペイントカラー)
                ; $D388 = Nc (境界色数)
                ; $D389 = BC1 (境界色1)
                LDA     #$18
                STA     SUBCMD_CMD
                LDD     tmpX16
                STD     SUBCMD_P1           ; $D383-$D384 = X
                LDD     tmpY16
                STD     SUBCMD_P3           ; $D385-$D386 = Y
                PULS    D                   ; A = 境界色1 (BC1、stack top)
                STB     SUBCMD_P5           ; $D387 = PC
                STA     SUBCMD_P7           ; $D389 = BC1
                LDA     #1
                STA     SUBCMD_P6           ; $D388 = Nc (ひとまず 1)
                ; 境界色は参考書籍どおり何個でも並べられる (PAINT (x,y),塗色,境界色…)。
                ;   ',' が続く限り $D389 以降へ積み、個数を $D388 へ書く。
                ;   個数の上限はサブシステム側の色リスト容量に合わせて 8。
                ;   ここを 1 個固定にしていると、境界として指定された残りの色を
                ;   塗りが通り抜けてしまい、囲みの外まで際限なく広がる。
ipaint_bcn:
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                CMPA    #','
                BNE     ipaint_go
                JSR     eval_to_int_15_x1
                LDU     #SUBCMD_P6
                LDA     ,U                  ; A = これまでの個数
                INCA
                CMPA    #8
                BLS     ipaint_bcs
                LDA     #9                  ; 9 個以上は 9 で頭打ちにして積まない。
                STA     ,U                  ;   受け手が個数超過として描かない値であり、
                BRA     ipaint_bcn          ;   9 個目以降を無視して描いてしまうのを防ぐ
ipaint_bcs:
                STA     ,U                  ; 個数 +1
                STB     A,U                 ; $D388+個数 = 次の境界色スロット
                BRA     ipaint_bcn
ipaint_go:
                ; ここでは追加の送出を行わない (復帰時 A=BC1 のままコマンド $04
                ;   (行入力) 等を誤発行すると main/sub 両 CPU が相互待ちで停止する)。
                JMP     impl_sub_send_cmd   ; 末尾呼出

                ; 原 PRINT (末尾委譲, RTS は原側で) (ここへ展開)
;------------------------------------------------------------------------------
; PRINT 本体 (7t_statements.inc から移設)
;   TAB(/SPC( 対応でコード増となり移設元の 当区間の空き (残 2B) を超える
;   ため、本プールへ移動。参照はラベル経由のみで
;   位置依存は無い。TAB(/SPC( トークンは式評価へ流さない (式プライマリが
;   未消費のまま 0 を返し、" 0" を無限に印字し続けるため)。
;------------------------------------------------------------------------------
impl_print:
                ; 表示文字列の先頭にフィールドを作る: SF オーダ ($11 + 現在の文字色
                ; $01E5 の値。参考書籍)。$01E5 を POKE で書き換えて
                ; 保護 (P) や反転 (R) を立てた文字列も、そのままの属性で表示される。
                JSR     ic12_send
ip_loop:
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     ip_nl
                CMPA    #':'
                BEQ     ip_nl
                CMPA    #TOK_ELSE       ; ELSE → PRINT 終了 (改行あり)
                BEQ     ip_nl
                CMPA    #';'
                BEQ     ip_semi
                CMPA    #','
                BEQ     ip_comma
                CMPA    #TOK_TAB        ; TAB( → 桁位置合わせ
                BEQ    ip_tab
                CMPA    #TOK_SPC        ; SPC( → 空白 n 個
                LBEQ    ip_spc
                CMPA    #TOK_USING      ; USING → 書式出力
                LBEQ    ip_using
                ; 対応する開き括弧を持たない ')' が項の位置に来た場合。式評価は
                ; ここで値を返せず読み取り位置も進められないので、そのまま項の
                ; 評価へ渡すと同じ 1 文字を読み直して出力が止まらなくなる。
                ; 参考書籍の式の規則にない綴りなので Syntax Error とする
                ; (それまでに出した項はすでに画面に出ている)。
                CMPA    #')'
                LBEQ    ion_err
                JSR     impl_eval_expr
                JSR     impl_print_val
                BRA     ip_loop

;------------------------------------------------------------------------------
; ip_at_end — 区切り (';' / ',') の直後が「文の終わり」かを Z で返す。
;   文の終わりは 行末 ($00) / ':' / ELSE トークンの 3 つ。IF 〜 THEN の節に
;   置かれた PRINT は ELSE で終わるので、ELSE も終わりとして数える。ここを
;   見落とすと `IF 1 THEN PRINT "*"; ELSE …` で ';' の改行抑止が効かず、
;   ip_loop 冒頭の ELSE 判定 (改行あり) に落ちて余分な改行が出る。
;   前置の空白は読み飛ばす (X を進める)。破壊: A。Z=1 なら文末。
;------------------------------------------------------------------------------
ip_at_end:
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     iae_ret         ; $00 = 行末
                CMPA    #':'
                BEQ     iae_ret
                CMPA    #TOK_ELSE       ; ELSE (CMPA の Z がそのまま答え)
iae_ret:
                RTS
ip_semi:
                LEAX    1,X
                ; セミコロンの直後が文末なら改行を抑止して終了
                BSR     ip_at_end
                BEQ     ip_done_nonl
                BRA     ip_loop
ip_comma:
                LEAX    1,X
                ; カンマは文末であっても次のゾーンへ送る (参考書籍が定める体裁)。
                ; カンマゾーン折返し: 既に 2 ゾーン目 (PRINT_COL>=15) に居る場合は
                ; 改行して次行の先頭ゾーンへ。さもなくば次ゾーン (col14) へ tab。
                LDA     PRINT_COL
                CMPA    #15
                BLO     ipc_tab
                JSR     disk_crlf       ; CR+LF (共通の改行出力)
                BRA     ipc_end
                ; 次の PRINT ゾーン (14桁単位) まで空白を出力
ipc_tab:
                LDA     PRINT_COL
ipc_tab_mod:
                CMPA    #14
                BLO     ipc_tab_have
                SUBA    #14
                BRA     ipc_tab_mod
ipc_tab_have:
                ; A = PRINT_COL mod 14。0 ならゾーン先頭 (tab 不要)
                TSTA
                BEQ     ipc_end
                JSR     put_space               ; 空白 1 個を出す
                BRA     ipc_tab
                ; (impl_putchar が PRINT_COL を進めるためループで収束)
ipc_end:
                ; ゾーン送りを終えた。直後が文末なら改行を抑止して終了
                BSR     ip_at_end
                BEQ     ip_done_nonl
                BRA     ip_loop
ip_nl:
                JSR     disk_crlf       ; CR+LF (共通の改行出力)
ip_done_nonl:
                RTS

; TAB(n): 桁 (n MOD 40) へ移動 (動作観察: TAB(45)→桁5, TAB(300)→桁20)。
;   現在桁が目標を超えていれば改行してから桁合わせ (動作観察)。
;   桁位置は 0..32767 (参考書籍 TAB)。n<0 は Illegal Function Call。
;   引数は最近接丸めで整数化し、32767 超は共通の整数変換で Overflow。
; >>FOLD fn_swap_to_call
;------------------------------------------------------------------------------
; fn_swap_to_call — DEF 側の走査位置を控えて呼出側の引数位置へ切り替え、空白を
;   読み飛ばして A に次の 1 文字を返す。DEF FN の引数解析で 2 箇所が同じ 4 命令を
;   並べていたので 1 本にまとめた (容量捻出)。
;   末尾は JMP なので呼出側から見た意味は元の並びと同じ。
;------------------------------------------------------------------------------
fn_swap_to_call:
                LEAX    1,X
                STX     fn_def_scan
                JSR     fn_ens_ent      ; 呼出側の行を作業列へ
                LDX     fn_call_argptr
                JMP     skip_sp_x
; <<FOLD fn_swap_to_call
; >>FOLD ip_tabspc_arg
;------------------------------------------------------------------------------
; ip_tabspc_arg — TAB( / SPC( の引数を読む定型。'(' 込みのトークンを消費し、
;   式を評価して最近接丸めで整数化し、')' を消費して D = n / TSTA 済で返す。
;   TAB と SPC が同じ 7 命令を並べていたので 1 本にまとめた (容量捻出)。
;   RTS は CC を変えないので、呼出側は元どおり TSTA の結果で分岐できる。
;------------------------------------------------------------------------------
ip_tabspc_arg:
                JSR     eval_expr_x1
                JSR     chk_num         ; 文字列を渡されたら Type Mismatch
                JSR     impl_fac_to_int_round ; D = n
                PSHS    D
                ; ')' を消費 (A 破壊) (ここへ展開)
; ')' を消費する。無ければ Syntax Error。
ip_tabspc_rpar:
                JSR     skip_sp_x
                CMPA    #')'
                BNE     ip_tabspc_syn
                LEAX    1,X
                PULS    D
                TSTA
                RTS
; <<FOLD ip_tabspc_arg
ip_tab:
                BSR     ip_tabspc_arg   ; D = n / TSTA 済 (共通化)
                BMI     ip_tabspc_ifc   ; n<0 → Illegal Function Call
ip_tab_mod40:
                CMPD    #40
                BLO     ip_tab_have
                SUBD    #40
                BRA     ip_tab_mod40
ip_tab_have:
                ; B = 目標桁 (0-39)。現在桁が目標を超過なら改行して桁 0 へ
                ; (impl_putchar は B/X/U を保存する)
                CMPB    PRINT_COL
                BHS     ip_tab_fill
                JSR     disk_crlf       ; CR+LF (PRINT_COL=0 にして次行へ)
ip_tab_fill:
                ; PRINT_COL < 目標桁 の間、空白を出す
                CMPB    PRINT_COL
                BLS     ip_tabspc_next  ; 目標 <= 現在桁 → 完了
                JSR     put_space               ; 空白 1 個を出す
                BRA     ip_tab_fill
ip_tabspc_next:
                JMP     ip_loop

; SPC(n): 空白を n 個出力 (動作観察: SPC(45)=45 個=行折返しは端末側)。
;   n<0 は Illegal Function Call (TAB と同族)。引数は最近接丸めで整数化 (動作観察)。
ip_spc:
                BSR     ip_tabspc_arg   ; D = n / TSTA 済 (共通化)
                ; SPC( は 0..255 のみ受理する (TAB( は 0..32767 で、ここが両者の
                ; 違い。動作観察: SPC(255) は空白 255 個、SPC(256) と SPC(-1) は誤り)。
                ; 上位バイトが 0 でなければ負値も 256 以上もまとめて範囲外。
                BNE     ip_tabspc_ifc
ip_spc_lp:
                ADDD    #0              ; D = 0 か (CMPD #0 と同じフラグで 1 バイト短い)
                BEQ     ip_tabspc_next
                SUBD    #1
                PSHS    D               ; put_space は A を ' ' に書き換えるので D (残り個数) を退避
                JSR     put_space               ; 空白 1 個を出す
                PULS    D
                BRA     ip_spc_lp

ip_tabspc_ifc:
                JMP     errc_5         ; Illegal Function Call (動作観察)
ip_tabspc_syn:
                JMP     errc_2      ; Syntax Error (尾部併合)

;==============================================================================
; iep_start_x — 実行開始行の決定 (RUN 行番号)
;   impl_exec_prog の初期化直後から JSR される。RUNLIN=$FFFF なら
;   先頭 (TXTTAB)。行番号指定があれば該当行レコードを探し X に返す。
;   RUNLIN は消費時に $FFFF へ戻す (CHAIN 等の後続 impl_exec_prog を汚さない)。
;   不存在行は Undefined line number (ERR 8。GOTO と同じ。動作観察に基づく)。
;   (本体は ZMB $C000 が吸収する本プールへ配置)
;==============================================================================
iep_start_x:
                ; INTERVAL 割込は RUN 毎に全状態を初期化する。前回実行の
                ; 有効状態・保留・登録行を持ち越すと、編集で消えた行へ分岐し得る。
                JSR     intv_reset      ; 破壊: B, X (直後に X は上書きされる)
                LDD     RUNLIN
                CMPD    #$FFFF
                BEQ     isx_top
                LDX     #$FFFF
                STX     RUNLIN          ; 消費 (以降の実行は先頭から)
                JSR     impl_find_line  ; D=行番号 → X=行レコード先頭
                BEQ     isx_undef
                RTS
isx_top:
                LDX     TXTTAB
                RTS
isx_undef:
                JMP     errc_8        ; Undefined line number

; impl_print_str — 0 終端文字列を putchar で出力 (7t_runtime.inc から移設。
;   当区間の空き不足のため本プールへ。参照はラベル経由のみで位置依存なし)
impl_print_str:
                LDA     ,X+
                BEQ     ips_done
                JSR     impl_putchar
                BRA     impl_print_str
ips_done:
                RTS

;--- print_inline — 呼出しの直後に置いた 0 終端の文言を出し、その次へ戻る -----
;   JSR print_inline / <文言> / FCB 0 / (続き) の形で使う。呼出しごとに
;   LDX #文言 を書かずに済む。破壊: A, X
print_inline:
                PULS    X               ; 戻りアドレス = 文言の先頭
                BSR     impl_print_str  ; X は 0 の次まで進む
                JMP     ,X              ; 文言の次から続ける

;--- print_str7 — 最終文字の bit7 で語末を示す文言を出力する -------------------
;   入力 X = 文言先頭。出力後 X は次の文言の先頭を指す。A,B 破壊。
;   区切りのバイトを持たない格納形式なので、1 文字ごとに bit7 を見て終りを判る。
print_str7:
                LDA     ,X+
                PSHS    A               ; 読んだままの 1 バイト (bit7 = 文言の終わり)
                ANDA    #$7F
                CMPA    #$20            ; $01-$1F は語彙番号 (印字可能文字より下)
                BHS     ps7_lit
                ; --- 語彙番号 → 語彙表 ps7_words の n 番目を出す ---
                PSHS    X               ; 文言の続きを退避
                LDX     #ps7_words
                DECA
                BEQ     ps7_go
ps7_sk:
                LDB     ,X+             ; 語末は bit7 が立っている
                BPL     ps7_sk
                DECA
                BNE     ps7_sk
ps7_go:
                BSR     print_str7      ; 語彙は文字だけなので自分自身で出せる
                PULS    X               ; 文言の続きへ戻る
                LDA     ,S              ; 読んだままの 1 バイトを見る
                BMI     ps7_end         ; bit7 = 文言の最後の語 → 空白は付けない
                LDA     #' '            ; 語の区切りの空白
ps7_lit:
                JSR     impl_putchar
ps7_end:
                PULS    A               ; PULS は CC を変えないので
                TSTA                    ;   語末の印 (bit7) を見るため N を作り直す
                BPL     print_str7
                RTS

;==============================================================================
; il_parse_range: LIST の行番号範囲引数を解析し il_start_ln / il_end_ln を設定
;   入力: X = LIST トークン直後
;   出力: il_start_ln = 開始行 (既定 0), il_end_ln = 終端行 (既定 $FFFF)
;         X = 引数の直後
;   参考書籍の LIST 構文:
;     LIST      全行     (start=0,  end=$FFFF)
;     LIST n    n 行のみ (start=n,  end=n)
;     LIST n-   n 以降   (start=n,  end=$FFFF)
;     LIST n-m  n〜m     (start=n,  end=m)
;     LIST -m   先頭〜m  (start=0,  end=m)
;   '-' は ASCII $2D / トークン化 $DA の双方を許容 (LOAD 後は $DA へ化ける)。
;   (7t_save_list.inc の impl_list から呼ぶ。本プールへ配置)
;==============================================================================
il_parse_range:
                CLRA
                CLRB
                STD     il_start_ln     ; 既定 = 0 (先頭から)
                LDD     #$FFFF
                STD     il_end_ln       ; 既定 = 無制限 (末尾まで)
                LDA     ,X
                CMPA    #'0'
                BLO     ipr_dash        ; 数字でない → 開始省略 (LIST / LIST -m)
                CMPA    #'9'
                BHI     ipr_dash
                JSR     pn2_uint            ; D = 行番号, X 前進
                STD     il_start_ln
                STD     il_end_ln       ; 暫定 = 単一行 (LIST n)。ダッシュがあれば上書き
ipr_dash:
                LDA     ,X
                CMPA    #'-'            ; ASCII '-'
                BEQ     ipr_have_dash
                CMPA    #','            ; 参考書籍は範囲の区切りに
                BEQ     ipr_have_dash   ;   ハイフォンとコンマの双方を認める
                CMPA    #TOK_MINUS      ; トークン化 '-'
                BNE     ipr_done        ; 区切り無し → end 現状維持
ipr_have_dash:
                LEAX    1,X             ; '-' を消費
                LDD     #$FFFF
                STD     il_end_ln       ; 'n-' → 以降。終端数字が続けば下で上書き
                LDA     ,X
                CMPA    #'0'
                BLO     ipr_done
                CMPA    #'9'
                BHI     ipr_done
                JSR     pn2_uint            ; D = 行番号, X 前進
                STD     il_end_ln
ipr_done:
                RTS

; (cont_clear は容量の都合で末尾の空き区画 (7t_core.s) へ移した。
;  参照はラベル経由のみで位置依存なし)

;==============================================================================
; エラーメッセージテーブル (参考書籍の英語文言)
;   区切りのバイトは置かず、各文言の最後の要素の bit7 を立てて終わりを示す。
;   要素は 2 種類ある。$20 以上は文字そのもの、$01-$1F は語彙表 ps7_words の
;   語番号で、繰り返し現れる綴り (File / Error / Without …) を 1 バイトへ畳む。
;   語番号の要素は「その語 + 区切りの空白 1 個」を表し、bit7 が立っている
;   (= 文言の最後) ときだけ空白を付けない。復号は print_str7 が行う。
;   並び順 = 文言索引 (1 起算) であり、
;   外部へ出すエラーコードとは別物である。
;   文言表は 50 件。
;     索引  1..26  = エラーコード  1..26
;     索引 27..41  = エラーコード 50..64  (コード − 23)
;     索引 42..50  = エラーコード 65..73  (コード − 23)
;   コード 0 / 27..49 (参考書籍で欠番) / 74 以上は索引 21
;   (Unprintable Error) へ丸める
;   (文言未定義のエラーは Unprintable Error とする旨は参考書籍 ERROR 文の項)。
;   配置: 本表は外部から呼ばれる入口ではなく、参照は 7t_runtime.inc の
;   `LDX #err_msg_tbl` 1 箇所のラベル経由だけである。アドレスを固定する必然性が
;   無いためアドレスアンカを置かず、本プールの末尾に置く。
;==============================================================================
;   語彙番号 (ps7_words の並び順。1 起算)。文言表と語彙表は本ファイル内で完結する。
W_FILE          EQU     $01     ; File
W_WITHOUT       EQU     $02     ; Without
W_STRING        EQU     $03     ; String
W_BAD           EQU     $04     ; Bad
W_OVERFLOW      EQU     $05     ; Overflow
W_DEVICE        EQU     $06     ; Device
W_ERROR         EQU     $07     ; Error
W_OUT           EQU     $08     ; Out
W_UNDEFINED     EQU     $09     ; Undefined
W_NUMBER        EQU     $0A     ; Number
W_NOT           EQU     $0B     ; Not
W_PROTECTED     EQU     $0C     ; Protected
W_DISK          EQU     $0D     ; Disk
W_ILLEGAL       EQU     $0E     ; Illegal
W_FUNCTION      EQU     $0F     ; Function
W_WITHOUTL      EQU     $10     ; without
W_ALREADY       EQU     $11     ; Already
W_OF            EQU     $12     ; Of
W_TOO           EQU     $13     ; Too
W_OPEN          EQU     $14     ; Open
W_DIRECT        EQU     $15     ; Direct
W_RESUME        EQU     $16     ; Resume
W_FULL          EQU     $17     ; Full
W_WHILE         EQU     $18     ; While
W_IN            EQU     $19     ; In
W_NEXT          EQU     $1A     ; Next
W_DATA          EQU     $1B     ; Data
W_WEND          EQU     $1C     ; Wend
W_FOR           EQU     $1D     ; For
err_msg_tbl:
                FCB     W_NEXT,W_WITHOUT,W_FOR+$80
                                        ;  1 Next Without For
                FCB     'S','y','n','t','a','x',' ',W_ERROR+$80
                                        ;  2 Syntax Error
                FCB     'R','e','t','u','r','n',' ',W_WITHOUT,'G','o','s','u','b'+$80
                                        ;  3 Return Without Gosub
                FCB     W_OUT,W_OF,W_DATA+$80
                                        ;  4 Out Of Data
                FCB     W_ILLEGAL,W_FUNCTION,'C','a','l','l'+$80
                                        ;  5 Illegal Function Call
                FCB     W_OVERFLOW+$80
                                        ;  6 Overflow
                FCB     W_OUT,W_OF,'M','e','m','o','r','y'+$80
                                        ;  7 Out Of Memory
msg_undef_ln:
                FCB     W_UNDEFINED,'L','i','n','e',' ',W_NUMBER+$80
                                        ;  8 Undefined Line Number
                FCB     'S','u','b','s','c','r','i','p','t',' ',W_OUT,W_OF,'R','a','n','g','e'+$80
                                        ;  9 Subscript Out Of Range
                FCB     'D','u','p','l','i','c','a','t','e',' ','D','e','f','i','n','i','t','i','o','n'+$80
                                        ; 10 Duplicate Definition
                FCB     'D','i','v','i','s','i','o','n',' ','B','y',' ','Z','e','r','o'+$80
                                        ; 11 Division By Zero
                FCB     W_ILLEGAL,W_DIRECT+$80
                                        ; 12 Illegal Direct
                FCB     'T','y','p','e',' ','M','i','s','m','a','t','c','h'+$80
                                        ; 13 Type Mismatch
                FCB     W_OUT,W_OF,W_STRING,'S','p','a','c','e'+$80
                                        ; 14 Out Of String Space
                FCB     W_STRING,W_TOO,'L','o','n','g'+$80
                                        ; 15 String Too Long
                FCB     W_STRING,'F','o','r','m','u','l','a',' ',W_TOO,'C','o','m','p','l','e','x'+$80
                                        ; 16 String Formula Too Complex
                FCB     'C','a','n',''','t',' ','C','o','n','t','i','n','u','e'+$80
                                        ; 17 Can't Continue
                FCB     W_UNDEFINED,'U','s','e','r',' ',W_FUNCTION+$80
                                        ; 18 Undefined User Function
                FCB     'N','O',' ',W_RESUME+$80
                                        ; 19 NO Resume
                FCB     W_RESUME,W_WITHOUT,W_ERROR+$80
                                        ; 20 Resume Without Error
                FCB     'U','n','p','r','i','n','t','a','b','l','e',' ',W_ERROR+$80
                                        ; 21 Unprintable Error
                FCB     'M','i','s','s','i','n','g',' ','O','p','e','r','a','n','d'+$80
                                        ; 22 Missing Operand
                FCB     W_FOR,W_WITHOUT,W_NEXT+$80
                                        ; 23 For Without Next
                FCB     W_WHILE,W_WITHOUTL,W_WEND+$80
                                        ; 24 While without Wend
                FCB     W_WEND,W_WITHOUTL,W_WHILE+$80
                                        ; 25 Wend without While
                FCB     'B','u','b','b','l','e',' ',W_FULL+$80
                                        ; 26 Bubble Full
                FCB     W_BAD,W_FILE,W_NUMBER+$80
                                        ; 27 Bad File Number
                FCB     W_BAD,W_FILE,'M','o','d','e'+$80
                                        ; 28 Bad File Mode
                FCB     W_FILE,W_ALREADY,W_OPEN+$80
                                        ; 29 File Already Open
                FCB     W_DEVICE,'I','/','O',' ',W_ERROR+$80
                                        ; 30 Device I/O Error
                FCB     'I','n','p','u','t',' ','P','a','s','t',' ','E','n','d'+$80
                                        ; 31 Input Past End
                FCB     W_BAD,W_FILE,'D','e','s','c','r','i','p','t','o','r'+$80
                                        ; 32 Bad File Descriptor
                FCB     W_DIRECT,'S','t','a','t','e','m','e','n','t',' ',W_IN,W_FILE+$80
                                        ; 33 Direct Statement In File
                FCB     W_FILE,W_NOT,W_OPEN+$80
                                        ; 34 File Not Open
                FCB     W_BAD,W_DATA,W_IN,W_FILE+$80
                                        ; 35 Bad Data In File
                FCB     W_DEVICE,W_IN,'U','s','e'+$80
                                        ; 36 Device In Use
                FCB     W_DEVICE,'U','n','a','v','a','i','l','a','b','l','e'+$80
                                        ; 37 Device Unavailable
                FCB     'B','u','f','f','e','r',' ',W_OVERFLOW+$80
                                        ; 38 Buffer Overflow
                FCB     W_PROTECTED,'P','r','o','g','r','a','m'+$80
                                        ; 39 Protected Program
                FCB     W_FILE,W_NOT,'F','o','u','n','d'+$80
                                        ; 40 File Not Found
                FCB     W_FILE,W_ALREADY,'E','x','i','s','t','s'+$80
                                        ; 41 File Already Exists
                FCB     'D','i','r','e','c','t','o','r','y',' ',W_FULL+$80
                                        ; 42 Directory Full
                FCB     W_TOO,'M','a','n','y',' ',W_OPEN,W_DISK,'F','i','l','e','s'+$80
                                        ; 43 Too Many Open Disk Files
                FCB     W_DISK,W_FULL+$80
                                        ; 44 Disk Full
                FCB     'F','i','e','l','d',' ',W_OVERFLOW+$80
                                        ; 45 Field Overflow
                FCB     W_STRING,W_NOT,'F','i','e','l','d','e','d'+$80
                                        ; 46 String Not Fielded
                FCB     W_BAD,'R','e','c','o','r','d',' ',W_NUMBER+$80
                                        ; 47 Bad Record Number
                FCB     W_BAD,W_FILE,'S','t','r','u','c','t','u','r','e'+$80
                                        ; 48 Bad File Structure
                FCB     'D','r','i','v','e',' ',W_NOT,'R','e','a','d','y'+$80
                                        ; 49 Drive Not Ready
                FCB     W_DISK,'W','r','i','t','e',' ',W_PROTECTED+$80
                                        ; 50 Disk Write Protected

;------------------------------------------------------------------------------
; ps7_words — 文言表の語彙表 (print_str7 が語彙番号 $01-$1F で引く)。
;   1 語 1 件、語末の 1 文字だけ bit7 を立てて区切る (文言表と同じ区切り方)。
;   並び順がそのまま語彙番号 (1 起算) になる。
;------------------------------------------------------------------------------
ps7_words:
                FCB     'F','i','l','e'+$80                         ; $01 File
                FCB     'W','i','t','h','o','u','t'+$80             ; $02 Without
                FCB     'S','t','r','i','n','g'+$80                 ; $03 String
                FCB     'B','a','d'+$80                             ; $04 Bad
                FCB     'O','v','e','r','f','l','o','w'+$80         ; $05 Overflow
                FCB     'D','e','v','i','c','e'+$80                 ; $06 Device
                FCB     'E','r','r','o','r'+$80                     ; $07 Error
                FCB     'O','u','t'+$80                             ; $08 Out
                FCB     'U','n','d','e','f','i','n','e','d'+$80     ; $09 Undefined
                FCB     'N','u','m','b','e','r'+$80                 ; $0A Number
                FCB     'N','o','t'+$80                             ; $0B Not
                FCB     'P','r','o','t','e','c','t','e','d'+$80     ; $0C Protected
                FCB     'D','i','s','k'+$80                         ; $0D Disk
                FCB     'I','l','l','e','g','a','l'+$80             ; $0E Illegal
                FCB     'F','u','n','c','t','i','o','n'+$80         ; $0F Function
                FCB     'w','i','t','h','o','u','t'+$80             ; $10 without
                FCB     'A','l','r','e','a','d','y'+$80             ; $11 Already
                FCB     'O','f'+$80                                 ; $12 Of
                FCB     'T','o','o'+$80                             ; $13 Too
                FCB     'O','p','e','n'+$80                         ; $14 Open
                FCB     'D','i','r','e','c','t'+$80                 ; $15 Direct
                FCB     'R','e','s','u','m','e'+$80                 ; $16 Resume
                FCB     'F','u','l','l'+$80                         ; $17 Full
                FCB     'W','h','i','l','e'+$80                     ; $18 While
                FCB     'I','n'+$80                                 ; $19 In
                FCB     'N','e','x','t'+$80                         ; $1A Next
                FCB     'D','a','t','a'+$80                         ; $1B Data
                FCB     'W','e','n','d'+$80                         ; $1C Wend
                FCB     'F','o','r'+$80                             ; $1D For

err_msg_tbl_end:

; CIRCLE 頂点角の sin 定数表 後半 (j=9..15)。自前 sin コア
;   (fx_sin_core) の実出力ビット列 (6B 展開形: 指数/仮数 32bit/符号)。
;   前半 c_sjtab9 (7t_funcs.inc) と分置して容量を均衡させる。
c_sjtab7:
                FCB     $7F,$C5,$E4,$03,$58,$00 ; j=9
                FCB     $7F,$D4,$DB,$31,$49,$00 ; j=10
                FCB     $7F,$E1,$C5,$97,$8B,$00 ; j=11
                FCB     $7F,$EC,$83,$5E,$7A,$00 ; j=12
                FCB     $7F,$F4,$FA,$0A,$B6,$00 ; j=13
                FCB     $7F,$FB,$14,$BE,$80,$00 ; j=14
                FCB     $7F,$FE,$C4,$6D,$1F,$00 ; j=15

;------------------------------------------------------------------------------
; ieh_tail — エラー表示の後半。impl_error_handler から JMP で入る。
;   エラー名を出したあと、プログラム実行中なら " In <発生行番号>" を付ける。
;   直接モードでは CURLIN = $FFFF なので行番号を付けない
;   (参考書籍の表示例 "Overflow In 10" / "Duplicate Definition In 50"、
;    および STOP の "Break In nnnnn" と同じ体裁)。
;   本体は impl_error_handler と同じ区間 (空きプール) に置く。誤り送出を
;     共通スタブ (errc_5 / errc_8 / errc_2) にまとめて当区間に余裕を作ってある。
;------------------------------------------------------------------------------
ieh_tail:
                ; 捕捉されずに止まった誤りの後は入れ子の枠を捨て、システムスタックを
                ; 上端 (文字領域の下限) へ戻す。参考書籍が CONT を
                ; 認めるのは BREAK / STOP / END の後だけなので、副手続き / 繰り返し /
                ; WHILE の枠はもう要らない。占有したままにすると、記憶を使い切って
                ; 止まった直後の直接モードの行が置けなくなる。
                ; (STOP / BREAK は iet_d から入るのでここを通らない = 枠は残る)
                LDS     STRBOT
                STS     EXEC_SP
                ; 捕捉されずに停止したエラーは、そのエラーが起きた文の先頭を
                ; 再開位置として記録する。CONT はその文をやり直す。
                LDX     CURIP
                JSR     cont_save
                ; 行番号は ERRLIN (= 入口で CURLIN から写した発生行) を見る。
                ; 通常経路では CURLIN と同値だが、捕捉済みエラーの再表示
                ; (ieh_report 経由) では現在行が処理ルーチン側へ進んでいるため、
                ; 発生行を保つ ERRLIN を出典にする。
                LDD     ERRLIN
                ; iet_d — 行番号を D で受け取る入口。STOP はここへ入って
                ;   "Break In <停止行>" を出す。ERRLIN を汚さないので ERL / ERR は
                ;   STOP では変化しない。D = $FFFF なら行番号を付けない。
iet_d:
                CMPD    #$FFFF          ; $FFFF (直接モード) なら行番号なし
                BEQ     iet_eol
                PSHS    D               ; 表示中に壊れる D (行番号) を退避
                JSR     print_inline
                FCC     " In "          ; 誤りの名前と発生行番号の区切り
                FCB     0
                PULS    D
                JSR     print_uint16    ; 発生行番号を 10 進で出力
iet_eol:
                ; 末尾に改行は付けない。次に出る "Ready" が先頭に改行を持ち、
                ; それが本表示行の終端を兼ねる。ここで改行するとエラー表示と
                ; Ready の間に余分な空行が入る。
                JMP     warm_start

;------------------------------------------------------------------------------
; CIRCLE の塗りつぶし (第 7 引数 F) —
;   (7t_save_list.inc の CIRCLE 実装から呼ぶ。本プールに
;    配置。参照はラベル経由のみで位置依存なし)
;
; 参考書籍の CIRCLE 第 7 引数 F は「円弧の内部を塗る」指定である。塗りは輪郭を
; 別に引かず、64 頂点多角形の各辺を画素単位で歩き、通過画素ごとに
; 「中心 → その画素」の線分 (subsys $15) を 1 本ずつ発行して面を埋める。
; 端点は始点・終点とも発行するので、頂点は隣接する 2 辺で重複して送られる。
;
; 辺の内挿規則 (動作観察で確定):
;   辺を覆う画素数を N = max(|dx|,|dy|) + 1 とし、X 軸と Y 軸それぞれに
;   独立した加算器を回す。各軸の増分はその軸が覆う画素数 |d|+1、法は N、
;   初期値は X 軸が |dx|・Y 軸が 0。1 画素進むごとに 誤差 += 増分 し、
;   誤差 >= N になった軸だけを 1 画素動かして 誤差 -= N する。
;   主軸 (差の大きい側) は増分が N に等しいので毎回必ず動き、従軸だけが
;   間引かれる。初期値を X 軸だけ |dx| にしてあるのが肝で、これが
;   「主軸が横のときと縦のときで従軸の折れる位置が半画素ずれる」動作観察した挙動を
;   そのまま再現する (両軸を 0 から始めると横主軸の辺が 1 画素ずれる)。
;   差分 (|dx|,|dy|) ではなく画素数 (|dx|+1,|dy|+1) を刻む点も動作観察どおりで、
;   差分で刻むと半径 40 以上や縦横比の大きい楕円で折れ位置がずれる。
;------------------------------------------------------------------------------

; CIRCLE 第 7 引数 (F/N) を解析し、描画の型を決める。
;   F あり: 全線分の始点を中心へ固定し、頂点の退避先を走査元 CFL_AX にする。
;   省略/N: 隣接頂点どうしを結ぶ (退避先 = 線分の始点欄)。
;   第 8 引数 (機能) は受理して読み飛ばす。
;   入出力: X = 本文ポインタ。壊す: A,B,U
; DEFSTR / DEFINT / DEFSNG / DEFDBL — 既定型表の更新
;   トークン化後の構文 (A-Z または A,B) を処理する。型番号は
;   SNG=0 / INT=1 / DBL=2 / STR=3。
impl_defstr:
                LDB     #3
                BRA     idf_go
impl_defint:
                LDB     #1
                BRA     idf_go
impl_defsng:
                CLRB
                BRA     idf_go
impl_defdbl:
                LDB     #2
idf_go:
                PSHS    B
                JSR     skip_sp_x
idf_item:
                ; A = 範囲先頭の添字, U = 表の該当欄 (ここへ展開)
;------------------------------------------------------------------------------
; idf_chku — DEF 型宣言文の「文字の範囲」の先頭 1 字を検査し、既定型表の欄を指す。
;   入力: A = 英字。出力: A = 添字 (0-25), U = DEFTYPE の該当欄。
;   (呼出元 impl_defint 等の区画に残量が無いため本プールへ置く。参照はラベル
;    経由のみで位置依存はない)
;------------------------------------------------------------------------------
idf_chku:
                JSR     chk_alpha
                LDU     #DEFTYPE
                LEAU    A,U
                PSHS    A                       ; 範囲先頭の添字
                LEAX    1,X
                LDA     ,X
                LDB     #1                      ; 単文字なら 1 要素
                CMPA    #TOK_MINUS
                BNE     idf_one
idf_range:
                LDA     1,X
                LEAX    2,X
                JSR     chk_alpha                 ; 範囲末尾の添字
                SUBA    ,S                      ; - 範囲先頭の添字
                ; 参考書籍は「最初の英字は後の英字よりアルファベット順で上位」と
                ; 定めている。逆順の綴りは受け付けない (表の外へ書かない)。
                LBCS    errc_2                  ; Syntax Error
                INCA                            ; 要素数 = 末尾-先頭+1
                TFR     A,B
idf_one:
                LEAS    1,S
idf_fill:
                LDA     ,S                      ; 宣言する型
idf_flp:
                STA     ,U+
                DECB
                BNE     idf_flp
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     idf_done
                LEAX    1,X
                LDA     ,X
                BRA     idf_item
idf_done:
                PULS    B,PC

; deftype_reset — 既定型表 (DEFTYPE, 26B) を全て SNG(=0) に戻す。
;   全変数を消去する場面 (CLEAR / RUN / NEW / 行入力 / コールドスタート) から
;   vars_clear_all 経由で呼ぶ。X を保つのが呼出規約 (CLEAR は本文ポインタを
;   X に載せたまま呼ぶ)。破壊: B。
deftype_reset:
                PSHS    X
                LDX     #DEFTYPE
                LDB     #26
dtr_lp:
                CLR     ,X+
                DECB
                BNE     dtr_lp
                PULS    X,PC


cir_fill_arg:
                LDU     #SUBCMD_P3          ; 既定 = 輪郭 (前頂点を線分の始点へ)
                JSR     skip_comma
                BCC     cfg_set
                JSR     skip_sp_x           ; ',' と F/N の間の空白を許容
                CMPA    #'F'
                BNE     cfg_set             ; 省略・N・その他は塗らない
                LEAX    1,X                 ; 'F' を消費
                JSR     subcmd_xy1
                LDU     #CFL_AX             ; 前頂点は走査元として退避する
cfg_set:
                STU     cfl_dst
                RTS

; 頂点 A (CFL_AX/CFL_AY) から頂点 B (SUBCMD_P7/P9) までの辺を画素単位で歩き、
; 通過画素ごとに「中心 → 画素」の線分を送る。終端画素 (= B) は呼出元の送出が
; 担うので、ここでは画素 0..N-2 の N-1 本を送り、復帰時に SUBCMD_P7/P9 を
; B へ戻しておく。壊す: A,B,X,U
cir_fill_edge:
                ; --- Y 軸: 誤差 0 から ---
                LDD     SUBCMD_P9
                SUBD    CFL_AY
                BSR     cfl_abs             ; D = |dy|+1, X = 歩幅
                STX     CFL_SY
                CLR     CFL_EY              ; Y 軸の初期誤差は 0
                CLR     CFL_EY+1
                STD     CFL_IY
                STD     CFL_N               ; 画素数の候補
                ; --- X 軸: 誤差 |dx| から ---
                LDD     SUBCMD_P7
                SUBD    CFL_AX
                BSR     cfl_abs             ; D = |dx|+1, X = 歩幅, CFL_EX = |dx|
                STX     CFL_SX              ; X 軸の初期誤差 |dx| は cfl_abs が入れる
                STD     CFL_IX              ;   (Y 軸の 0 との非対称がこの規則の肝)
                CMPD    CFL_N
                BLS     cfe_n
                STD     CFL_N               ; N = max(|dx|,|dy|) + 1
cfe_n:
                LDX     CFL_N
                LEAX    -1,X                ; 送る本数 = N-1 (終端は呼出元が送る)
                BEQ     cfe_ret             ; 同一画素の辺は送るものが無い
                STX     CFL_I
                LDD     CFL_AX              ; 走行開始点 = 頂点 A
                STD     SUBCMD_P7
                LDD     CFL_AY
                STD     SUBCMD_P9
cfe_lp:
                JSR     impl_sub_send_draw  ; 中心 → 現画素 (軽量送出)
                BSR     cfl_adv             ; 次の画素へ
                LDX     CFL_I
                LEAX    -1,X
                STX     CFL_I
                BNE     cfe_lp
cfe_ret:
                RTS

; D = 座標差 → D = |差|+1 (その軸が覆う画素数)、X = 歩幅 (負なら -1、他は +1)。
; 併せて CFL_EX へ |差| を置く。呼出しは Y 軸 → X 軸の順なので、最後に残るのは
; X 軸の |dx| = X 軸加算器の初期誤差になる。
cfl_abs:
                LDX     #1
                TSTA
                BPL     cab_r
                LDX     #-1
                NEGA                        ; D = -D (16bit)
                NEGB
                SBCA    #0
cab_r:
                STD     CFL_EX
                ADDD    #1
                RTS

; 現画素を 1 つ進める。X 軸ブロック → Y 軸ブロックの順に、
;   誤差 += 増分 → 誤差 >= N なら座標 += 歩幅、誤差 -= N
; を適用する。座標は SUBCMD_P7 (X) / SUBCMD_P9 (Y) の 2 バイト間隔、
; ブロックは 6 バイト間隔で並ぶので、両方を一定歩幅で送りながら 2 周する。
cfl_adv:
                LDX     #CFL_BLK
                LDU     #SUBCMD_P7
                BSR     cfa_one             ; X 軸
                LEAX    6,X                 ; 次のブロックと座標へ移り、
                LEAU    2,U                 ;   落ち込みで Y 軸を処理する
cfa_one:
                LDD     4,X                 ; 誤差
                ADDD    2,X                 ; += 増分
                SUBD    CFL_N
                BCC     cfa_st              ; 誤差 >= N → 1 画素進める
                ADDD    CFL_N               ; 届かない → 引いた分を戻す
                STD     4,X
                RTS
cfa_st:
                STD     4,X                 ; 誤差 -= N
                LDD     ,U
                ADDD    ,X                  ; 座標 += 歩幅
                STD     ,U
                RTS

;==============================================================================
; kw_idx / fn_idx — 予約語表の先頭字索引 (各 A-Z 26 欄 × 2 バイト)
;   行変換器の照合開始位置を語頭の英字で群へ絞る (照合コストの主対策)。
;   kw_idx は命令語表 kw_tbl、fn_idx は関数語表 kw_fn_tbl に対応する。
;   各欄の意味 (変位の詰込み形式) は scripts/gen_7t_keywords.py の kwidx_rows を
;   参照。予約語表と同期生成する (手書き禁止。表の並びが変わると値が変わる)。
;   本プールへ配置 (参照はラベル経由のみで位置依存なし)。
;   索引引きの本体 (tmk_idx) は 7t_expr.inc。kw_idx より fn_idx が後方に
;   あることに tmk_idx の呼出元判定 (CMPX #fn_idx) が依存する (並びを保つこと)。
;==============================================================================
kw_idx:
                FCB     $09,$09                         ; 'A' → 変位   9 ($89 AUTO)
                FCB     $10,$0B                         ; 'B' → 変位  16 ($8B BUBINI)
                FCB     $22,$0F                         ; 'C' → 変位  34 ($8F CONNECT)
                FCB     $4F,$18                         ; 'D' → 変位  79 ($98 DEFDBL)
                FCB     $77,$20                         ; 'E' → 変位 119 ($A0 ERROR)
                FCB     $94,$28                         ; 'F' → 変位 148 ($A8 FILES)
                FCB     $9E,$2B                         ; 'G' → 変位 158 ($AB GCURSOR)
                FCB     $AA,$2E                         ; 'H' → 変位 170 ($AE HARDC)
                FCB     $AF,$2F                         ; 'I' → 変位 175 ($AF INTERVAL)
                FCB     $BC,$32                         ; 'J' → 変位 188 ($B2 KILL)
                FCB     $BC,$32                         ; 'K' → 変位 188 ($B2 KILL)
                FCB     $C3,$34                         ; 'L' → 変位 195 ($B4 LOCATE)
                FCB     $D8,$39                         ; 'M' → 変位 216 ($B9 MERGE)
                FCB     $E8,$3D                         ; 'N' → 変位 232 ($BD NEXT)
                FCB     $F2,$40                         ; 'O' → 変位 242 ($C0 OPEN)
                FCB     $FD,$44                         ; 'P' → 変位 253 ($C4 PRESET)
                FCB     $18,$CA                         ; 'Q' → 変位 280 ($CA RANDOMIZE)
                FCB     $18,$CA                         ; 'R' → 変位 280 ($CA RANDOMIZE)
                FCB     $43,$D2                         ; 'S' → 変位 323 ($D2 SYMBOL)
                FCB     $65,$DA                         ; 'T' → 変位 357 ($DA TROFF)
                FCB     $7C,$E0                         ; 'U' → 変位 380 ($E0 UNLIST)
                FCB     $8A,$E3                         ; 'V' → 変位 394 ($E3 WHILE)
                FCB     $8A,$E3                         ; 'W' → 変位 394 ($E3 WHILE)
                FCB     $98,$E6                         ; 'X' → 変位 408 ($E6 XOR)
                FCB     $9B,$E7                         ; 'Y' → 変位 411 ($E7 \)
                FCB     $9B,$E7                         ; 'Z' → 変位 411 ($E7 \)
kw_idx_end:
fn_idx:
                FCB     $00,$00                         ; 'A' → 変位   0 ($80 ANPORT)
                FCB     $0F,$04                         ; 'B' → 変位  15 ($84 CSRLIN)
                FCB     $0F,$04                         ; 'C' → 変位  15 ($84 CSRLIN)
                FCB     $28,$0A                         ; 'D' → 変位  40 ($8A DATE)
                FCB     $2C,$0B                         ; 'E' → 変位  44 ($8B EOF)
                FCB     $32,$0D                         ; 'F' → 変位  50 ($8D FIX)
                FCB     $38,$0F                         ; 'G' → 変位  56 ($8F HEX$)
                FCB     $38,$0F                         ; 'H' → 変位  56 ($8F HEX$)
                FCB     $3C,$10                         ; 'I' → 変位  60 ($90 INKEY$)
                FCB     $4F,$14                         ; 'J' → 変位  79 ($94 LEFT$)
                FCB     $4F,$14                         ; 'K' → 変位  79 ($94 LEFT$)
                FCB     $4F,$14                         ; 'L' → 変位  79 ($94 LEFT$)
                FCB     $5D,$18                         ; 'M' → 変位  93 ($98 MID$)
                FCB     $61,$19                         ; 'N' → 変位  97 ($99 OCT$)
                FCB     $61,$19                         ; 'O' → 変位  97 ($99 OCT$)
                FCB     $65,$1A                         ; 'P' → 変位 101 ($9A POINT)
                FCB     $74,$1E                         ; 'Q' → 変位 116 ($9E RIGHT$)
                FCB     $74,$1E                         ; 'R' → 変位 116 ($9E RIGHT$)
                FCB     $7D,$20                         ; 'S' → 変位 125 ($A0 STRING$)
                FCB     $9D,$27                         ; 'T' → 変位 157 ($A7 TIME)
                FCB     $A4,$29                         ; 'U' → 変位 164 ($A9 VARPTR)
                FCB     $A4,$29                         ; 'V' → 変位 164 ($A9 VARPTR)
                FCB     $AD,$2B                         ; 'W' → 変位 173 ($AB (表終端))
                FCB     $AD,$2B                         ; 'X' → 変位 173 ($AB (表終端))
                FCB     $AD,$2B                         ; 'Y' → 変位 173 ($AB (表終端))
                FCB     $AD,$2B                         ; 'Z' → 変位 173 ($AB (表終端))
fn_idx_end:
