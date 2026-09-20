; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 数値の値域検査つき整数化 と 整数化関数の本体
;
;   参考書籍の規定に沿って「整数を要求する文脈」で採れる値域を
;   入口で検査する。値域を外れた実引数は Illegal Function Call とし、
;   下位バイトだけを黙って採る (= 256 で折り返す) ことをしない。
;     ・文字列関数の文字数/位置         … 0..255
;     ・アドレスを要求する文脈               … -65536..65535 (ゼロ方向切り捨て)
;       (DEF USR/EXEC/LOADM/SAVEM/CLEAR のアドレス・基数変換・図形の水平位置・
;        半径・桁・演奏文の変数参照。範囲外は Illegal Function Call)
;     ・POKE の書込みアドレス / PEEK の式 … 0..65535 (ゼロ方向切り捨て)
;       (参考書籍が両者にだけ値域を明記している)
;     ・POKE の値                       … 0..255 (最近接丸め)
;   あわせて INT / FIX を「整数の範囲に収まらない値でも巻き戻らない」実装に
;   する。収まる値は整数型、収まらない値は実数型のまま端数だけを
;   落とす。倍精度は降格せずに扱う (降格すると有効桁が 24 bit に落ちて
;   INT(1.23456789D8) のような値が崩れる)。
;
;   本ファイルは全てラベル参照のみで位置依存が無く、空きプールに
;   置く (各是正箇所の属する区画には余地が無いため)。
;==============================================================================

;------------------------------------------------------------------------------
; err_ifc — Illegal Function Call (ERR 5) の近距離中継。
;   誤り番号そのものは共通スタブ errc_5 だけが持つ。当プールから
;   errc_5 までは短分岐が届かないので、ここへ 3 byte の中継を 1 本置いて
;   各判定は `BNE err_ifc` (2 byte) で済ませる。
;------------------------------------------------------------------------------

;------------------------------------------------------------------------------
; 実引数の型検査 (参考書籍「関数」「型」)
;
;   数値を要求する文脈へ文字列を渡した式、文字列を要求する文脈へ数値を渡した
;   式は Type Mismatch (ERR 13) で打ち切る。標準はこれを誤りとして表に出し、
;   ON ERROR GOTO でも捕まえられる。評価した値の型を見ずに関数へ渡すと、
;   ABS("A") が引数の文字列をそのまま印字する、LEN(5) が 0 を返す、といった
;   「意味のない値のまま処理が進む」振舞いになる。
;
;   検査は関数ごとに書き足すのではなく、引数評価の共通入口を型付きの入口へ
;   差し替える形で入れる (呼出側は JSR の飛び先が変わるだけで 1 byte も増えない)。
;     eval_p_num … fn(数式)   を評価。文字列なら Type Mismatch
;     eval_p_str … fn(文字列式) を評価。数値なら Type Mismatch
;     eval_x_str … 括弧を伴わない位置での文字列引数 (LEFT$ 等の第 1 引数)
;   FACTYPE は 0=整数 / 1=単精度 / 2=倍精度 / 3=文字列。
;   err_tm は当プールから err_tmm へ届かせるための 3 byte の近距離中継。
;------------------------------------------------------------------------------
err_tm:
                JMP     err_tmm
chk_str:
                JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
                BNE     err_tm
                RTS
eval_p_num:
                JSR     impl_eval_expr_p
                BRA     chk_num
eval_p_str:
                JSR     impl_eval_expr_p
                BRA     chk_str
chk_num:
                JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
                BEQ     err_tm
                RTS
eval_x_str_ac:                          ; 1 文字取り込んでから文字列を評価
                LDA     ,X+
eval_x_str:
                JSR     impl_eval_expr
                BRA     chk_str

;------------------------------------------------------------------------------
; eval_to_int_255 — 式を評価し 0..255 の整数を得る。
;   丸めは最近接 (整数を要求する文脈の規約)。範囲外は Illegal Function Call。
;   入出力: X=本文ポインタ (評価後の位置へ前進)。出力 A=0, B=値。
;------------------------------------------------------------------------------
eval_to_int_255_ac:                     ; 1 文字取り込んでから 0..255 の整数を得る
                LDA     ,X+
eval_to_int_255:
                JSR     eval_to_int_round
                TSTA
                BNE     eta_e5
                RTS

