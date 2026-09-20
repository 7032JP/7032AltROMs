; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa

; PUT@ / PUT@A のパターン送出 — 分割転送継続コマンド ($64) 方式
;   ($9722 以降の空き領域に配置。ジャンプは impl_put から LBNE)
;
; 参考書籍の格納形式: 配列上のパターンは矩形を左上から右・上から
;   下へ 1 ドット 1 ビット (MSB が左) で並べたもので、行末でバイト境界へ切上げ
;   ない連続詰めである。形式2/4 (@A) は同じ並びを B / R / G の 3 面ぶん、面ごと
;   に ceil(幅×行数/8) バイトずつ連結する。配列上ではこの 3 面が隙間なく続く。
;
; 共有 RAM のデータ領域は 1 コマンドあたり僅少 ($1C/$1E は $D38E-$D3FF の 114B)
;   しかない。事実上のハンドシェイク規約はこのために「$D38D の計数がブロック全体に
;   満たない場合、サブは受理済コマンドを清算して BUSY を落とし、続きを要求する。
;   メインは継続コマンド $64 ($D383=計数 / $D384〜=データ、1 回 124B) を書いて
;   転送を継続する」という分割転送プロトコルを備える。ここではそれを使う。
;
;   矩形を行の束へ切って $1C/$1E を複数回発行する方式は採らない。サブ側は
;   コマンドごとに面の先頭をバイト境界へ揃え直すため、幅が 8 の倍数でない矩形
;   では 2 個目以降のチャンクで面の切れ目がずれ得るうえ、1 回の PUT@ が複数の
;   コマンドに化けて事実上のハンドシェイク規約の見え方から外れる。
;------------------------------------------------------------------------------
IPB_FIRST       EQU     114                 ; $1C/$1E の領域 ($D38E-$D3FF)
IPB_CONT        EQU     124                 ; $64 の領域   ($D384-$D3FF)

iput_blk:
                LDA     gp_attr
                BEQ     ipb_enter           ; 形式1/3 は 5 種すべての動作型を採る
                ; 形式2/4 が採らない動作型を弾く (ここへ展開)
; ipu_fnchk — PUT@ 形式2/4 が採らない動作型を弾く (プール配置。@A 確定で呼ばれる)。
;   参考書籍の PUT@ 形式2/4 (属性付き) が採る動作型は
;   PSET / OR / AND / XOR で、PRESET は適用範囲外であり Illegal Function Call
;   となる (サブ CPU 側へは何も発行しない)。形式1/3 は 5 種すべてを採るので
;   呼出元 (iput_blk の @A 分岐) がこの経路へは入れない。
ipu_fnchk:
                LDA     gp_func
                DECA                        ; PRESET (=1) だけが 0 になる
                LBEQ    errc_5              ; Illegal Function Call
ipb_enter:
                PSHS    X                   ; 本文ポインタ保護 (複写で X を使う)
                ; --- 幅 W と行数 H ---
                LDD     tmpX16_2
                SUBD    tmpX16
                ADDD    #1
                STD     gp_w                ; W = X1-X0+1 (1..640)
                LDD     tmpY16_2
                SUBD    tmpY16
                INCB
                STB     gp_rows_rem         ; H = Y1-Y0+1 (1..200)
                ; --- 1 面のバイト数 nb = ceil(W*H/8) ---
                ;   W*H は 16bit に収まらない (最大 128000) ので
                ;   (W>>3)*H + ceil(((W&7)*H)/8) に分けて求める。
                LDD     gp_w
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB                        ; B = W>>3 (0..80)
                LDA     gp_rows_rem
                MUL
                STD     gp_bits
                LDB     gp_w+1
                ANDB    #$07
                LDA     gp_rows_rem
                MUL                         ; D = (W&7)*H
                ADDD    #7
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                ADDD    gp_bits
                STD     gp_bits             ; nb
                ; --- 送出総バイト数 = nb × 面数 (形式1/3 は 1、形式2/4 は 3) ---
                LDA     #$1C
                TST     gp_attr
                BEQ     ipb_cmd
                LDD     gp_bits
                ASLB
                ROLA
                ADDD    gp_bits
                STD     gp_bits             ; 3*nb
                LDA     #$1E
