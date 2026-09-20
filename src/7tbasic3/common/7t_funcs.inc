; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
;==============================================================================
; ファイル名解析 impl_parse_fname は 7t_diskboot.inc へ移設した。
;   (コード領域を圧迫しないため、装置プレフィクス判定を含む本体は
;    ディスク関連コードと同居させ 7t_diskboot の空き領域に配置する。)
;==============================================================================

;==============================================================================
; 関数実装 (数値)
;==============================================================================

; SGN(x): x>0 → 1 / x=0 → 0 / x<0 → -1 (参考書籍「SGN 関数」)。
;   本体は ZMB $C000 が吸収するプールへ配置。
impl_fn_sgn:
                JSR     eval_p_num
                JMP     pool_fn_sgn

impl_fn_int:
                JSR     eval_p_num
                JMP     pool_fn_int_floor   ; 床関数本体 (空きプールへ配置)

impl_fn_abs:
                JSR     eval_p_num
                JMP     pool_fn_abs     ; 型を保ったまま符号を落とす (本体はプール)


; FRE の本体は 7t_float.inc の大文字変換入口の手前に置く。

impl_fn_fix:
                JSR     eval_p_num
                JMP     pool_fn_fix_trunc

impl_fn_len:
                JSR     eval_p_str
                LDB     STRLEN
                JMP     int_to_fac_b    ; A=0 を畳んだ入口

impl_fn_asc:
                JSR     eval_p_str
                LDX     STRPTR
                LDB     ,X
                JMP     int_to_fac_b    ; A=0 を畳んだ入口

impl_fn_cint:
                JSR     eval_p_num
                ; 丸め+整数範囲検査 (プール) (ここへ展開)
;------------------------------------------------------------------------------
; pool_fn_cint — CINT(x): 最近接丸めで整数へ。整数の範囲を外れたら Overflow。
;   (動作観察: CINT(2.5)=3 / CINT(-2.5)=-3 / CINT(40000.7)=Overflow)
;------------------------------------------------------------------------------
pool_fn_cint:
                JSR     impl_fac_to_int_round
                JMP     impl_int_to_fac

impl_fn_csng:
                JSR     eval_p_num
                JMP     dbl_demote      ; 倍精度は単精度へ変換。他は素通し

impl_fn_cdbl:
                JSR     eval_p_num
                JMP     dbl_widen       ; 単精度/整数を倍精度へ拡張

; PEEK / RND の入口は 7t_vars_print.inc の PSG 初期化入口の手前に置く。

; impl_fn_varptr: VARPTR(変数) — 変数の値が格納されている RAM 上の先頭アドレスを返す。
;   参考書籍記載のとおり、配列要素 VARPTR(A%(i)) は当該要素の格納
;   アドレス、スカラ VARPTR(V) はその変数の値領域先頭アドレスを返す。常駐型ローダや
;   LOADM/SAVEM のアドレス引数 (LOADM"f",VARPTR(A%(0))+ofs 等) で多用される。
;   入力: X = VARPTR トークン直後 (先頭が '(')
;   出力: FAC1 = アドレス, fn_xsave = 引数 (閉じ括弧) の直後
impl_fn_varptr:
                JSR     skip_sp_lparen
                BNE     ifvp_noparen
                LEAX    1,X             ; VARPTR 開き括弧 '(' をスキップ
                JSR     impl_get_var_ref ; U = 変数値アドレス
                ; impl_get_var_ref は配列なら X を配列の ')' 直後へ進めるが、
                ; スカラでは X を名前先頭へ戻す。VARPTR の閉じ括弧へ確実に
                ; 到達するため、現在位置から括弧の対応を取りつつ ')' を探す。
                CLRB              ; B = 括弧ネスト深さ
ifvp_scan:
                LDA     ,X
                BEQ     ifvp_close      ; 行/文末 → 終端 (防御)
                CMPA    #':'
                BEQ     ifvp_close
                CMPA    #'('
                BNE     ifvp_chk_close
                INCB
                LEAX    1,X
                BRA     ifvp_scan
ifvp_chk_close:
                CMPA    #')'
                BNE     ifvp_advance
                TSTB
                BEQ     ifvp_close      ; ネスト 0 の ')' = VARPTR の閉じ括弧
                DECB