;------------------------------------------------------------------------------
; eval_p_to_int_255 — 括弧付き式 fn(expr) の場合の 0..255 整数化。
;   impl_eval_expr_p は '(' / ')' の消費と fn_xsave の確定まで行う契約。
;------------------------------------------------------------------------------
eval_p_to_int_255:
                BSR     eval_p_num      ; 文字列を渡されたら Type Mismatch
                JSR     impl_fac_to_int_round
                TSTA
                BNE     eta_e5
                RTS

;------------------------------------------------------------------------------
; eval_to_int_15 / eval_to_int_7 — 図形文の色引数の値域検査つき整数化。
;   丸めてから範囲を検査し、範囲外は Illegal Function Call とする。
;   CMPD は符号なし比較なので負値も同じ 1 回の判定で捕まる (COLOR 文と同じ型)。
;   動作観察で確かめた値域:
;     PSET / LINE / CIRCLE / CONNECT / SYMBOL のパレットコード … 0..15
;     PAINT の塗色・境界色 (何個並べても各々)                  … 0..15
;     COLOR= のパレットコード                                  … 0..7
;     COLOR= のカラーコード                                    … 0..15
;   丸めが先で検査が後 (動作観察: PSET(x,y,-.5) は色 0 で受理、-.51 は誤り)。
;------------------------------------------------------------------------------
fac1_shr32:                             ; FAC1 仮数 32bit を 1 桁右へ
                LSR     FAC1+1
                ROR     FAC1+2
                ROR     FAC1+3
                ROR     FAC1+4
                RTS
fac1_shl32:                             ; FAC1 仮数 32bit を 1 桁左へ
                ASL     FAC1+4
                ROL     FAC1+3
                ROL     FAC1+2
                ROL     FAC1+1
                RTS
eval_to_int_15_x1:                      ; 1 進めてから 0..15 の整数を得る
                LEAX    1,X
eval_to_int_15:
                JSR     eval_to_int_round
                CMPD    #15
                BHI     eta_e5
                RTS




;------------------------------------------------------------------------------
; eval_to_addr     — アドレスを要求する文脈の整数化 (POKE ほか)。
; eval_p_to_addr   — 同じ判定の括弧付き式 fn(expr) の場合 (PEEK / HEX$ / OCT$ ほか)。
;   ゼロ方向切り捨て (動作観察: POKE 30000.6 は 30000 アドレスへ書く)。
;   -65536..65535 の外は Illegal Function Call。
;   単精度の値域判定は指数で行う: 値は [2^(exp-128), 2^(exp-127)) なので
;   |v| < 65536 ⇔ exp <= 143。exp=144 は -65536 ちょうどのみ通す。
;------------------------------------------------------------------------------
eval_p_to_addr:
                JSR     impl_eval_expr_p
                BRA     eta_chk
eval_to_addr_x1:                        ; 1 進めてからアドレスを得る
                LEAX    1,X
eval_to_addr:
                JSR     impl_eval_expr
; impl_fac_to_addr — 評価済みの FAC1 に同じ規約を当てる入口 (式評価を伴わない
;   経路。MML の変数参照など、値が既に FAC1 にある場合に使う)。
impl_fac_to_addr:
eta_chk:
                BSR     chk_num         ; 文字列を渡されたら Type Mismatch
                JSR     dbl_demote      ; 倍精度は単精度へ落としてから判定
                LDA     FACTYPE
                BEQ     eta_int
                LDA     FAC1
                BEQ     eta_conv        ; 値 0
                CMPA    #143
                BLS     eta_conv        ; |v| < 65536
                CMPA    #144
                BNE     eta_e5
                TST     FAC1+5          ; exp=144: 負の
                BPL     eta_e5
                LDD     FAC1+1          ; 仮数がちょうど $80000000 (= -65536)
                CMPD    #$8000
                BNE     eta_e5
                LDD     FAC1+3
                BNE     eta_e5
eta_conv:
                JMP     float_to_int_trunc
eta_int:
                LDD     FAC1+2
                RTS
; err_ifc への近距離中継。上の判定群と下の 0..65535 検査からは err_ifc まで
;   短分岐が届かないので 1 本置き、各判定を 2 byte の短分岐で済ませる。
eta_e5:
                JMP     errc_5

