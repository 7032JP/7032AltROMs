; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 変数管理
;
; 変数レイアウト (VARTAB以降) —可変長名へ改めた:
;   各変数: [型|名前長] [名前 1..16 バイト] [値...]
;   先頭バイトの上位2ビット: 00=単精度, 01=整数, 10=倍精度, 11=文字列
;   先頭バイトの下位6ビット: 名前の有効長 (1-16)
;   整数: 2B, 単精度: 4B, 倍精度: 8B, 文字列: 2B(ポインタ)+1B(長さ)
;   1 変数の記録域 = 1 + 名前長 + 値サイズ (動作観察どおりの大きさ)。
;   配列: ARYTAB以降、[型|名前長][名前][len(2B)][ndim(1B)][各次元(2B)][要素...]
;==============================================================================

VTYPE_SNG       EQU     $00
VTYPE_INT       EQU     $40
VTYPE_DBL       EQU     $80
VTYPE_STR       EQU     $C0

; 変数サイズテーブル (型→バイト数): 整数2,単精度4,倍精度8,文字列3
var_size_tbl:
                FCB     4,2,8,3         ; SNG,INT,DBL,STR

; impl_get_var_ref: X が指す変数名を読み, VARPTR(U)/VARTYP を設定
;   入力: X = 変数名先頭
;   出力: U = 変数値アドレス, VARTYP = 型 (VTYPE_xx), X = 名前の直後
;
;   変数名は先頭 16 文字が有意 + 型サフィックス (% ! # $)。
;   VARTAB 上の格納形式は [型|名前長][名前][値...]。
;   名前は本文上の位置 VNPTR と有効長 VNLEN で表し (複写しない)、検索/新規作成する
;   (スタックや X に頼らないことでレジスタ破壊バグを避ける)。
impl_get_var_ref:
                BSR     parse_var_name  ; VNPTR/VNLEN/VARTYP を設定 (X=名前直後)
                ; 配列チェック: 次が '(' なら配列
                ; (標準 SAVE 形式は空白を保存するため名前と '(' の間の空白を許容)
                JSR     skip_sp_lparen
                LBEQ    gvr_array
                BRA     gvr_find_scalar

; parse_var_name: X の変数名+型を VNPTR/VNLEN/VARTYP に解析し X を名前直後へ。
; 有効文字数は先頭 16 文字 (標準構成の動作観察。17 文字目で初めて衝突する)。
;   先頭 2 文字のみを有意にすると、3 文字目以降だけが異なる別変数が同一視される
;   (XA1/XA2/XA3/XA4 が 1 個に潰れる、XY1=XY*SCL が自己代入になる 等)。
;   名前は本文上の位置 (VNPTR) と長さ (VNLEN) で表し、複写はしない。
;   17 文字目以降の英数字は名前に含めずに読み飛ばす (X は名前の直後まで進む)。
;   破壊: A, B, X, U (U は型サフィックス省略時の DEFTYPE 引きに使う。全呼出元で
;   U は非生存であることを確認済 = impl_get_var_ref / impl_erase / impl_decl_array /
;   igc_arrname / idef_fn / fn_name_eq。fn_name_eq のみ U を自前で退避している)。
parse_var_name:
                JSR     skip_sp_x       ; 先行空白を読み飛ばす (テープ由来本文対策)
                STX     VNPTR           ; 名前先頭 (呼出元が英字であることを保証する)
                LDB     #1
                LEAX    1,X             ; 名前1 は無条件に取る
                ; 2 文字目以降: 英数字が続く限り取り込む
                ; (数字も名前の構成文字。X1/F11 のような名前も全長を取る)
pvn_lp:
                LDA     ,X
                CMPA    #'A'
                BLO     pvn_dig
                CMPA    #'Z'
                BHI     pvn_done
                BRA     pvn_take
pvn_dig:
                CMPA    #'0'
                BLO     pvn_done
                CMPA    #'9'
                BHI     pvn_done
pvn_take:
                LEAX    1,X
                CMPB    #VN_MAXLEN
                BHS     pvn_lp          ; 17 文字目以降は読み飛ばすだけ
                INCB
                BRA     pvn_lp
pvn_done:
                STB     VNLEN
gvr_n2done:
                ; 型サフィックス判定 (省略時は DEFTYPE/既定SNG)
                CLRB                    ; VTYPE_SNG = 0
                LDA     ,X
                CMPA    #'%'
                BNE     gvr_not_int
                LDB     #VTYPE_INT
                LEAX    1,X
                BRA     gvr_type_set
gvr_not_int:
                CMPA    #'!'
                BNE     gvr_not_sng
                CLRB                    ; VTYPE_SNG = 0
                LEAX    1,X
                BRA     gvr_type_set
gvr_not_sng:
                CMPA    #'#'
                BNE     gvr_not_dbl
                LDB     #VTYPE_DBL
                LEAX    1,X
                BRA     gvr_type_set
gvr_not_dbl:
                CMPA    #'$'
                BNE     gvr_no_suffix
                LDB     #VTYPE_STR
                LEAX    1,X
                BRA     gvr_type_set
gvr_no_suffix:
                ; サフィックスなし → DEFTYPE[名前1-'A'] で型を引く
                LDU     VNPTR
                LDA     ,U              ; 名前1 (本文上を直接読む)
                CMPA    #'A'
                BLO     gvr_type_set
                CMPA    #'Z'
                BHI     gvr_type_set
                SUBA    #'A'
                LDU     #DEFTYPE
                LDB     A,U             ; DEFTYPE[名前1-'A'] (0-3)
                ASLB                    ; 型ビット位置 (bit6-7) へ
                ASLB
                ASLB
                ASLB
                ASLB
                ASLB
gvr_type_set:
                STB     VARTYP
                RTS                     ; parse_var_name 復帰
; gvr_array の本体は容量の都合で別の区画の空きプール (7t_arr_ndim.inc) へ移した。
; 入口はラベル参照のみ (impl_get_var_ref の LBEQ / igc_an_elem の JMP) で
; 位置依存はない。
gvr_find_scalar:
                ; VARTAB を走査して名前一致を探す
                PSHS    X               ; 入力ポインタ退避
                LDU     VARTAB
gvr_scan:
                CMPU    ARYTAB
                BHS     gvr_new         ; 見つからない → 新規作成
                ; 先頭バイト [型|名前長] を検索キーと一括比較する。型ビットを
                ; 捨てて比較すると A と A$ (と A%) が同一エントリに命中し、A=1 の
                ; 後の A$="..." が数値 A の格納域を文字列記述子で上書き破壊する。
                ; F-BASIC では型サフィックス違いは別変数 (参考書籍)。
                LDA     VNLEN
                ORA     VARTYP          ; 検索キー = 型|名前長
                CMPA    ,U
                BNE     gvr_next
                BSR     vn_cmp          ; 名前本体 (U+1 から VNLEN バイト) を比較
                BNE     gvr_next
                ; 名前+型 一致 → 値アドレス = U + 1 + 名前長
                PULS    X               ; 入力ポインタ復元
                BRA     gvr_valptr      ; 末尾呼び (gvr_valptr の RTS で復帰)
gvr_next:
                ; エントリサイズ分スキップ (= ヘッダ 1B + 名前長 + 値サイズ)
                LDA     ,U              ; hdr = 型|名前長
                PSHS    A
                BSR     gvr_valptr      ; U = 値先頭 (+ ヘッダ + 名前長)
                PULS    A
                BSR     vn_valsize      ; B = 値サイズ
                LEAU    B,U
                BRA     gvr_scan
gvr_new:
                ; 新規変数を ARYTAB (=変数領域末尾) に追加する。
                ; 相互運用要件レイアウトでは配列がこの直後に密着しているため、
                ; vars_make_room が配列ブロックをエントリサイズ分上へ移動して
                ; 隙間を空ける (OM 検査込み。動作観察どおりの挙動)。
                ; エントリサイズ = 名前 2B + 値サイズ (型別) を Y に用意する。
                LDA     VARTYP
                BSR     vn_valsize      ; B = 値サイズ
                ADDB    VNLEN
                INCB                    ; + ヘッダ 1B
                STB     vnw_size        ; エントリバイト数 (Y は使わない:
                                        ;   旧 gvr_new は Y 非破壊契約で、呼び元が
                                        ;   Y を保持しているため必ず守る)
                JSR     vars_make_room  ; U = 書込先 (旧 ARYTAB)。OM 時は戻らない
                ; ヘッダ (型|名前長) と名前本体を格納する
                LDA     VNLEN
                ORA     VARTYP
                STA     ,U+
                TFR     U,X
                JSR     vn_copy         ; 名前本体を ,X+ へ複写 (X 前進, U 破壊)
                TFR     X,U
                PULS    X               ; 入力ポインタ復元 (gvr_find_scalar の PSHS X)
                PSHS    U               ; 値先頭アドレスを退避 (戻り値)
                ; 値領域ゼロクリア。
                ; クリア幅と ARYTAB の前進量は、確保幅 (vnw_size = 名前 2B +
                ;   値サイズ) と同じ単一の出所 var_size_tbl から導く。走査側
                ;   (gvr_next) も同じ表でエントリを読み飛ばすため、確保・クリア・
                ;   末尾更新・走査の 4 者が常に一致する。
                ;   型ごとの分岐で幅を個別に書くと、型の追加・変更のたびに
                ;   確保幅と ARYTAB 前進量が食い違い、次に作られる変数のエントリ
                ;   が既存の値スロット内部へ書かれる (走査側は表どおり読み飛ばす
                ;   のでそのエントリを跨ぐ)。型別分岐を持たない形にする。
                LDB     vnw_size        ; ヘッダ 1B + 名前長 + 値サイズ (不変更)
                SUBB    VNLEN
                DECB                    ; B = 値サイズ (2/3/4/8。0 にはならない)
gvr_clr_lp:
                CLR     ,U+
                DECB
                BNE     gvr_clr_lp
gvr_update_arytab:
                STU     ARYTAB          ; 変数領域末尾を更新
                PULS    U,PC ; U = 値先頭 (戻り値)

; ---------------------------------------------------------------------------
; 可変長名の共通部品
; ---------------------------------------------------------------------------
; vn_valsize: A = 型ビット (bit7-6) を含むバイト → B = 値サイズ。A, X 破壊。
vn_valsize:
                ANDA    #$C0
                LSRA
                LSRA
                LSRA
                LSRA
                LSRA
                LSRA                    ; A = 型インデックス 0-3
                LDX     #var_size_tbl
                LDB     A,X
                RTS

; gvr_valptr: U = スカラエントリ先頭 → U = 値先頭 (U + 1 + 名前長)。A 破壊。
gvr_valptr:
                LDA     ,U
                ANDA    #$3F
                INCA                    ; +ヘッダ 1B を畳む
                LEAU    A,U
                RTS

; vn_cmp: VNPTR/VNLEN の名前と、U が指すエントリの名前 (U+1 から VNLEN バイト)
;   を比較する。名前は双方とも本文/表の上に在るものを直接読む (複写しない)。
;   出力: Z=1 一致 / Z=0 不一致。A, B 破壊。U, X, Y 保存。
vn_cmp:
                PSHS    Y
                LDY     VNPTR
                CLRB
vnc_lp:
                LDA     B,Y             ; 名前[i]
                INCB
                CMPA    B,U             ; エントリ名前[i] (= U+1+i)
                BNE     vnc_out         ; Z=0 で脱出
                CMPB    VNLEN
                BLO     vnc_lp
                                        ; 全長一致 → 直前の CMPB で Z=1
vnc_out:
                PULS    Y,PC            ; PULS は CC を変えない

; impl_get_var_val: 変数値を FAC1 に読み込む
impl_get_var_val:
                JSR     impl_get_var_ref
                ; U = 値アドレス, VARTYP = 型
                LDA     VARTYP
                CMPA    #VTYPE_INT
                BEQ     gvv_int
                CMPA    #VTYPE_STR
                BEQ     gvv_str
                JMP     pool_var_load
gvv_int:
                LDD     ,U
                JMP     impl_int_to_fac
gvv_str:
                LDD     1,U             ; 実体先頭 (記述子 +1)
                STD     STRPTR
                LDA     ,U              ; 長さ (記述子 +0)
                STA     STRLEN
                JMP     fret_str_own    ; FACTYPE=3 を立てて復帰 (共通尾部)

; impl_assign_var: FAC1 の値を U が指す変数に格納
impl_assign_var:
                LDA     VARTYP
                CMPA    #VTYPE_INT
                BEQ     av_int
                CMPA    #VTYPE_STR
                BEQ     av_str
                JMP     pool_var_store
av_int:
                ; 整数変数への代入は最近接丸め (2.7 → 3 / -2.7 → -3) で、整数の
                ;   範囲を外れたら Overflow (参考書籍の整数型)。
                JSR     impl_fac_to_int_round
                STD     ,U
                RTS
av_str:
                JMP     str_asg_mark    ; 本体は別の区画の空きプール

; (整数制御変数への初期値セットを中継していた入口は、代入を構築の最後へ
;  回した際に参照が無くなったので撤去した。単精度側も同じ組み替えで
;  中継 pool_assign_for_var ごと参照が無くなり撤去済)

ipf_cp4:
                LDB     #4
                JMP     mcp_l           ; 4 バイトの複写 (多倍長ヘルパと共用)

; pfor_reuse_scan: 枠列の先頭から続く繰り返しの枠の中に for_var_ptr と同じ
;   制御変数の枠があれば、その枠とその内側を捨てる。同一変数での FOR 再突入が
;   古いループを置き換える挙動 (参考書籍 FOR〜NEXT) の再現。
;   NEXT を経ずに放棄されたループが積み上がって溢れるのを防ぐ。
;   繰り返し以外の枠 (副手続き / WHILE / 直接モードの行) に当たったら打ち切る
;   (副手続きの中の同名の FOR は外側と別の入れ子)。
;   破壊: A,B,U。X は保存。fss_alloc 冒頭から呼ばれる (INT/SNG 両経路に効く)。
pfor_reuse_scan:
                PSHS    X
                LDX     EXEC_SP
prs_lp:
                CMPX    STRBOT
                BHS     prs_out         ; 枠列の末尾まで一致なし
                LDA     ,X
                CMPA    #TAG_FOR
                BNE     prs_out         ; 繰り返し以外の枠 → 打ち切り
                LDD     1,X             ; var_addr(+フラグ)
                CMPD    for_var_ptr
                BEQ     prs_hit
                LEAX    FRM_FOR,X
                BRA     prs_lp
prs_hit:
                LEAX    FRM_FOR,X
                JSR     frame_del       ; 一致した枠とその内側を捨てる
prs_out:
                PULS    X,PC

; impl_push_while: WHILE の IP を GOSUB スタックに保存 (4B フレーム: IP + CURLINE_PTR)
;   CURIP = WHILE トークン位置 (ディスパッチャが文頭で設定)。
;   スタック先頭フレームの IP が既に CURIP と一致する場合は WEND 経由の
;   再評価なので再 push しない (毎周スタックが成長するのを防ぐ)。
;   X (本体先頭 = 条件式直後) は保存する (ディスパッチャの次 IP)。
impl_push_while:
                PSHS    X
                BSR     while_top       ; 枠列の先頭が自分の WHILE の枠か (Z=1)
                BEQ     pwhl_dup        ; 既に自分の枠 → 何もしない
                LDX     CURIP
                LDB     #TAG_WHILE
                JSR     gw_push         ; 枠 (5B) を積む。溢れは Out Of Memory
pwhl_dup:
                PULS    X,PC

; while_top: 枠列の先頭が「IP = CURIP の WHILE の枠」なら Z=1 (X = その枠)。
;   破壊: A,B,X。
while_top:
                LDX     EXEC_SP
                CMPX    STRBOT
                BHS     wt_no
                LDA     ,X
                CMPA    #TAG_WHILE
                BNE     wt_no
                LDD     1,X
                CMPD    CURIP
                RTS
wt_no:
                ANDCC   #$FB            ; Z=0
                RTS

; impl_pop_while: WEND — WHILE の先頭に戻る
;   最も近い WHILE の枠を探す。その内側に残った繰り返しの枠 (GOTO で抜けたもの)
;   は捨てる。副手続きの枠を越えては探さない (WEND without WHILE)。
impl_wend:
impl_pop_while:
                LDB     #TAG_WHILE
                JSR     stk_find
                BNE     pwend_err
                CMPX    EXEC_SP
                BEQ     pw_top
                JSR     frame_del       ; 内側の枠を捨てる (X = WHILE の枠のまま)
                LDX     EXEC_SP
pw_top:
                LDD     3,X
                STD     CURLINE_PTR
                LDX     1,X             ; X = WHILE先頭 IP
                STX     CURIP
                ; WHILEの先頭へ戻って条件を再評価するためにスタックを保持
                ; (WHILE が再度false になったときにpopする)
                JMP     exec_ensure     ; WHILE の行を作業列へ復元 (末尾呼び)
pwend_err:
                LDB     #25             ; WEND without WHILE
                JMP     impl_error_handler

; --- 空きプール ($F398-$F3CC, 53 byte) ---
;   ゼロ周回 FOR 本体スキップ (旧 impl_skip_for) は、本実装が「FOR 本体は
;   最低 1 回実行し、終了判定は NEXT 側で行う」仕様を採る (7t_statements.inc
;   の impl_for を参照) ため、どの経路からも呼ばれない死蔵コードだった。
;   撤去して明示的な空き領域に置き換える。後続 impl_skip_wend 以降のアドレスは
;   不変に保つ。
;------------------------------------------------------------------------------
; iget_body: GET@ (ドットパターン形式) の応答受取本体。
;   impl_get の区画には残量が無いため、本体はこの空き領域に置き末尾呼出で入る。
;
;   矩形読出の応答領域は共有 RAM の $D384-$D3FF (124 byte) しかない。
;   サブシステムは 124 byte を書くごとにいったん処理を止め、
;     $D381 bit7 = 1   … 「続きがある」提示
;     $D383        … 当該チャンクのデータ計数、データ本体は $D384 から
;   の形で部分応答を公開し、メイン側からの継続要求コマンド ($64) を待つ。
;   継続要求を出さずに先頭 1 チャンクで打ち切ると、124 byte (= 992 ドット) を
;   超える大きさのパターンが途中で欠落する。
;
;   ここでは「複写 → 継続提示があれば $64 を送って次チャンクを受け取る」を
;   提示が消えるまで繰り返す。複写長は毎回サブシステムが返した計数に従い、
;   上限でクランプしない (計数は応答領域長を構造的に超えない)。
;   複写先 (gp_arr_ptr) はチャンクごとに前進させる。
;------------------------------------------------------------------------------
iget_body:
                PSHS    X                   ; 本文ポインタ保護: iget_colors が
                                            ;   色リスト書込で X をステージングに残し、
                                            ;   復帰後のディスパッチャが応答データを
                                            ;   本文として誤実行していた
                JSR     iget_colors         ; $D38B=色数 / $D38C- =色コードリスト
igb_chunk:
                JSR     impl_sub_send_cmd
                LDY     gp_arr_ptr          ; 複写先 (0 = 配列無し → 継続だけ処理)
                BEQ     igb_more
                LDB     SUBCMD_P1           ; 当該チャンクの計数 (クランプしない)
                BEQ     igb_more
                LDU     #SUBCMD_P2          ; データ本体の先頭
igb_copy:
                LDA     ,U+
                STA     ,Y+
                DECB
                BNE     igb_copy
                STY     gp_arr_ptr          ; 次チャンクの複写先へ前進
igb_more:
                LDA     SUBSTG_BASE+1       ; 継続提示 (bit7)
                BPL     igb_done            ; 立っていない → 応答は完結
                LDA     #$64                ; 継続要求
                STA     SUBCMD_CMD
                BRA     igb_chunk
igb_done:
                PULS    X,PC                ; 本文ポインタを返して復帰

;------------------------------------------------------------------------------
; ioe_off — ON ERROR GOTO 0 (エラー捕捉の解除) の本体
;   ion_onerr から LBEQ で入る。捕捉を解除するだけが基本だが、エラー処理
;   ルーチンの実行中に解除した場合は、参考書籍のとおりそこで
;   プログラムを止め、処理ルーチンを呼んだ元のエラーを表示する。
;   ERRNUM / ERRLIN は捕捉した時のまま残っているので、表示部 (ieh_report) を
;   呼ぶだけでよい。
;   ion_onerr の属する区間に予算が無いため本プールへ置く
;   (参照はラベル経由のみで位置依存なし)。
;------------------------------------------------------------------------------
ioe_off:
                LDA     ONERROR_EN      ; 解除する前の状態 (2 = 処理ルーチン実行中)
                CLR     ONERROR_EN      ; エラー捕捉を解除
                LSRA                    ; 0/1 → Z=1、2 → Z=0
                LBNE    ieh_report      ; 処理ルーチン内での解除 → 元エラーで停止
                RTS


;------------------------------------------------------------------------------
; irv_body: 区切りで並んだ値の並びから 1 個を取り出す共通本体。
;   DATA 文の値と、INPUT 文へ打ち込まれた行の項目は、どちらも「カンマで
;   区切った値の並び」という同じ書式なので取り出し方を 1 つにまとめてある。
;   入口は 2 つあり (ird2_readval = DATA / inp_readval = INPUT)、どちらも追加の
;   区切りは ':' である。DATA は文の区切りで値も終わり、INPUT へ打ち込まれた行も
;   ':' でコンマと同じように項目が切れる (参考書籍「INPUT 文」の
;   区切りの規定)。
;   入力: X = 値の先頭, VARTYP = 受け取る型, inp_dlm2 = 追加の区切り
;   出力: 数値なら FAC1、文字列なら STRPTR/STRLEN/FACTYPE、
;         X = 値の直後 (後続の空白も読み飛ばし、区切りか終端を指す)
;         C = 0 … 項目として正しい / C = 1 … 数値として読めない項目
;   数値の項目は「1 文字も無い (区切りが続いている)」「数として読めない字が
;   残る」のいずれでも C=1 を返す。呼出側は DATA なら Syntax Error、INPUT なら
;   打ち直しの促しへ分ける (どちらも 0 として黙って受け取ることはしない)。
;
;   値の書式 (参考書籍「DATA 文」「INPUT 文」):
;     引用符付き "..."  引用符は区切り記号であって値には入らない。中身は空白も
;                       カンマもそのまま値になる。前後に空白を含む値や、カンマを
;                       含む値を 1 個として渡す唯一の手段である。閉じ引用符が
;                       無いまま行が終わったら、そこまでを値とする (行末の $00 は
;                       文の終端として呼出側が見るので消費しない)。
;     引用符なし        カンマ・追加の区切り・行末までが値だが、値の前後に付いた
;                       空白は値に含めない。値の途中の空白は残る。空白を詰めないと
;                       `IF A$="CD"` のような比較が成立しなくなる。
;   値を取り出したあと後続の空白を読み飛ばすのは、区切り (カンマ) の判定を
;   空白が妨げないようにするため。空白を残すと「区切りが無い = 並びの終わり」と
;   誤判定し、DATA では次の値を読み飛ばして値が尽き、INPUT では 2 個目以降が
;   区切り記号そのものを読むことになる。
;------------------------------------------------------------------------------
irv_body:
                JSR     skip_sp_x       ; 値の手前の空白を読み飛ばす
                LDA     VARTYP
                CMPA    #VTYPE_STR
                BEQ     irv_str
                JMP     irv_num         ; 数値の項目 (妥当性検査つき。当区間に

                                        ;   余地が無いため本体は別プール)
irv_str:
                LDA     ,X
                CMPA    #'"'
                BNE     irv_raw
                ; --- 引用符付き: 引用符を外して中身をそのまま値にする ---
                LEAX    1,X             ; 開き引用符は値に入れない
                STX     STRPTR
                CLRB
irv_q_lp:
                LDA     ,X+
                BEQ     irv_q_eol       ; 閉じ引用符が無いまま行末
                CMPA    #'"'
                BEQ     irv_fin         ; 閉じ引用符も値に入れない
                INCB
                BRA     irv_q_lp
irv_q_eol:
                LEAX    -1,X            ; 行末の $00 は消費しない
                BRA     irv_fin
irv_raw:
                ; --- 引用符なし: 区切りまでを数え、末尾の空白を落とす ---
                STX     STRPTR
                CLRB
irv_raw_lp:
                LDA     ,X
                BEQ     irv_raw_end
                CMPA    #','
                BEQ     irv_raw_end
                CMPA    inp_dlm2        ; 追加の区切り (DATA=':' / INPUT=無効値)
                BEQ     irv_raw_end
                LEAX    1,X
                INCB
                BRA     irv_raw_lp
irv_raw_end:
                JSR     irv_rtrim       ; 末尾の空白ぶん長さ B を縮める (別プール)
irv_fin:
                STB     STRLEN
                LDA     #3
                STA     FACTYPE
                JMP     irv_okx

;------------------------------------------------------------------------------
; impl_uint_to_fac — D を符号なし 16 ビット (0-65535) の値として FAC1 へ。
;   0-32767 は整数として持つ。32768 以上は整数の上限を超えるので
;   実数 (単精度) として持たせる。impl_int_to_float の仮数組立て入口
;   (iitf_mag) は D を大きさとして受けるので、符号を正に置いて合流する。
;   破壊: A,B,CC。X/Y/U 保存 (呼出元の本文ポインタを壊さない)。
;   (置き場所は区画の都合。参照はラベル経由のみで位置依存はない)
;------------------------------------------------------------------------------
impl_uint_to_fac:
                TSTA
                BMI     u2f_big         ; bit15 = 1 → 32768 以上
                JMP     impl_int_to_fac

u2f_big:
                CLR     FAC1+5          ; 符号 = 正
                JMP     iitf_mag        ; D を大きさとして単精度へ

;------------------------------------------------------------------------------
; get_var_ref_chk — 変数名で始まっていることを確かめてから変数参照を行う。
;   impl_get_var_ref は「呼出元が先頭を英字と保証すること」を前提にしている
;   ので、書式が <変数> と定まっている文 (SWAP など) の入口はここを通す。
;   英字で始まらない綴りは Syntax Error にして、本文ポインタが行の終端を
;   踏み越えないようにする。
;   入力/出力: impl_get_var_ref と同じ。
;   (置き場所は区画の都合。参照はラベル経由のみで位置依存はない)
;------------------------------------------------------------------------------
get_var_ref_chk:
                JSR     skip_sp_x
                JSR     chk_alpha               ; 英字でなければ Syntax Error
                JMP     impl_get_var_ref

; impl_skip_wend: WHILE が偽の時 WEND を探してスキップ
;   入口で、スタック先頭フレームの IP が CURIP (= この WHILE のトークン位置)
;   と一致すれば 1 段 (4B) pop する。WEND 経由の再評価で偽になった場合の
;   後始末。初回評価から偽 (未 push) の場合は何も pop しない。
;   ※ pop 判定はここ (走査前) で行う。行またぎ走査 (isw_nl) が CURIP を
;     更新するため、WEND 発見後では自フレームの同定ができない。
impl_skip_wend:
                PSHS    X
                JSR     while_top       ; 枠列の先頭が自分の WHILE の枠か
                BNE     isw_scan0       ; 違う → 捨てない
                LEAX    FRM_SMALL,X
                JSR     frame_del       ; 自分の枠を捨てる
isw_scan0:
                PULS    X
isw_scan:
                LDB     #1              ; ネスト深さ
isw_lp:
                LDA     ,X+
                BEQ     isw_nl          ; 行末
                ; 関数第 2 バイトは値域が WHILE/WEND ($E2/$E4) と重ならないので
                ; $FF の特別扱いは不要 (作業列に他の多バイト要素は無い)
                CMPA    #TOK_WHILE      ; WHILE
                BNE     isw_nw
                INCB
                BRA     isw_lp
isw_nw:
                CMPA    #TOK_WEND       ; WEND
                BNE     isw_lp
                DECB
                BNE     isw_lp
                ; 対応 WEND を発見。X = WEND 直後。pop は入口で処理済み。
                RTS
isw_nl:
                LDX     CURLINE_PTR
                LDX     ,X
                BEQ     isw_err
                ; 次の行を作業列へ変換して走査を続ける (E2)。exec_conv_line が
                ; CURLINE_PTR/CURLIN を更新する。B (ネスト深さ) は変換で壊れる
                ; ため退避する。
                PSHS    B
                JSR     exec_conv_line
                PULS    B
                STX     CURIP
                BRA     isw_lp
isw_err:
                LDB     #25
                JMP     impl_error_handler

; PEEK / RND。参照はラベル経由。
impl_fn_peek:
                ; 式の値は 0 から 65535 まで (参考書籍 PEEK)
                JSR     eval_p_to_uaddr ; 読出しアドレス (0..65535 の検査つき)
                TFR     D,X
                LDB     ,X
                JMP     int_to_fac_b    ; A=0 を畳んだ入口

; RND [(引数)] — 0 <= x < 1 の乱数 (参考書籍)。
;   引数の扱い (参考書籍の規定どおり):
;     省略 (括弧なし) / 正 … 系列を 1 つ進めて次の値を返す
;     0                  … 系列を進めず直前に返した値をもう一度返す
;     負                 … 系列を初期状態へ戻してから次の値を返す
;   括弧の有無をここで見てから分岐する。無条件に impl_eval_expr_p を
;     呼ぶと、同ルーチンは '(' が無いとき「括弧なしの式」として後続を
;     そのまま評価するため、引数省略形 `IF RND>.9 THEN` で比較演算子ごと
;     引数として食われ、IF の条件が RND の戻り値そのもの (常に真) に化ける
;     (動作観察: 標準構成では `FOR I=1 TO 20: IF RND>.9 THEN C=C+1` の
;     成立は 0/20)。
;   本体は当区間の ZMB 予算が足りないため空きプール (7t_deffn.inc) へ置く。

; impl_on_select: ON...GOTO/GOSUB の選択 (値に従いリスト中の行番号を選択)
;   入力: FAC1 = 選択値
;   出力: FAC1 = 選択された行番号, Z=1なら範囲外
impl_on_select:
                JSR     impl_fac_to_int
                ; 選択値が 0-255 の外なら Illegal Function Call (実行環境の動作観察)。
                ; 符号なしで比べれば負は $8000 以上に見えるので 1 回の比較で
                ; 負も 256 以上もまとめて撥ねられる。リストの個数より大きい値は
                ; 誤りではなく「分岐せず次の文へ」なのでここでは見ない。
                CMPD    #255
                BHI     ons_fcerr
                TSTB                    ; 上位は 0 と確定したので下位だけで足りる
                BEQ     ons_zero        ; 0 = 分岐せず次の文へ
                PSHS    D               ; 選択値 (1-origin)
ons_lp:
                LDD     ,S
                CMPD    #1
                BEQ     ons_found
                ; 行番号をスキップ (カンマ区切り)。
                ;   選択成立時 (ons_found) と同一の impl_eval_linenum で読み進める。
                ;   impl_parse_num は行番号参照の 2 進形式 ($FE $F2 hh ll) や参照
                ;   手前の先行空白 ($20) を扱えず、X が前進しないため使わない。
                ;   impl_eval_linenum は skip_sp_x + $FE/ASCII 両形式を受理し、
                ;   選択パスと同じだけ X を前進させるため、スキップと選択の X
                ;   前進量が必ず一致する。
                ;   (D は破壊されるが直後に LDD ,S でカウンタを再ロードするので無害)
                JSR     impl_eval_linenum ; 行番号を読み飛ばす (選択パスと同一処理)
                LDA     ,X+             ; カンマ
                CMPA    #','
                BNE     ons_out2_back   ; 非カンマ → 終端を消費しすぎたので戻す
                LDD     ,S
                SUBD    #1
                STD     ,S
                BRA     ons_lp
ons_out2_back:
                LEAX    -1,X            ; 終端 (':'/$00) の手前へ戻す
                BRA     ons_out2
ons_found:
                LEAS    2,S             ; 選択カウンタを捨てる
                JSR     impl_eval_linenum ; FAC1=対象行, D=対象行番号
                ; この時点で X は選択した行番号の直後 (リスト途中) を指す。
                ;   ON GOSUB の復帰位置は「ON 文の直後の文」でなければならないため、
                ;   残りの行番号リスト (,l,l,…) を末尾 (':'/$00) まで読み飛ばして
                ;   X を文末へ前進させる。これを行わないと GOSUB 復帰フレームが
                ;   リスト途中を指し、RETURN が ",200…" を実行して暴走/無限ループ
                ;   してしまう (ON GOTO は復帰しないので X が途中でも動く)。
                ;   選択行番号 D は impl_find_line の入力なので退避/復元する
                ;   (impl_eval_linenum が D/FAC1 を破壊するため)。
                PSHS    D               ; 選択行番号を退避
ons_skiprest:
                JSR     skip_comma
                BCC     ons_skipdone
                JSR     impl_eval_linenum ; 次候補の行番号を読み飛ばす
                BRA     ons_skiprest
ons_skipdone:
                PULS    D               ; 選択行番号を復元
                ANDCC   #$FB            ; Z=0 (D=選択行番号のまま返す)
                RTS
ons_out2:
                PULS    D
ons_out:
                ORCC    #$04            ; Z=1
                RTS
ons_fcerr:
                JMP     errc_5          ; Illegal Function Call
ons_zero:
                ; 選択値が 0 以下: 分岐しない。X はまだ行番号リストの先頭を
                ; 指しているので、リストを文末まで読み飛ばしてから Z=1 で戻る。
                ; 読み残すと生の数字が文の位置に残り Syntax Error になる
                JSR     impl_eval_linenum ; 候補 1 個を読み飛ばす
onz_lp:
                JSR     skip_comma      ; ',' が続けば C=1 で消費
                BCC     ons_out         ; リスト末尾 → Z=1 で戻る
                JSR     impl_eval_linenum
                BRA     onz_lp


; impl_read_data: DATA から値を読んで FAC1 へ
;   状態:
;     DATLINE ($0064) = 現在走査中の DATA 行レコード先頭 (next_ptr を指す)
;     DATPTR  ($0027) = 次に読み出す位置 (直前値の直後 or 0=未探索)
;   RESTORE は DATLINE=対象行レコード先頭, DATPTR=0 (未探索) に設定する。
impl_read_data:
                ; 文字列の値を原文へ張り替えるための対応付け (ird2_share) を初期化する。
                ;   継続読みでは直前値の直後 (',' の位置) を dat_tgt に置いて DATA 行を
                ;   変換し、同じ DATA 文の原文の位置を得る (行変換の tdd_end が記す)。
                ;   未探索 (DATPTR=0) では dat_tgt=0 となり、変換で対応付けは記されない。
                CLR     dat_src
                CLR     dat_src+1
                ; DATPTR が継続位置 (直前値の直後) を持つか判定
                LDD     DATPTR
                STD     dat_tgt
                BEQ     ird2_findline   ; 0 → 行探索が必要
                ; 継続: DATA 行を作業列へ復元してから直前値の直後を見る
                ; (DATPTR は作業列内の位置。変換は決定的なので復元後も同値で有効)
                LDX     DATLINE
                JSR     exec_load_rec
                ; 直前値の直後が ',' なら同 DATA 文の次の値
                LDX     DATPTR
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     ird2_advline    ; ',' でない → この DATA 文は終了
                LEAX    1,X             ; ',' をスキップ
                BSR     ird2_readval
                BCS     ird2_syn        ; 数値として読めない値 → 誤り
                BRA     ird2_got
ird2_advline:
                ; 対応付けは尽きた DATA 文のものなので捨てる (次の DATA 文の値は
                ;   ird2_share が変換し直して対応付ける)。dat_tgt も倒し、次の行の
                ;   変換で古い位置に対応付けが記されないようにする
                CLR     dat_src
                CLR     dat_src+1
                CLR     dat_tgt
                CLR     dat_tgt+1
                ; この DATA 文は尽きた。DATA 文は行末だけでなく文の区切り ':' でも
                ; 終わり、同じ行の続きに次の DATA が書けるので、行を跨がずに
                ; その位置から文の走査を続ける。行末 ($00) なら ird2_stmts が
                ; 次の行レコードへ送るので、ここでの行送りは要らない。
                LDX     DATPTR
                BRA     ird2_stmts
ird2_findline:
                ; DATLINE から DATA 文を線形に探索。候補行を作業列へ変換し、
                ; 既存のトークン走査 (TOK_DATA 探し) をそのまま作業列上で使う。
                LDX     DATLINE
                LDD     ,X
                BEQ     ird2_ood        ; next_ptr=0 → プログラム末尾 → Out Of Data
                JSR     exec_load_rec
                LDX     #EXECBUF        ; X = 変換済み本文の先頭
ird2_stmts:
                LDA     ,X+
                BEQ     ird2_nextrec    ; 行末 → 次レコード
                CMPA    #'"'            ; 文字列リテラル → 閉じ引用符まで飛ばす
                BNE     ird2_nq         ;   (文字列中の $83/$00 誤認防止)
ird2_str:
                LDA     ,X+
                BEQ     ird2_nextrec    ; 閉じ忘れ文字列 → 行末扱い
                CMPA    #'"'
                BNE     ird2_str
                BRA     ird2_stmts
ird2_nq:
                CMPA    #TOK_FUNC_PFX   ; 関数前置 ($FF) → 第 2 バイトを読み飛ばす
                BNE     ird2_nfe        ;   (関数トークンの DATA 誤認防止。
                LEAX    1,X             ;    関数の第2バイトを DATA と取り違えていた)
                BRA     ird2_stmts
ird2_nfe:
                CMPA    #TOK_DATA       ; DATA トークン
                BNE     ird2_stmts
                ; DATA の後の値を読む。DATA トークン直後の空白を読み飛ばす。
                JSR     skip_sp_x
                BSR     ird2_readval
                BCS     ird2_syn        ; 数値として読めない値 → 誤り
ird2_got:
                STX     DATPTR
                ; 文字列の値は複写せず原文を指す (ird2_share。空文字列は実体を持たない)
                LDA     VARTYP
                CMPA    #VTYPE_STR
                BNE     ird2_ret
                TST     STRLEN
                BEQ     ird2_ret
                JMP     ird2_share
ird2_ret:
                RTS
ird2_nextrec:
                LDX     DATLINE
                LDX     ,X              ; next_ptr
                STX     DATLINE
                BRA     ird2_findline
ird2_ood:
                LDB     #4              ; Out Of Data
                JMP     impl_error_handler

; 値の取り出し (irv_body) の入口 2 つ。本体は区画の空き都合で同ファイル内の
; 別プールに置いてある。参照はラベル経由のみで位置依存はない。追加の区切りはどちらも ':' である。DATA は文の区切りで値も終わり、
; INPUT へ打ち込まれた行も ':' でコンマと同じように項目が切れる。
ird2_readval:                           ; DATA 文から
inp_readval:                            ; INPUT 文から
                LDA     #':'
                STA     inp_dlm2
                JMP     irv_body

; ird2_syn: DATA の値が数値として読めないときの誤り。
;   誤りの発生行は READ を書いた行ではなく、その値を持つ DATA の行として
;   報告する (参考書籍の表示例と同じ)。ON ERROR GOTO で捕まえた
;   ときの ERL も DATA の行になる。DATLINE は行レコードの先頭を指しており、
;   行番号はその 2 バイト後ろにある。
ird2_syn:
                LDX     DATLINE
                LDD     2,X
                STD     CURLIN
ivp_syn:
                JMP     errc_2          ; Syntax Error

; irv_rtrim: 引用符なしの値の末尾に付いた空白ぶん、長さ B を縮める。
;   入力: B = 区切りまでの生の長さ, X = 区切りの位置 (値の直後)
;   出力: B = 末尾の空白を除いた長さ。X は動かさない (呼出側が区切りを見るため)
;   U は必ず退避する。INPUT 文は代入先の変数のアドレスを U に載せたまま値の
;     取り出しへ入るので、ここで U を潰すと代入先を見失って値が入らない
;     (引用符なしの項目だけが長さ 0 になる)。
;   置き場所は区画の空きの都合。参照はラベル経由のみで位置依存はない。
irv_rtrim:
                PSHS    U
                TFR     X,U
irt_lp:
                TSTB
                BEQ     irt_done        ; 全部空白だった → 長さ 0
                LDA     -1,U
                CMPA    #' '
                BNE     irt_done
                LEAU    -1,U
                DECB
                BRA     irt_lp
irt_done:
                PULS    U,PC

;==============================================================================
; 数値解析 impl_eval_linenum (行番号)
;==============================================================================
impl_eval_linenum:
                ; 行番号の手前には利用者が打った空白 ($20) が本文にそのまま残る
                ; ことがあるので、先行空白を読み飛ばしてから解析する。
                JSR     skip_sp_x
                ; ASCII 10進行番号は共通の符号なし整数パーサ pn2_uint で読む。
                ; (自前の *10 ループを置くと 3 桁以上で桁上げを誤りやすい)
                JSR     pn2_uint        ; D = 行番号, X 前進
                JMP     impl_int_to_fac

;==============================================================================
; 行番号解析 (impl_parse_linenum)
;==============================================================================

;==============================================================================
; impl_eval_expr_p: 括弧付き式評価 fn(expr)
;==============================================================================
impl_eval_expr_p:
                JSR     skip_sp_lparen
                BNE     ieep_nopar
                LEAX    1,X
                JSR     chk_stack       ; 関数の入れ子 1 段ごとにシステムスタックの空きを確かめる
                JSR     impl_eval_expr
                ; 開き括弧を読んだ以上、引数の直後は必ず閉じ括弧でなければならない。
                ; 1 バイトを無検査で読み飛ばすと、閉じ括弧が無い書き方
                ; (`PRINT LEN("AB` のように引数の文字列定数が閉じないまま行が
                ; 終わる、`PRINT LEN(3` のように閉じ括弧を打ち忘れる) で行末の
                ; $00 を食い進み、次の行レコードの生バイトを文として実行し始める。
                ; 誤りとして扱われないので `ON ERROR GOTO` でも捕まえられない。
                ; 行末でも文の途中でも Syntax Error で打ち切る (括弧だけが閉じない
                ; `PRINT ("AB` を式の側で打ち切るのと同じ規則)。
                LDA     ,X+             ; ')'
                CMPA    #')'
                BNE     ivp_syn         ; 閉じ括弧が無い → Syntax Error
                STX     fn_xsave        ; 引数直後の本文ポインタを保存 (関数復帰用)
                RTS
ieep_nopar:
                JSR     impl_eval_expr
                STX     fn_xsave
                RTS



;==============================================================================
; PRINT 値出力
;==============================================================================
; PRINT の 1 項目は桁の途中で割らずに出す。行末までに収まらない項目は行を
; 送って次行の先頭から出す (参考書籍の PRINT。動作観察でも、区切り
; が ';' の並びで項目が行末をまたぐとき、項目は割れずにまとまって次行の先頭へ
; 現れる)。判定には整形後の桁数が要るので、その段取りは pv_item が行う。
; 整形だけを取り出したい呼出 (PRINT USING の桁分解) は、判定を通さない入口
; pv_direct を使う (行送りを捕捉バッファへ書き込ませないため)。
impl_print_val:
;------------------------------------------------------------------------------
; pv_item — PRINT の 1 項目を、行に収まるかを確かめてから出す。
;   文字列項目は綴りの長さがそのまま桁数だが、数値項目は整形しないと桁数が
;   分からないので、先に綴りへ直してから判定し、その綴りをそのまま出す
;   (同じ数を 2 度整形しない)。数値の後ろに付く区切りの空白は、収まるかの
;   判定には数えない (動作観察: 項目の綴りが行末にちょうど収まるときは行を
;   送らず、区切りの空白だけが次行の先頭へ回る)。
;------------------------------------------------------------------------------
pv_item:
                JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
                BEQ     pvi_str
                PSHS    X               ; 本文ポインタを保つ (変換が X を使う)
                JSR     num_to_str      ; 数値項目 → 綴り (区切りの空白は付かない)
                BSR     pvi_len         ; 収まらなければ先に行を送る
                BSR     pv_str          ; 綴りを出す
                PULS    X
                JMP     put_space
pv_direct:
                JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
                BEQ     pv_str
                TSTA
                LBNE    pv_flt          ; 実数 (単精度/倍精度) → プール分岐へ
                ; 整数は STR$ と同じ 10 進変換で綴りにしてから出す。桁の生成を
                ; 2 か所に持たず num_to_str (= int_to_str_raw) 1 本に統一する。
                ; 数値の後ろに付く区切りの空白は、綴りには含まれないのでここで出す。
                PSHS    X               ; 本文ポインタを保つ (変換が X を使う)
                JSR     num_to_str
                BSR     pv_str
                PULS    X
                JMP     put_space
pvi_str:
                BSR     pvi_len
                BRA     pv_str
;------------------------------------------------------------------------------
; pvi_len — 桁数 STRLEN の項目が現在行に収まらなければ先に行を送る。
;   PRINT_COL は画面の幅で折り返して 0 に戻る (impl_putchar) ので、そのまま
;   現在桁である。行頭に居るときは、1 行に収まらない長い項目でも送らない
;   (送ると空行が 1 行入ってしまう)。
;------------------------------------------------------------------------------
pvi_len:
                LDA     PRINT_COL
                BEQ     pvi_ret         ; 行頭 → そのまま出す
                ADDA    STRLEN          ; 現在桁 + 項目の桁数
                BCS     pvi_nl          ; 桁あふれ → 収まらない
                CMPA    con_cols
                BLS     pvi_ret         ; 行末までに収まる
pvi_nl:
                JMP     disk_crlf       ; 収まらない → 先に行を送る
pvi_ret:
                RTS
pv_str:
                ; 呼び出し元 (PRINT) は X を本文ポインタとして保持するため退避する。
                PSHS    X
                LDX     STRPTR
                LDB     STRLEN
                BEQ     pvs_done
pvs_lp:
                JSR     put_x_inc
                DECB
                BNE     pvs_lp
pvs_done:
                ; 出し終えた実体が一時の最上位なら、ここで返す。返さないと
                ;   1 つの PRINT 文の中で作った一時が項目ごとに積み上がり、
                ;   文字列領域を絞ったプログラムが領域を使い切ってしまう。
                JSR     str_tmp_free_top
                PULS    X,PC ; 本文ポインタ復元