ifvp_advance:
                LEAX    1,X
                BRA     ifvp_scan
ifvp_close:
                LEAX    1,X             ; 閉じ括弧 ')' をスキップ
ifvp_emit:
                STX     fn_xsave        ; 引数直後の本文ポインタを保存
                TFR     U,D             ; D = 変数アドレス
                JMP     impl_int_to_fac
ifvp_noparen:
                ; '(' が無い異常形は 0 を返す (停止しない)
                STX     fn_xsave
                JMP     int_to_fac_zero         ; FAC1 = 0

; (POINT は空き領域の impl_fn_point で実装済み。ここは残る未実装関数のスタブ。
;  PEN は pen_dev60 (Error 60) へ切替済みのためこの束から外した。
;  SCREEN は impl_fn_screen (7t_save_list.inc のプール)、CSRLIN は
;  impl_fn_csrlin (7t_core.s の末尾プール)、ANPORT は impl_fn_anport (7t_numchk.inc)
;  で実装済みのため、この束から外した)
impl_fn_lof:
impl_fn_eof:
                JSR     impl_eval_expr_p
                JMP     int_to_fac_zero         ; FAC1 = 0

;==============================================================================
; 文字列関数
;==============================================================================

; >>FOLD ret_tmpstr1
;------------------------------------------------------------------------------
; ret_tmpstr1 — TMPSTR の 1 文字を文字列の返り値にして関数の共通尾部へ抜ける。
;   CHR$ と INKEY$ が同じ 4 命令を並べていたので 1 本にまとめた (容量捻出)。
;   末尾は JMP なので呼出側から見た意味は元の並びと同じ。
;------------------------------------------------------------------------------
ret_tmpstr1:
                LDD     #TMPSTR
                STD     STRPTR
                LDA     #1
                STA     STRLEN
                BRA     fret_str        ; (短分岐が届く)
; <<FOLD ret_tmpstr1
impl_fn_chr_d:
                JSR     eval_p_to_int_255 ; 0..255 検査つき整数化 (範囲外は ERR 5 / 桁あふれは ERR 6)
                STB     TMPSTR
                BRA     ret_tmpstr1     ; TMPSTR の 1 文字を返す (共通化)

; impl_fn_str_d: STR$(n) — 数値を文字列に変換
; 逆向きに桁を格納し、STRPTR はバッファ内の先頭桁を指す
; 単精度は PRINT と同じ整形 (print_float) を putchar 捕捉で流用する
impl_fn_str_d:
                JSR     eval_p_num
                LDA     FACTYPE
                BEQ     ifsd_int
                JMP     pool_str_float      ; 単精度経路 (空きプールへ配置)
ifsd_int:
                JSR     impl_fac_to_int
; int_to_str: D(符号付き16bit) を 10進文字列に変換し RAM バッファへ。
;   STR$ 仕様に従い正/0 は先頭に空白、負は '-' を付す。
;   桁はバッファ末尾から逆順に書き、STRPTR=先頭桁, STRLEN=桁数+符号。
;   RAM バッファ str_conv_buf ($0600-) を使う (ROM は書込不可のため)。
int_to_str:
                BSR     int_to_str_raw
                BRA     fret_str

; num_to_str — FAC1 の数値を PRINT と同じ整形で綴りにする (一時領域は作らない)。
;   数値の後ろに付く区切りの空白は含めない。PRINT が項目の桁数で行送りを決める
;   ために使い、判定の後はこの綴りをそのまま出す (同じ数を 2 度整形しない)。
;   出力: STRPTR / STRLEN = 綴り。破壊: A,B,X,U。
num_to_str:
                LDA     FACTYPE
                BEQ     nts_int
                JMP     pool_str_float  ; 単精度/倍精度は捕捉整形 (STR$ と同じ)
nts_int:
                JSR     impl_fac_to_int
                BRA     int_to_str_raw

; int_to_str_raw — 変換だけを行い、一時領域への複写を伴わない入口。
;   PRINT の項目整形はここを使う (項目ごとに一時文字列を作らないため)。
int_to_str_raw:
                CLR     str_sign
                TSTA
                BPL     its_pos
                JSR     negd
                INC     str_sign        ; 負フラグ