;------------------------------------------------------------------------------
; eval_to_uaddr   — 0〜65535 を要求する文脈の整数化 (POKE の書込みアドレス)。
; eval_p_to_uaddr — 同じ判定の括弧付き式 fn(expr) の場合 (PEEK の式)。
;   参考書籍は POKE の書込みアドレスと PEEK の式の値を 0〜65535 と
;   定める。負の値は範囲外なので Illegal Function Call (参考書籍の
;   エラーコード 05) とする。
;   値域以外の扱い (ゼロ方向切り捨て・型検査) は eval_to_addr と同じ。
;   符号の判定は変換後の D では行えない (-1 も 65535 も $FFFF になる)。
;     整数型 … D の bit15 がそのまま符号 (整数型の値は -32768..32767)
;     単精度 … FAC1+5 が符号バイト (値ゼロのときは 0)
;   どちらも変換 (float_to_int_trunc) で壊れないので、変換後に見てよい。
;------------------------------------------------------------------------------
eval_p_to_uaddr:
                BSR     eval_p_to_addr
                BRA     etu_post
eval_to_uaddr:
                BSR     eval_to_addr
etu_post:
                TST     FACTYPE
                BNE     etu_flt
                TSTA                    ; 整数型: D の bit15
                BRA     etu_neg
etu_flt:
                TST     FAC1+5          ; 単精度: 符号バイト
etu_neg:
                BMI     eta_e5          ; 負 → Illegal Function Call
                RTS


                ; ゼロ方向切り捨て本体 (プール) (ここへ展開)
;------------------------------------------------------------------------------
; pool_fn_fix_trunc — FIX(x): ゼロ方向切り捨て
; pool_fn_int_floor — INT(x): 床関数 (負で端数ありなら 1 を引く)
;   動作観察: INT(-2.5)=-3 / INT(-.5)=-1 / FIX(-40000.7)=-40000 / INT(40000.7)=40000
;   0,S に置く床フラグ (0=FIX / 1=INT) で 2 つの入口を共用する。
;------------------------------------------------------------------------------
pool_fn_fix_trunc:
                CLRB                    ; 0 = ゼロ方向切り捨て
                BRA     pfif_entry

pool_fn_int_floor:
                LDB     #1              ; 1 = 床
pfif_entry:
                PSHS    B               ; 0,S = 床フラグ
                LDA     FACTYPE
                BNE     pfif_notint
                PULS    B,PC
pfif_notint:
                CMPA    #2
                LBEQ    pfif_dbl        ; 倍精度は降格せずに扱う
                LDA     FAC1
                BEQ     pfif_zero       ; 値 0
                CMPA    #143
                BHS     pfif_big        ; |v| >= 32768 → 単精度のまま端数落とし
                LDA     #159
                SUBA    FAC1            ; A = 右シフト量 (159-exp)
                CMPA    #32
                BHS     pfif_lt1        ; |v| < 1
                CLRB                    ; B = sticky (押し出しビットの OR)
pfif_rsh:
                JSR     fac1_shr32
                BCC     pfif_ns
                LDB     #1
pfif_ns:
                DECA
                BNE     pfif_rsh
                PSHS    B               ; 0,S = sticky / 1,S = 床フラグ
                LDD     FAC1+3          ; D = |trunc| 下位16bit
                TST     FAC1+5          ; 符号
                BPL     pfif_fin        ; 正 → そのまま
                COMA
                COMB                    ; D = -|trunc| - 1
                TST     ,S              ; 端数あり?
                BEQ     pfif_inc
                TST     1,S             ; 床フラグ?
                BNE     pfif_fin        ; INT かつ端数あり → -|trunc|-1 が床
pfif_inc:
                ADDD    #1              ; D = -|trunc|
pfif_fin:
                LEAS    2,S
                JMP     impl_int_to_fac
pfif_lt1:
                CLRA
                CLRB
                TST     ,S              ; 床フラグ
                BEQ     pfif_ret0       ; FIX → 0
                TST     FAC1+5
                BPL     pfif_ret0
                LDD     #-1             ; 負の真小数 → -1
pfif_ret0:
                LEAS    1,S
                JMP     impl_int_to_fac
pfif_zero:
                CLRA
                CLRB
                BRA     pfif_ret0