ipb_cmd:
                ; --- 先頭コマンド ($1C Put Block 1 / $1E Put Block 2) ---
                JSR     sub_stage_x0    ; SH_ERR/SH_CNT=0, P1=tmpX16 (共通化)
                LDD     tmpY16
                STD     SUBCMD_P3           ; Y0
                LDD     tmpX16_2
                STD     SUBCMD_P5           ; X1
                LDD     tmpY16_2
                STD     SUBCMD_P7           ; Y1
                LDA     gp_color
                STA     SUBCMD_P9           ; 色
                LDA     gp_func
                STA     SUBCMD_P10          ; 描画ファンクション
                LDX     #SUBCMD_P11         ; 計数欄 ($D38D)。データは直後から
                LDB     #IPB_FIRST
                BSR     ipb_xfer
ipb_more:
                LDD     gp_bits
                BEQ     ipb_done            ; 全バイト送出済 → 完了
                LDA     #$64                ; 分割転送の継続コマンド
                STA     SUBCMD_CMD
                LDX     #SUBCMD_P1          ; 計数欄 ($D383)。データは $D384 から
                LDB     #IPB_CONT
                BSR     ipb_xfer
                BRA     ipb_more
ipb_done:
                PULS    X,PC                ; 本文ポインタを返して復帰

;------------------------------------------------------------------------------
; ipb_xfer — 残バイト数 gp_bits のうち min(残, B) バイトを、X が指す計数欄と
;   その直後のデータ欄へ積んで 1 コマンド送出する。gp_arr_ptr と gp_bits を
;   送出ぶんだけ前進/減算する。壊す: A,B,X,U (impl_sub_send_cmd は全て保存)。
;------------------------------------------------------------------------------
ipb_xfer:
                LDA     gp_bits             ; 残の上位が非 0 → 残 >= 256 > B
                BNE     ipb_take
                CMPB    gp_bits+1
                BLS     ipb_take            ; B <= 残 → B バイト送る
                LDB     gp_bits+1           ; 残 < B → 残だけ送る
ipb_take:
                STB     ,X+                 ; 計数
                LDU     gp_arr_ptr
                PSHS    B                   ; 送出バイト数を退避
ipb_cp:
                LDA     ,U+
                STA     ,X+
                DECB
                BNE     ipb_cp
                STU     gp_arr_ptr          ; 次チャンクの読出位置
                CLRA
                LDB     ,S+
                PSHS    D
                LDD     gp_bits
                SUBD    ,S++
                STD     gp_bits             ; 残 -= 送出数
                JMP     impl_sub_send_cmd   ; 末尾呼び (レジスタは全保存される)

;==============================================================================
; iif_target — IF cond THEN/ELSE 直後の暗黙 GOTO 判定 (impl_if から JMP)
;   `IF cond THEN 行番号` は `THEN GOTO 行番号` 等価。ディスパッチャは文頭の裸の
;   行番号を読み飛ばすだけで暗黙 GOTO を行わないため、ここで判定してジャンプする。
;   行番号でなければ後続を文として実行させるべく単純復帰する。
;   (impl_if 本体の ZMB 予算に収まらないため空き領域へ配置。)
;==============================================================================
iif_target:
                JSR     skip_sp_x       ; 先行空白を読み飛ばす (A=最初の非空白)
                CMPA    #'0'
                BLO     iift_ret        ; 数字でない → 文/行末 → 復帰
                CMPA    #'9'
                BHI     iift_ret
iift_goto:
                JSR     impl_eval_linenum
                JSR     impl_find_line
                BEQ     iift_undef
                JMP     impl_jump_to_line
iift_undef:
                JMP     errc_8        ; Undefined line
iift_ret:
                RTS

                include "7t_double5.inc"        ; 倍精度リテラル解析 (プール)