its_pos:
                BSR     uint_digits     ; 桁だけを末尾から逆順に書く (X = 先頭桁)
its_sign:
                ; 符号位置
                TST     str_sign
                BEQ     its_spc
                LEAX    -1,X
                LDA     #'-'
                STA     ,X
                BRA     its_finish
its_spc:
                LEAX    -1,X
                LDA     #' '            ; 正/0 は先頭空白
                STA     ,X
its_finish:
                ; STRPTR = X, STRLEN = (str_conv_buf+8) - X
                STX     STRPTR
                LDD     #str_conv_buf+8
                SUBD    STRPTR
                STB     STRLEN
                RTS

; uint_digits — D (符号なし 16bit) の 10 進の桁を str_conv_buf の末尾から逆順に
;   書く。出力: X = 先頭桁のアドレス。破壊: A,B,U。
;   綴りを作る経路 (STR$ / PRINT の項目整形) と、10 進をそのまま出す経路
;   (print_uint16) が同じ 1 本を使う (同じ桁分解を 2 か所に置かない)。
uint_digits:
                LDX     #str_conv_buf+8 ; 末尾から書く
                ADDD    #0              ; D = 0 か (CMPD #0 と同じフラグで 1 バイト短い)
                BNE     ud_loop
                LEAX    -1,X            ; 値=0
                LDA     #'0'
                STA     ,X
                RTS
ud_loop:
                ; D != 0 の間: D mod 10 を末尾へ
                LDU     #0              ; 商
ud_div10:
                CMPD    #10
                BLO     ud_div10_done
                SUBD    #10
                LEAU    1,U
                BRA     ud_div10
ud_div10_done:
                ; B = 余り (0-9), U = 商
                ADDB    #'0'
                LEAX    -1,X
                STB     ,X              ; 桁を格納
                TFR     U,D             ; D = 商
                ADDD    #0              ; D = 0 か (CMPD #0 と同じフラグで 1 バイト短い)
                BNE     ud_loop
                RTS