;--- |v| >= 32768 の単精度: 整数型に収まらないので単精度のまま端数を落とす ---
;    右シフトで端数を押し出し、同じ回数だけ左へ戻す。押し出しが在って
;    負かつ INT なら 1.0 を引く (床)。
pfif_big:
                LDA     #159
                SUBA    FAC1            ; A = 端数ビット数 (exp>=159 なら 0 以下)
                BLE     pfif_bret       ; 端数なし → そのまま返す
                PSHS    A               ; 0,S = シフト量 / 1,S = 床フラグ
                CLRB                    ; B = sticky
pfif_brsh:
                JSR     fac1_shr32
                BCC     pfif_bns
                LDB     #1
pfif_bns:
                DECA
                BNE     pfif_brsh
                LDA     ,S+             ; シフト量を回収 (0,S = 床フラグ)
pfif_blsh:
                JSR     fac1_shl32
                DECA
                BNE     pfif_blsh
                TSTB                    ; 端数はあったか
                BEQ     pfif_bret
                TST     ,S              ; 床フラグ
                BEQ     pfif_bret       ; FIX → 調整不要
                TST     FAC1+5
                BPL     pfif_bret       ; 正 → 調整不要
                LEAS    1,S
                ; FAC1 = FAC1 + (-1.0)  … 単精度 -1.0 = 指数$80 / 仮数$80000000 / 符号-
                LDD     #$8080
                STD     FAC2
                CLRA
                CLRB
                STD     FAC2+2
                STD     FAC2+4
                STD     FAC2+6
                LDA     #$80
                STA     FAC2+5
                JMP     fadd_core       ; FAC1 = FAC2 + FAC1 (FACTYPE=1)
pfif_bret:
                LEAS    1,S
                RTS

;--- 倍精度: 降格せずに端数だけを落とす ---
;    倍精度は 8B パック形 (+0 指数 / +1 bit7=符号+仮数上位7bit / +2..+7 下位48bit)
;    で、仮数は先頭 1 暗黙の 56 bit。値は [2^(exp-129), 2^(exp-128)) なので
;    端数ビット数 k = 184-exp。k<=0 なら端数なし、k>=56 なら |v|<1。
pfif_dbl:
                LDA     FAC1
                BEQ     pfif_zero       ; 値 0
                LDA     #184
                SUBA    FAC1            ; A = 端数ビット数 k
                BLE     pfif_dret       ; k <= 0 → 端数なし
                CMPA    #56
                BHS     pfif_dlt1       ; k >= 56 → |v| < 1
                CLRB
                PSHS    D               ; 0,S = 残り k / 1,S = sticky (床フラグは 2,S へ退く)
                LDX     #FAC1+8
pfif_dwhole:
                LDA     ,S
                CMPA    #8
                BLO     pfif_dpart
                SUBA    #8
                STA     ,S
                LDA     ,-X             ; 丸ごと落とすバイト
                BEQ     pfif_dw2
                INC     1,S             ; sticky
pfif_dw2:
                CLR     ,X
                BRA     pfif_dwhole
pfif_dpart:
                TSTA                    ; A = 端数の残りビット数 r (0..7)
                BEQ     pfif_dfin
                LDB     #$FF
pfif_dmask:
                ASLB
                DECA
                BNE     pfif_dmask      ; B = 上位を残すマスク ($FF<<r)
                LEAX    -1,X
                LDA     ,X              ; 元のバイト
                ANDB    ,X
                STB     ,X              ; 下位 r bit を落とす
                CMPA    ,X
                BEQ     pfif_dfin
                INC     1,S             ; 落ちたビットが在った
pfif_dfin:
                LEAS    1,S             ; 残り k を捨てる (0,S = sticky)
                PULS    B               ; B = sticky
                TSTB
                BEQ     pfif_dret
                TST     ,S              ; 床フラグ
                BEQ     pfif_dret       ; FIX → 調整不要
                TST     FAC1+1          ; 符号 (bit7)
                BPL     pfif_dret       ; 正 → 調整不要
                LEAS    1,S
                ; FAC1 = FAC1 + (-1.0)  … 倍精度 -1.0 = 指数$81 / 仮数先頭1暗黙 / 符号-
                LDD     #$8180
                STD     FAC2
                CLRA
                CLRB
                STD     FAC2+2
                STD     FAC2+4
                STD     FAC2+6
                LDA     #2
                STA     FAC2TYPE
                JMP     fp_add_impl     ; FAC1 = FAC2 + FAC1 (倍精度コアへ)