;==============================================================================
; is_implicit_let: 暗黙 LET 確定判定 (非破壊ピーク)
;   入力: A = 文の先頭1文字 (呼び元で 'A'..'Z' と確認済み),
;         X = その1文字の直後を指す。
;   変数名の残り (英数字2文字目 + 型サフィックス %!#$) を読み飛ばした位置に
;   '=' トークン (TOK_EQ) が続くときだけ Z=1 を返す (= 真の代入文)。
;   それ以外は Z=0。X/A は破壊しない (呼び元の状態を保つ)。
;   これにより英字始まりでも代入でない本文を impl_let に渡して
;   Syntax Error を出す事故を防ぐ。空き領域に配置。
;==============================================================================
is_implicit_let:
                PSHS    A,X             ; A/X を退避 (非破壊)
                LDA     ,X              ; 2文字目
                CMPA    #'A'
                BLO     iil_chk_dig
                CMPA    #'Z'
                BLS     iil_n2
                BRA     iil_suffix
iil_chk_dig:
                CMPA    #'0'
                BLO     iil_suffix
                CMPA    #'9'
                BHI     iil_suffix
iil_n2:
                LEAX    1,X
                ; 3 文字目以降の英数字も読み飛ばす。ここは暗黙 LET かどうかの
                ;   判定 ('=' の位置探し) のみで、名前の有効長 (先頭 16 文字)
                ;   には依存しない。スキップを怠ると "TITLE$=" が暗黙 LET と
                ;   認識されない (parse_var_name 側と対称に処理する)。
iil_n2skip:
                LDA     ,X
                CMPA    #'A'
                BLO     iil_n2skip_dig
                CMPA    #'Z'
                BHI     iil_suffix
                LEAX    1,X
                BRA     iil_n2skip
iil_n2skip_dig:
                CMPA    #'0'
                BLO     iil_suffix
                CMPA    #'9'
                BHI     iil_suffix
                LEAX    1,X
                BRA     iil_n2skip
iil_suffix:
                LDA     ,X              ; 型サフィックス %!#$ なら 1 文字消費
                CMPA    #'%'
                BEQ     iil_sfx1
                CMPA    #'!'
                BEQ     iil_sfx1
                CMPA    #'#'
                BEQ     iil_sfx1
                CMPA    #'$'
                BNE     iil_eqchk
iil_sfx1:
                LEAX    1,X
iil_eqchk:
                JSR     skip_sp_lparen
                BEQ     iil_yes
                CMPA    #TOK_EQ         ; '=' トークン → 真の代入
                PULS    A,X,PC ; 退避復元 (Z は CMPA を保持)
iil_yes:
                ORCC    #$04            ; Z=1 (代入として扱う)
                PULS    A,X,PC

; (共用の TO 句解析 impl_parse_for_to は、整数側が fsi_parse_to へ、単精度側が
;  fsg_parse_to へそれぞれ移って参照が無くなったので撤去した。)

;------------------------------------------------------------------------------
; for_setup_int — 整数 (%) 制御変数の FOR を構築 (impl_for から LBEQ で入る)。
;   X = 変数名の直後 / for_var_ptr = 制御変数の値先頭。復帰時 X = ループ本体先頭。
;
;   制御変数への代入は、初期値・終値・増分をすべて整数化し終えてから行う。
;     初期値を先に書き込むと、終値や増分が整数の範囲を外れて Overflow で
;     止まるとき (FOR I%=5 TO 32769 など)、止まったにもかかわらず制御変数
;     だけが書き換わってしまう。参考書籍の FOR〜NEXT は「繰返しが
;     成立して初めて制御変数が初期値になる」ので、成立しない経路では
;     書き換えない。整数化した 3 値は S へ順に退避する。
;------------------------------------------------------------------------------
for_setup_int:
                JSR     impl_expect_tok
                FCB     TOK_EQ          ; = トークン (トークナイザが '=' を変換済み)
                JSR     impl_eval_expr  ; 初期値 → FAC1
                ; 初期値も整数へ丸める。範囲外は impl_fac_to_int_round が
                ; Overflow(6) を出す。この時点ではまだ制御変数へ書かない。
                JSR     impl_fac_to_int_round   ; D = 初期値 (整数)
                PSHS    D
                ; TO 句解析。欠落 (テープ由来の不完全な FOR 行: 'FOR I=n' のみで
                ; TO/本体/NEXT を欠く) のときは、退避した初期値ごと捨ててこの文を
                ; 読み飛ばす (参考書籍に準じた寛容動作。fsi_noto)。
                BSR     fsi_parse_to            ; TO 有→終値を FAC1 にして復帰
                JSR     impl_fac_to_int_round   ; D = 終値 (整数)
                PSHS    D
                JSR     skip_sp_x
                CMPA    #TOK_STEP       ; STEP
                BNE     fsi_defstep
                JSR     eval_expr_x1
                JSR     impl_fac_to_int_round   ; D = 増分 (整数。2.6 → 3)
                BRA     fsi_push
fsi_defstep:
                LDD     #1                      ; 増分 = 1
fsi_push:
                ; impl_push_for は 終値=FAC1+2 / 増分=FAC2+2 を読む。
                STD     FAC2+2          ; FAC2 ← 増分
                PULS    D               ; D = 退避した終値
                STD     FAC1+2          ; FAC1 ← 終値
                PULS    D               ; D = 退避した初期値
                LDU     for_var_ptr     ; ここで初めて制御変数へ代入する
                STD     ,U
                STX     CURIP           ; ループ先頭 IP = FOR 解析完了直後の位置 (: または本体トークン)
                ; 本実装は終了判定を NEXT 側で行う (本体は最低1回実行)。
                ; よって FOR 入口でのゼロ周回スキップは行わない。
; impl_push_for: FOR ループ情報を枠列へ積む
;   枠 (15B): [種別][var_addr(2)][終値(4)][増分(4)][IP(2)][行レコード(2)]
;   確保 (空きの検査 + 同一制御変数の再利用 + var_addr 格納) は fss_alloc に統合。
impl_push_for:
                JSR     fss_alloc       ; 確保。U = 枠+3 (終値格納先)
                TFR     U,X
                LDU     #FAC1           ; 終値 (FAC1 の先頭 4B)
                JSR     ipf_cp4
                LDU     #FAC2           ; 増分 (FAC2 の先頭 4B)
                JSR     ipf_cp4
                ; ループ先頭 IP
                LDD     CURIP
                STD     ,X++
                LDD     CURLINE_PTR
                STD     ,X
                ; push_for は X を枠の作業ポインタに破壊する。ディスパッチャは
                ; ハンドラ復帰時の X を次 IP とするため、本体先頭 (CURIP) を X に戻す。
                LDX     CURIP
                RTS

; fsi_parse_to: for_setup_int 専用の TO 句解析。TO トークンがあれば後続式を評価し
;   終値を FAC1 へ入れて復帰。TO 欠落時は捨てる量に注意 (呼出側が初期値 2B を
;   S へ積んでいる)。
fsi_parse_to:
                JSR     skip_sp_x
                CMPA    #TOK_TO         ; TO
                BNE     fsi_noto
                JMP     eval_expr_x1
fsi_noto:
                ; 自身の戻りアドレス (2B) と退避した初期値 (2B) を捨て、
                ; この文を読み飛ばしてディスパッチャへ直接復帰する。
                LEAS    4,S
                JMP     impl_stub       ; ':'/$00 まで X を進め RTS

;------------------------------------------------------------------------------
; for_setup_sng — SNG (実数) 制御変数の FOR を構築 (impl_for から JMP で入る)。
;   X = 変数名の直後 / for_var_ptr = 変数実アドレス | $8000 (bit15 = SNG 印)。
;   終値/増分は 内部形式 でレコードへ格納。復帰時 X = ループ本体先頭 IP。
;
;   制御変数への代入は、初期値・終値・増分をすべて評価し終えてから行う。
;     初期値を先に変数へ書き込むと、(1) 終値・増分の式が制御変数を参照する
;     場合に旧値ではなく初期値で評価され周回数が変わり (FOR K=1 TO K-3 の類)、
;     (2) 終値・増分が範囲外で Overflow (6) により止まるときも制御変数だけが
;     書き換わってしまう。参考書籍の FOR〜NEXT は「終値・増分を
;     制御変数の代入前の値で評価し、繰返しが成立して初めて制御変数が初期値に
;     なる」。単精度の値は 内部形式 4 byte なので、初期値と終値は 4 byte ずつ S へ
;     退避する。レコード確保 (fss_alloc) も 3 値の評価完了後に行い、増分の
;     評価が誤りで止まった場合に評価途中のレコードが残らないようにする。
;------------------------------------------------------------------------------
for_setup_sng:
                JSR     impl_expect_tok
                FCB     TOK_EQ          ; = トークン (トークナイザが '=' を変換済み)
                JSR     impl_eval_expr  ; 初期値 → FAC1
                LEAS    -4,S
                TFR     S,U
                JSR     pack_mbf        ; 初期値 内部形式 → 0..3,S (まだ変数へは書かない)
                ; TO 句解析。欠落 (テープ由来の不完全な FOR 行) のときは、退避した
                ; 初期値ごと捨ててこの文を読み飛ばす (整数側 fsi_noto と同じ寛容動作)。
                BSR     fsg_parse_to    ; TO 有→終値を FAC1 にして復帰
                LEAS    -4,S
                TFR     S,U
                JSR     pack_mbf        ; 終値 内部形式 → 0..3,S
                JSR     skip_sp_x
                CMPA    #TOK_STEP       ; STEP
                BNE     fsg_defstep
                JSR     eval_expr_x1
                BRA     fsg_have

fsg_defstep:
                JSR     impl_load_1     ; 増分=1 → FAC1
fsg_have:
                ; 3 値の評価完了。ここからレコード確保と格納 (誤りで戻らない処理は無い)
                JSR     fss_alloc       ; レコード確保, U = レコード+2 (終値格納先), X 温存
                PULS    D               ; 退避した終値 内部形式 (上位 2B)
                STD     ,U
                PULS    D               ; 同 (下位 2B)
                STD     2,U             ; 終値 → レコード[2-5]
                LEAU    4,U             ; U = レコード+6 = 増分格納先
                JSR     pack_mbf        ; 増分 (FAC1) → レコード[6-9]
                LDD     for_var_ptr
                ANDA    #$7F            ; bit15 の SNG 印を外し実アドレスへ
                TFR     D,U
                PULS    D               ; 退避した初期値 内部形式 (上位 2B)
                STD     ,U              ; ここで初めて制御変数へ代入する
                PULS    D               ; 同 (下位 2B)
                STD     2,U
                STX     CURIP           ; ループ先頭 IP = FOR 解析完了直後の位置
                LDU     EXEC_SP         ; U = 枠 (fss_alloc が積んだ枠列の先頭)
                LDD     CURIP
                STD     11,U
                LDD     CURLINE_PTR
                STD     13,U
                LDX     CURIP
                RTS

; fsg_parse_to: for_setup_sng 専用の TO 句解析。fsi_parse_to と同じ判定だが、
;   TO 欠落時に捨てる量が違う (呼出側が初期値 内部形式 4B を S へ積んでいる)。
fsg_parse_to:
                JSR     skip_sp_x
                CMPA    #TOK_TO         ; TO
                BNE     fsg_noto
                JMP     eval_expr_x1
fsg_noto:
                ; 自身の戻りアドレス (2B) と退避した初期値 (4B) を捨て、
                ; この文を読み飛ばしてディスパッチャへ直接復帰する。
                LEAS    6,S
                JMP     impl_stub       ; ':'/$00 まで X を進め RTS

;==============================================================================
; 実装コード群（以降に配置）
; 固定アドレス制約なし。実装を自由に配置可能。
;==============================================================================

;==============================================================================
; 配列サブシステム (DIM / 配列参照) — 空き領域に再配置
;==============================================================================
; (旧名の配列要素アドレス取得入口は参照が無い死蔵コードだったので撤去した)

; >>FOLD sub_stage_x0
;------------------------------------------------------------------------------
; sub_stage_x0 — サブ CPU 側コマンドの組み立て出だし。A = コマンド。SH_ERR/SH_CNT を
;   0 にし、tmpX16 を第 1 引数へ置く。PUT@ の矩形送出 (7t_arrays) と DRAW の
;   矩形送出 (7t_deffn) が同じ 5 命令を並べていたので 1 本にまとめた
;   (容量捻出)。破壊: A,B (D)。
;------------------------------------------------------------------------------
sub_stage_x0:
                STA     SUBCMD_CMD
                CLRA
                CLRB
                STD     SUBSTG_BASE     ; $FC80 SH_ERR = 0 / $FC81 = 0
                LDD     tmpX16
                STD     SUBCMD_P1
                RTS
; <<FOLD sub_stage_x0

; ---- 配列エントリ物理形式 (相互運用要件の上方確保レイアウト) ----
;   [type|namelen][名前 1..16B][len(2B)][ndim(1B)][s0(2B)]..[s(n-1)(2B)][要素...]
;   len = len 欄先頭から エントリ末尾までのバイト数 = 3 + 2*ndim + 要素バイト
;   (動作観察で確かめた「次エントリへのオフセット」と同じ意味・同じ値。エントリ総バイト数は
;    1 + 名前長 + len。名前欄を可変長へ改めたため、名前長に依らず
;    動作観察どおりの記録域の大きさになる)
;   配列は ARYTAB から STREND へ上方に並ぶ。


; (vn_copy は容量の都合で別の区画の空きプール (7t_arr_vncopy.inc) へ移した。
;  arr_hdrskip も同じ理由で 7t_expr.inc の空きプールへ移設済)

; arr_find: VNPTR/VNLEN/VARTYP 一致の配列エントリを探す。
;   出力: 見つかれば arr_entry=エントリ先頭, Z=0。無ければ Z=1。
;   破壊: A, B, X, U (U は全呼出元 = arr_resolve / igc_arrname / impl_erase /
;   ida_arr のいずれでも非生存であることを確認済)。
arr_find:
                LDX     ARYTAB
afnd_lp:
                CMPX    STREND
                BHS     afnd_miss
                ; X = エントリ先頭。ヘッダ (型|名前長) と名前本体を比較
                LDA     VNLEN
                ORA     VARTYP
                CMPA    ,X
                BNE     afnd_skip
                TFR     X,U             ; U は arr_find の呼出元で非生存 (破壊可)
                JSR     vn_cmp          ; U+1 から VNLEN バイトを VNPTR と比較
                BNE     afnd_skip
                STX     arr_entry
                ANDCC   #$FB            ; Z=0
                RTS
afnd_skip:
                ; 次エントリ = len 欄先頭 + len
                JSR     arr_hdrskip
                LDD     ,X
                LEAX    D,X
                BRA     afnd_lp
afnd_miss:
                ORCC    #$04            ; Z=1
                RTS

; arr_entry_size: X=配列エントリ先頭 → D=総バイト数 (ヘッダ+要素)。X 保存。
;   ヘッダの ndim/型を arr_ndim/VARTYP に、要素サイズを arr_elsz に読み込む。
;   次元欄はエントリ上をそのまま読む方式なのでここでは複写しない。
arr_entry_size:
                PSHS    X
                LDA     ,X
                ANDA    #$C0
                STA     VARTYP
                JSR     arr_hdrskip     ; X = len 欄先頭
                LDA     2,X             ; ndim
                STA     arr_ndim
                LDD     ,X              ; len 欄
                LEAX    D,X             ; X = エントリ末尾 (len 欄先頭 + len)
                TFR     X,D
                SUBD    ,S              ; D = 総バイト数 (= 1 + 名前長 + len)
                PSHS    D               ; arr_elsize が D を壊すため退避
                BSR     arr_elsize      ; VARTYP → arr_elsz (B)
                PULS    D,X,PC ; D = 総バイト数, X = エントリ先頭を復元

; ---------------------------------------------------------------------------
; impl_erase: ERASE 命令 (配列解放)
;   ERASE NAME[,NAME2...] : 指定した配列を配列テーブル (ARYTAB..STREND) から
;   物理的に取り除き、上位のエントリを前方へ詰めて STREND を縮める。除去後は
;   同じ名前の配列を再度 DIM できる (再 DIM 時の二重定義エラーを避ける)。
;   スカラ変数領域 (VARTAB..ARYTAB) や文字列ヒープには触れない (動作観察による ERASE と
;   同じく配列ストレージのみを解放する。参考書籍)。未定義配列に対する
;   ERASE は害を出さず素通りする (安全側)。
;   呼出規約 (二次コマンド: cmd2_hdl_tbl 経由の JSR ,X): 本文ポインタ (ERASE
;   トークン直後) は 2,S に退避されて渡り、読了位置は 2,S へ書き戻す
;   (impl_play / impl_chain と同一規約。JMP [B,X] で X は破壊されるため X 渡し
;    ではない)。
;   一時値は CPU スタックのみに置く (新規ゼロページを確保しない)。
;   本ルーチンは長いため コード領域を避け、配列サブシステムと同じ
;   空き領域に配置する。
; ---------------------------------------------------------------------------
impl_erase:
ers_lp:
                LDX     2,S             ; X = 本文ポインタ (CURIP)
                JSR     parse_var_name  ; VNPTR/VNLEN/VARTYP 設定、X=名前直後
                STX     2,S             ; 読了位置を退避 (arr_find/コピーが X を壊す)
                BSR     arr_find        ; 内部で LDX ARYTAB により X を破壊する
                BEQ     ers_next        ; Z=1 (未発見) → 何もせず次の名前へ
                ; --- 発見: arr_entry のエントリを除去する ---
                LDX     arr_entry       ; X = エントリ先頭 (= 前方詰めの宛先)
                BSR     arr_entry_size  ; D = エントリ総バイト数 (size), X 保存
                LEAY    D,X             ; Y = 源 = エントリ先頭 + size
                TFR     X,U             ; U = 宛先 = エントリ先頭
                PSHS    D               ; size を退避  [S: size, ret, CURIP...]
                ; 前方へ動かすバイト数 = STREND - 源
                LDD     STREND
                PSHS    Y               ; [S: 源, size, ...]
                SUBD    ,S++            ; D = STREND - 源 (源をポップ)
                ; --- 前方詰め (宛先 < 源 なので低位→高位で安全) ---
                TFR     D,X             ; X = 残バイト数 (X は TFR X,U 済で空き)
ers_cpy:
                LEAX    0,X             ; LEAX は Z を立てる (CMPX #0 より 1B 短い)
                BEQ     ers_cpy_done
                LDA     ,Y+
                STA     ,U+
                LEAX    -1,X
                BRA     ers_cpy
ers_cpy_done:
                ; STREND -= size (空き領域が size 分だけ増える)
                LDD     STREND
                SUBD    ,S              ; S 先頭 = size
                STD     STREND
                LEAS    2,S             ; size をポップ (スタックを入口フレームへ復帰)
ers_next:
                LDX     2,S             ; X = 読了位置 (CURIP) を再ロード
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     ers_ret
                LEAX    1,X             ; ',' を読み飛ばし次へ
                STX     2,S             ; 読了位置を書き戻し
                BRA     ers_lp
ers_ret:
                RTS                     ; 2,S は最新の読了位置を保持済

; arr_elsize: VARTYP → 要素サイズ (B), arr_elsz にも保存。
arr_elsize:
                LDA     VARTYP
                LSRA
                LSRA
                LSRA
                LSRA
                LSRA
                LSRA            ; A = 型インデックス 0-3
                LDX     #var_size_tbl
                LDB     A,X
                STB     arr_elsz
                RTS

; arr_elem_bytes: arr_ndim/次元表 (arr_dptr)/VARTYP から 要素総バイト数を D に返す。
arr_elem_bytes:
                BSR     arr_elsize
                JSR     arr_total_elems
                STD     arr_mul_a
                LDB     arr_elsz
                CLRA
                STD     arr_mul_b

; arr_mul: D = arr_mul_a * arr_mul_b (符号無, 反復加算)。
;   入力: arr_mul_a(2B), arr_mul_b(2B)。出力: D=積。乗数 b は小さい前提。
;   破壊: X (積の計数に使う。全呼出元が退避済)。
;   積が 16bit を超えたら Subscript Out Of Range (9) とする。溢れを黙って
;     捨てると、確保量が 65536 を跨ぐ DIM (例 DIM A%(255,255) は 131072 byte、
;     DIM B#(8191) は 65536 byte) が「要素数 0 の器」として成立し、以後の
;     要素書込みが器の外へ出る。同じ宣言が誤りになることは動作観測 (動作観察) で
;     確認した。
arr_mul:
                CLRA                    ; 積
                CLRB
amul_lp:
                LDX     arr_mul_b
                BEQ     amul_done
                LEAX    -1,X
                STX     arr_mul_b
                ADDD    arr_mul_a       ; 積 += a
                BCS     amul_ovf
                BRA     amul_lp
amul_done:
                RTS
amul_ovf:
                JMP     arr_err_sor     ; Subscript Out Of Range

;------------------------------------------------------------------------------
; vars_make_room: 新スカラ変数 1 個分の隙間を ARYTAB 位置に空ける。
;   相互運用要件レイアウトでは配列がスカラ変数の直後に密着しているため、
;   スカラ新規作成時は配列ブロック [ARYTAB..STREND) を上へ移動する
;   (動作観察どおりの挙動。動作観察でもスカラ追加時に配列実体が
;    エントリサイズ分だけ上へ動く)。
;   入力: vnw_size = エントリバイト数 (名前2B+値)。
;   出力: U = 旧 ARYTAB (新エントリ書込先)。STREND 更新済。
;   ARYTAB の更新は呼び元 (書込完了後の gvr_update_arytab) が行う。
;   破壊: A,B,X,U (Y は非破壊 — 旧 gvr_new の「スカラ作成は Y を壊さない」
;   契約を維持する。式評価/ディスパッチャが Y を保持したまま変数参照する
;   ため、Y を壊すと再帰 GOSUB 等が暴走する実害を確認済み)。
;   OM 時は impl_error_handler へ飛ぶ (復帰しない)。
;------------------------------------------------------------------------------
vars_make_room:
                CLRA
                LDB     vnw_size
                ADDD    STREND
                BCS     vmr_oom         ; 16bit 折返し → OM
                STD     ARR_NEWEND      ; 新 STREND
                JSR     alloc_limit     ; D = S − STK_RESERVE
                CMPD    ARR_NEWEND
                BLO     vmr_oom         ; 上限 < 新末尾 → OM
                ; 配列ブロックを上端から後ろ向きに複写 (領域が重なるため)
                LDX     STREND          ; src (旧上端)
                LDU     ARR_NEWEND      ; dst (新上端)
vmr_lp:
                CMPX    ARYTAB
                BLS     vmr_done        ; X が ARYTAB まで下りたら完了
                LDA     ,-X
                STA     ,-U
                BRA     vmr_lp
vmr_done:
                LDD     ARR_NEWEND
                STD     STREND
                ; 生存中の代入先が配列ブロック内なら移動量ぶん追従させる。
                ; ここでの ARYTAB はまだ旧値 (呼び元が書込後に進める) なので、
                ; 旧 ARYTAB 以上を指すものが移動対象である。
                LDD     let_tgt
                CMPD    ARYTAB
                BLO     vmr_nofix       ; スカラ域は動かないので触らない
                ADDB    vnw_size
                ADCA    #0
                STD     let_tgt
vmr_nofix:
                LDU     ARYTAB          ; U = 新エントリ書込先
                RTS
vmr_oom:
                JMP     err_rg          ; Out Of Memory