; fret_str: 文字列型を返す関数群の共通尾部 (FACTYPE=3 を立てて復帰)。
;   同一列 (LDA #3 / STA FACTYPE / RTS) が 13 箇所に重複していたため併合。
fret_str:
                ; 結果は必ず一時領域へ複写して所有する。固定の出力緩衝を
                ;   1 個しか持たないままだと、同じ関数を 2 回使ったときに先の
                ;   結果が後の結果で書き潰される (CHR$(65)+CHR$(66) が "BB")。
                JSR     str_to_tmp
fret_str_own:
                LDA     #3
                STA     FACTYPE
                RTS

impl_fn_val:
                JSR     eval_p_str
                ; STRPTR の文字列を数値に変換する。impl_parse_num は終端文字で
                ; 停止する方式のため、そのままでは文字列長 (STRLEN) の範囲外まで
                ; 読み足してしまう (文字列連結のテンポラリ再利用で末尾直後に
                ; 数字の残骸が居ると VAL("&H8") が &H88 と解析される動作観察)。
                ; 末尾の次の 1 バイトを一時的に $00 (終端) へ置き換えてから解析し、
                ; 解析後に元の値へ復元する。対象は本文/文字列域 (RAM) に限られる。
                LDX     STRPTR
                LDB     STRLEN
                ABX                     ; X = 文字列末尾の次
                LDA     ,X
                PSHS    A,X             ; 元バイトとアドレスを退避
                CLR     ,X              ; 一時終端
                LDX     STRPTR
                JSR     impl_parse_num
                PULS    A,X
                STA     ,X              ; 元バイトを復元
                RTS

impl_fn_hex_d:
                JSR     eval_p_to_addr ; 16 進表記はアドレスと同じ規約 (-65536..65535)
                ; 値 D を保持しつつ 4 桁の 16 進を hex_buf[3..0] へ最下位から書く。
                ; 注意: hex_cnt 初期化で A(=D 上位) を壊さないよう、先に値を退避する。
                PSHS    D               ; 値を一時退避
                LDX     #hex_buf+4      ; 末尾の次。格納前に 1 バイト戻す
                LDA     #4
                STA     hex_cnt
                PULS    D               ; 値を復元
hex_lp:
                PSHS    D               ; 値を退避
                ANDB    #$0F            ; B = 下位 4 ビット
                CMPB    #10
                BLO     hex_num
                ADDB    #7              ; 10..15 は数字との差 7 を足す
hex_num:
                ADDB    #'0'
hex_store:
                STB     ,-X             ; X はループ内の書込先。呼出しを挟まない
                PULS    D               ; 値を復元
                ; D >>= 4 (4回 LSRA/RORB)
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                DEC     hex_cnt
                BNE     hex_lp
                ; 先行ゼロを抑制 (F-BASIC HEX$ は先行ゼロ無し, 値0は "0")。
                ; hex_buf[0..3] = 最上位→最下位の 4 桁。
                ; 抑制と返却は OCT$ と同一手順なので共通ルーチンへ委ねる。
                LDX     #hex_buf
                LDB     #4              ; 残り桁数
                BRA     fn_skip0_ret


; OCT$(n): 8 進文字列。oct_buf[5..0] へ最下位から書き、先行ゼロを抑制。
;   HEX$ と同様、値 D を保持しつつ生成する (LDA で D 上位を壊さない)。
impl_fn_oct_d:
                JSR     eval_p_to_addr ; 8 進表記はアドレスと同じ規約 (-65536..65535)
                PSHS    D
                LDX     #oct_buf+6
                LDA     #6
                STA     oct_cnt
                PULS    D
oct_lp:
                PSHS    D
                ANDB    #$07
                ADDB    #'0'
                STB     ,-X             ; X はループ内の書込先。呼出しを挟まない
                PULS    D
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                DEC     oct_cnt
                BNE     oct_lp
                ; 先行ゼロ抑制 (HEX$ と共通)
                LDX     #oct_buf
                LDB     #6

; fn_skip0_ret: X=桁バッファ先頭 / B=桁数。先行の '0' を最後の 1 桁まで読み飛ばし、
;   残りを文字列として返す。HEX$ と OCT$ が同一の並びを持っていたので畳んだ。
;   畳み込み前の HEX$/OCT$ の並びとコードは完全に同一 (分岐先ラベル名のみ違う)。
fn_skip0_ret:
                LDA     ,X
                CMPA    #'0'
                BNE     fs0_done        ; 非 '0' → ここから出力
                CMPB    #1              ; 最後の1桁は残す (値0対策)
                BEQ     fs0_done
                LEAX    1,X
                DECB
                BRA     fn_skip0_ret
fs0_done:
                STX     STRPTR
                STB     STRLEN
                JMP     fret_str

; eval_int_keep: 文字列レジスタ (STRPTR/STRLEN) を保ったまま数式を評価し整数化する。
;   文字列関数の第 2/第 3 引数は「文字列を保持したまま」評価しなければならないが、
;   引数の式が別の文字列関数を含むと (例 LEFT$(A$,INSTR(A$," ")-1) )、内側の
;   評価が文字列レジスタを上書きしてしまう。よって評価の前後で退避・復元する。
;   入出力: X=本文ポインタ (評価後の位置へ前進)。B=整数値の下位 8 bit。
;   破壊: A,U (呼出側は数値を B でのみ受け取る規約)。
eval_int_keep:
                LDA     STRLEN
                LDU     STRPTR
                PSHS    U,A
                JSR     eval_to_int_255 ; 0..255 検査つき整数化
                PULS    A,U
                STA     STRLEN
                STU     STRPTR
                RTS


impl_fn_space_d:
                ; 文字数は 0..255。負や 256 以上は Illegal Function Call であり、
                ; 下位バイトだけを採って黙って生成してはならない。丸めは最近接
                ; (参考書籍の整数変換規約。SPACE$(2.6) は 3 文字)。
                JSR     eval_p_to_int_255
                ; SPACE$(n) は STRING$(n," ") と同じ生成なので、固定緩衝を
                ;   持たずに STRING$ の生成部を共用する。
                PSHS    B               ; 繰り返し数
                LDA     #' '
                STA     mid_len         ; 埋め文字
                BRA    fstr_alloc

; STRING$(n, c) : 文字 c を n 回繰り返した文字列。c は文字コード(数値)または
;   文字列(先頭1文字を使用)。引数は ( 数式 , 式 ) の 2 引数。
;   繰返し数は第 2 引数の式評価を跨いで生きるため、DP 一時ではなくハードウェア
;   スタックへ置く (DP 一時は FAC2 の内側で、式評価で必ず壊れる)。埋め文字
;   mid_len($5F) は最後の式評価より後にだけ書くので DP のままでよい。
impl_fn_strng_d:
                JSR     eval_to_int_255_ac
                PSHS    B               ; 繰り返し数 (0-255) を退避
                JSR     eval_expr_ac
                JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
                BEQ     fstr_from_str
                JSR     impl_fac_to_int
                TSTA                    ; 埋め文字コードは 0..255 のみ受理
                LBNE    errc_5          ; 範囲外は Illegal Function Call
                STB     mid_len         ; 埋め文字コード
                BRA     fstr_fill_init
fstr_from_str:
                ; 文字列先頭の参照は U で行う (X=STRPTR に載せ替えると、
                ;   fstr_fill_init の「')' 消費」が文字列内容に対して行われ、
                ;   本文ポインタを失う)。
                LDU     STRPTR
                LDA     ,U              ; 文字列の先頭 1 文字
                STA     mid_len
fstr_fill_init:
                LDA     ,X+             ; ')'
                STX     fn_xsave        ; 引数直後を確定 (式プライマリが復元する)
                ; 固定緩衝は持たず、一時領域へ直に書く。
                ;   fret_str は結果をもう一度一時へ複写するので、ここは複写を
                ;   省く尾部 fret_str_own へ抜ける (255 文字で二重に取らない)。
fstr_alloc:
                LDB     ,S              ; 繰り返し数
                JSR     str_tmp_alloc   ; X = 書込先
                STX     STRPTR
                LDB     ,S              ; 残り回数 (退避値を覗く)
                LDA     mid_len         ; 埋め文字
fstr_fill:
                TSTB
                BEQ     fstr_fdone
                STA     ,X+
                DECB
                BRA     fstr_fill
fstr_fdone:
                PULS    B               ; 繰り返し数を回収
                STB     STRLEN
                JMP     fret_str_own

; LEFT$(s$, n): 文字列 s$ の先頭 n 文字。
;   引数は ( 文字列式 , 数式 ) の 2 引数。eval_expr_p は単一引数用で
;   ',' を ')' と誤認するため、ここで '(' / ',' / ')' を手動処理する。
impl_fn_left_d:
                BSR     fn_str_num_args ; STRPTR/STRLEN=文字列, B=数値
                ; STRLEN = MIN(B, STRLEN)
                CMPB    STRLEN
                BHS     fleft_ok        ; B >= STRLEN → そのまま
                STB     STRLEN
fleft_ok:
                JMP     fret_str

impl_fn_right_d:
                BSR     fn_str_num_args
                ; 末尾 B 文字: 開始 = STRLEN - B (負なら全部)
                PSHS    B
                LDA     STRLEN
                SUBA    ,S+
                BPL     fright_ok
                CLRA                    ; B>=STRLEN → 全部
fright_ok:
                LDX     STRPTR
                LEAX    A,X
                STX     STRPTR
                ; 新 STRLEN = STRLEN - 開始オフセット
                NEGA
                ADDA    STRLEN
                STA     STRLEN
                JMP     fret_str

; MID$(s$, start [, len]) : start は 1-origin。
;   開始位置は第 3 引数の式評価を跨いで生きるため、DP 一時ではなくハードウェア
;   スタックへ置く (DP 一時 $5E/$5F は FAC2 の内側にあり、式評価が必ず壊す)。
;   要求長 mid_len は最後の式評価より後にだけ書くので DP のままでよい。
impl_fn_mid_d:
                JSR     eval_x_str_ac
                LDA     ,X+             ; ','
                JSR     eval_int_keep   ; 整数化。文字列レジスタは保つ
                PSHS    B               ; 開始 (1-origin) を退避
                ; 長さ指定?
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     fmid_toend
                LEAX    1,X
                JSR     eval_int_keep   ; 整数化。文字列レジスタは保つ
                STB     mid_len
                BRA     fmid_close
fmid_toend:
                LDA     STRLEN
                STA     mid_len         ; 残り全部 (後で残りに丸める)
fmid_close:
                LDA     ,X+             ; ')'
                STX     fn_xsave        ; 引数直後を確定 (式プライマリが復元する)
                ; offset = start-1 (start>=1)
                PULS    B               ; 開始位置を回収 (PULS は CC を変えない)
                TSTB
                LBEQ    errc_5          ; 開始位置 0 は Illegal Function Call
                DECB                    ; B = offset
                ; offset > STRLEN なら空文字列
                CMPB    STRLEN
                BLO     fmid_inrange
                ; 範囲外 → 空
                CLR     STRLEN
                BRA     fmid_done
fmid_inrange:
                ; STRPTR += offset
                LDX     STRPTR
                ABX
                STX     STRPTR
                ; 残り = STRLEN - offset
                LDA     STRLEN
                PSHS    B               ; offset
                SUBA    ,S+             ; A = 残り
                ; STRLEN = min(mid_len, 残り)
                CMPA    mid_len
                BLO     fmid_userem     ; 残り < 要求 → 残りを使う
                LDA     mid_len         ; 要求を使う
fmid_userem:
                STA     STRLEN
fmid_done:
                JMP     fret_str

; fn_str_num_args: ( str$ , num ) を評価し STRPTR/STRLEN=文字列, B=数値(下位)
;   X は ')' の次へ前進。
fn_str_num_args:
                JSR     eval_x_str_ac
                LDA     ,X+             ; ','
                JSR     eval_int_keep   ; 整数化。文字列レジスタは保つ
                PSHS    B
                LDA     ,X+             ; ')'
                STX     fn_xsave        ; 引数直後を確定 (式プライマリが復元する)
                ; fn_xsave 未更新だと式プライマリが古い fn_xsave へ本文
                ;   ポインタを巻き戻し、PRINT LEFT$(A$,2);"X" の後続項 "X" が
                ;   失われていた (是正)。
                PULS    B,PC

; INSTR([開始位置,] haystack$, needle$) : needle$ が haystack$ 内に開始位置
;   以降で最初に現れる位置 (1-origin)。見つからなければ 0。
;   開始位置を省くと 1。空の needle$ は開始位置そのものを返す。
;   開始位置が 0 以下は Illegal Function Call、文字列長を超えると 0。
;   (参考書籍「INSTR 関数」)
;   作業: ハードウェアスタックに開始位置と haystack の STRPTR/STRLEN を
;   退避し、needle 評価後に総当たり検索する。
impl_fn_instr:
                JSR     eval_expr_ac
                JSR     pool_instr_arg1 ; B = 開始オフセット(0起点), STRPTR/STRLEN = haystack$
                PSHS    B               ; [i0]
                LDD     STRPTR
                PSHS    D               ; [hPlo][hPhi]  (S+0=hi,S+1=lo の順)
                LDA     STRLEN
                PSHS    A               ; [hLen]
                JSR     eval_x_str_ac
                LDA     ,X+             ; ')'
                STX     fn_xsave        ; 引数直後を確定 (式プライマリが復元する)
                ; needle 空なら 1
                LDA     STRLEN
                BNE     fins_search
                LEAS    3,S             ; haystack 退避を捨てる (0,S = i0)
                PULS    B
                INCB                    ; 1-origin へ
                JMP     int_to_fac_b    ; A=0 を畳んだ入口
fins_search:
                ; レイアウト: 0,S=hLen  1,S=hPtr_hi  2,S=hPtr_lo
                ; 検索開始位置 i=0..(hLen-nLen)。一致したら i+1 を返す。
                LDB     3,S             ; B = i (試行開始オフセット, 0-origin)
                CMPB    ,S              ; 開始位置が文字列長を超える → 見つからず
                BHI     fins_notfound
fins_outer:
                ; 残り (hLen - i) >= nLen か? A = hLen - i
                LDA     ,S              ; hLen
                PSHS    B               ; i を一時退避 (1 バイト)
                SUBA    ,S+             ; A = hLen - i, S を戻す
                CMPA    STRLEN          ; 残り < nLen → 見つからず
                BLO     fins_notfound
                ; haystack+i と needle を nLen バイト分だけ突き合わせる
                LDX     1,S             ; haystack ptr (1,S:2,S)
                ABX                     ; X = haystack + i
                LDU     STRPTR          ; needle ptr
                LDA     STRLEN          ; 比較長 (nLen)
fins_cmp:
                PSHS    A               ; 残り比較数を退避
                LDA     ,X+
                CMPA    ,U+
                PULS    A
                BNE     fins_next
                DECA
                BNE     fins_cmp
                ; 全一致 → 位置 = i+1
                INCB
                CLRA                    ; D = i+1
                LEAS    4,S             ; 開始位置/haystack 退避を捨てる
                JMP     impl_int_to_fac
fins_next:
                INCB                    ; i++
                BRA     fins_outer
fins_notfound:
                LEAS    4,S
                JMP     int_to_fac_zero         ; FAC1 = 0

impl_fn_inkey_d:
                ; 引数なし関数は fn_xsave (関数評価が復帰後に復元) を更新しないと
                ; 前の関数呼出の古い値へ本文ポインタが飛び、同一行の後続文 (: 以降)
                ; が読み飛ばされる (TIME$/DATE$ と同規約)。
                STX     fn_xsave
                JSR     impl_sub_inkey
                TSTA
                BEQ     iik2_none
                STA     TMPSTR
                JMP     ret_tmpstr1     ; TMPSTR の 1 文字を返す (共通化)
iik2_none:
                LDD     #TMPSTR
                STD     STRPTR
                CLR     STRLEN
                JMP     fret_str

; INPUT$(文字数[,[#]ファイル番号]) — 参考書籍。
;   文字数は 0〜255。キーボードから指定の文字数だけ受け取る (画面に表示しない。
;   コントロール文字もそのまま取り込む。BREAK キーは中断)。ファイル番号を書いた
;   ときは、開いているファイルが無いので File Not Open。
impl_fn_input:
                JSR     impl_expect_tok
                FCB     '('
                JSR     eval_to_int_255 ; B = 文字数 (範囲外は Illegal Function Call)
                PSHS    B
                JSR     skip_sp_comma
                BNE     fip_close
                LDB     #59             ; File Not Open
                JMP     impl_error_handler
fip_close:
                JSR     impl_expect_tok
                FCB     ')'
                STX     fn_xsave
                LDX     #str_work_buf   ; 取込先
fip_lp:
                TST     ,S
                BEQ     fip_done
fip_wait:
                JSR     brk_gate        ; BREAK キーは中断 (X 温存)
                JSR     impl_sub_inkey  ; A = 文字 (0 = 未入力)
                TSTA
                BEQ     fip_wait
                STA     ,X+
                DEC     ,S
                BRA     fip_lp
fip_done:
                PULS    B
                STB     STRLEN
                LDD     #str_work_buf
                STD     STRPTR
                JMP     fret_str

; impl_fn_time / impl_fn_date は 7t_time.inc へ移設 (TIME$ 実時計実装)。



; CIRCLE 頂点角の sin 定数表 前半 (j=0..8)。自前 sin コア
;   (fx_sin_core) の実出力ビット列。詳細は 7t_save_list.inc cpg_lp を参照。
c_sjtab9:
                FCB     $00,$00,$00,$00,$00,$00 ; j=0 (sin 0 = 0)
                FCB     $7C,$C8,$BD,$35,$E1,$00 ; j=1
                FCB     $7D,$C7,$C5,$C1,$E3,$00 ; j=2
                FCB     $7E,$94,$A0,$31,$77,$00 ; j=3
                FCB     $7E,$C3,$EF,$15,$35,$00 ; j=4
                FCB     $7E,$F1,$5A,$E9,$C1,$00 ; j=5
                FCB     $7F,$8E,$39,$D9,$CE,$00 ; j=6
                FCB     $7F,$A2,$67,$99,$29,$00 ; j=7
                FCB     $7F,$B5,$04,$F3,$34,$00 ; j=8