pfif_dlt1:
                CLRA
                CLRB
                TST     ,S              ; 床フラグ
                LBEQ    pfif_ret0       ; FIX → 0
                TST     FAC1+1          ; 符号 (bit7)
                LBPL    pfif_ret0
                LDD     #-1             ; 負の真小数 → -1
                LBRA    pfif_ret0
pfif_dret:
                LEAS    1,S
                RTS

;==============================================================================
; 値域検査の追加分 — 本区間の先頭にある err_ifc からは短分岐が届かないので、
; ここに 2 本目の中継を置いて各判定は `BHI err_ifc2` (2 byte) で済ませる。
; (直前は pfif_dret の RTS なので落ち込み経路はない)
;==============================================================================
err_ifc2:
                JMP     errc_5

;------------------------------------------------------------------------------
; impl_fn_anport — ANPORT(チャンネル番号, 電圧レンジ)
;   参考書籍「ANPORT」: アナログポートから A/D 変換された
;   0〜255 の値を与える。チャンネル番号は 0〜3、電圧レンジは 0 (0〜5/8 V) か
;   1 (0〜2.5 V)。範囲外は Illegal Function Call (他の関数の値域検査と同じ)。
;   アナログ入力の基板が無い状態で読める値は 255 (参考書籍の例の表示。
;   参考書籍 ANALGP「基板を実装していない時には常に 255」)。本 ROM は
;   基板を持たないので、引数を検査したうえでその値を返す。
;   引数の解析は他の関数と同じ規約で、閉じ括弧の次を fn_xsave へ置く。
;   (err_ifc2 の直後に置くのは、値域検査の BHI が短分岐で届くようにするため)
;------------------------------------------------------------------------------
impl_fn_anport:
                JSR     impl_expect_tok
                FCB     '('
                JSR     eval_to_int_round       ; D = チャンネル番号
                CMPD    #3
                BHI     err_ifc2                ; 0〜3 の外
                JSR     impl_expect_tok
                FCB     ','
                BSR     eval_to_int_1           ; 電圧レンジは 0 か 1
                JSR     impl_expect_tok
                FCB     ')'
                STX     fn_xsave                ; 引数直後の本文ポインタ
                LDB     #255                    ; 基板が無い状態の値
                JMP     int_to_fac_b            ; A=0 を畳んだ入口

;------------------------------------------------------------------------------
; eval_to_int_1 / eval_to_int_key / eval_to_int_err —
;   「整数化した後に各文が行う値域検査」のうち、複数箇所で使う 3 種。
;   いずれも丸めが先で検査が後、範囲外は Illegal Function Call。
;   CMPD / TSTA は符号なし判定なので負値も同じ判定で捕まる。
;   動作観察で確かめた値域:
;     eval_to_int_1   … 0..1   (LOCATE のカーソル指定)
;     eval_to_int_key … 1..10  (KEY の番号。0 も範囲外)
;     eval_to_int_err … 1..255 (ERROR の誤り番号。0 も範囲外)
;------------------------------------------------------------------------------
eval_to_int_1:
                JSR     eval_to_int_round
                CMPD    #1
                BHI     err_ifc2
                RTS

eval_to_int_key:
                JSR     eval_to_int_round
                CMPD    #10
                BHI     err_ifc2
                TSTB
                BEQ     err_ifc2
                RTS


;------------------------------------------------------------------------------
; ikey_isnum — KEY の番号位置に来た 1 バイト (A) が「数値式の始まり」かを返す。
;   C=1 … 予約語トークンでない、または単項の '+' / '-' (演算子トークンだが
;          式の一部)。呼出側はそのまま式として評価する。
;   C=0 … その他の予約語トークン (呼出側が LIST か構文エラーかを判定する)。
;   単項符号を通さないと `KEY -1,"A"` が構文エラーになり、標準の
;   Illegal Function Call と食い違う。A / X は保つ。
;------------------------------------------------------------------------------
ikey_isnum:
                TSTA
                BPL     iki_yes         ; 予約語トークン ($80-) 以外
                CMPA    #TOK_UPLUS
                BLO     iki_no
                CMPA    #TOK_MINUS+1    ; '-' の次 (= 符号でない。'-' は '+' の直後)
                BHS     iki_no
iki_yes:
                ORCC    #$01
                RTS
iki_no:
                ANDCC   #$FE
                RTS
