; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; グラフィック座標解析ヘルパ
;==============================================================================

; ==========================================
; グラフィック座標解析ヘルパ
; ==========================================

; tmpX16, tmpY16, tmpX16_2, tmpY16_2 の作業変数
; (ROM内に定義)
; (tmpX16/tmpY16/tmpX16_2/tmpY16_2 は RAM ($0180-) に置く。書込を保持する
;  必要があるため ROM 内 FDB では成立しない)

; impl_parse_xy_cmd / impl_parse_xy_cmd2: 図形の座標 (x,y) を 1 組読み取る
;   引数: X = CURIP
;   出力: 第 1 座標なら tmpX16/tmpY16、第 2 座標なら tmpX16_2/tmpY16_2。
;         X は解析後の位置へ進む
;   形式: '(' があれば読み飛ばし、式評価 → 水平位置、',' を挟んで式評価 →
;         垂直位置、')' があれば読み飛ばす

; grclip_x / grclip_y — 図形座標を画面内へ丸める
;   扱い: 水平位置は符号無しで 639 超を 639 へ丸める (負値は
;   符号無し比較で巨大値になるため同じ経路で 639 になる)。垂直位置は 0-255
;   だけを受理し (範囲外は Illegal Function Call)、そのうち 199 超を 199 へ
;   丸める。丸めは座標の解析時点で行うので、LINE / PSET / PRESET / PAINT /
;   CIRCLE の中心 / 折線の各点まで同じ規則が一律に適用される。
;   丸めた結果は必ず画面内に収まるため、サブ CPU 側が範囲外座標を描かない扱いで
;   あっても図形が丸ごと消えることはない (両者は対で 1 つの規則をなす)。
;   D 入出力。CC のみ破壊。
grclip_y:
                CMPD    #200
                BCS     grclip_rts
                LDD     #199
grclip_rts:
                RTS

impl_parse_xy_cmd:
                LDU     #tmpX16         ; 格納先 = 第 1 座標
                BRA     ipxy_go
impl_parse_xy_cmd2:
                LDU     #tmpX16_2       ; 格納先 = 第 2 座標
ipxy_go:
                ; 第 1 / 第 2 座標で違うのは格納先だけなので、格納先を
                ; ipxy_dst へ預けて本体を 1 本に畳む。式評価は U を壊すため、
                ; 格納の直前に毎回 ipxy_dst から読み直す。
                STU     ipxy_dst
                ; 先頭の空白を読み飛ばす (トークン列は "SYMBOL (..." のように
                ; 命令トークンと '(' の間に空白を含むため、ここで詰めないと '(' を
                ; 認識できず eval_expr が '(' を括弧式として食い ')' 不一致で誤エラー)
                JSR     skip_sp_lparen
                BNE     ipxyc_no_paren
                LEAX    1,X
ipxyc_no_paren:
                JSR     eval_to_addr   ; 水平位置はアドレスと同じ規約 (-65536..65535)
                CMPD    #640            ; 水平位置を画面内へ丸める
                BCS     ipxyc_x_ok      ; 水平座標を 0..639 に収める (grclip_y と同じ形)
                LDD     #639
ipxyc_x_ok:
                LDU     ipxy_dst
                STD     ,U
                LDA     ,X+             ; ','
                ; 垂直位置は最近接丸めで整数化する (水平位置はゼロ方向切り捨て)。
                ; 丸めを欠くと図形の縦位置が 1 ドット上へずれ、折れ線の連結部に
                ; 隙間ができて塗りつぶしが外へ漏れる。
                JSR     impl_eval_expr
                JSR     impl_fac_to_int_round
                TSTA                    ; 垂直位置は 0-255 のみ受理
                LBNE    iw_fc           ; 範囲外 → Illegal Function Call
                BSR     grclip_y        ; 垂直位置を画面内へ丸める
                LDU     ipxy_dst
                STD     2,U
                LDA     ,X
                CMPA    #')'
                BNE     ipxyc_done
                LEAX    1,X
ipxyc_done:
                RTS

; impl_parse_func: 機能キーワード (PSET/PRESET/AND/OR/XOR) を解析
; 出力: B = ファンクションコード (0=PSET,1=PRESET,3=AND,2=OR,4=XOR)
; X は解析後に進む
impl_parse_func:
                ; トークン形を先に受理する (プログラムを LOAD すると PSET 等は
                ;   1 バイトのトークンに変換される。動作観察)。ASCII 綴りのみを
                ;   判定すると X が前進せず、呼出元 (LINE/CIRCLE 等) がトークン値
                ;   を色と誤認する。
                LDA     ,X
                CLRB                    ; FUNC_PSET = 0
                CMPA    #TOK_PSET       ; PSET トークン
                BEQ     ipf_tok
                LDB     #FUNC_PRESET
                CMPA    #TOK_PRESET     ; PRESET トークン
                BEQ     ipf_tok
                LDB     #FUNC_AND
                CMPA    #TOK_AND        ; AND トークン
                BEQ     ipf_tok
                LDB     #FUNC_OR
                CMPA    #TOK_OR         ; OR トークン
                BEQ     ipf_tok
                LDB     #FUNC_XOR
                CMPA    #TOK_XOR        ; XOR トークン
                BNE     ipf_ascii
ipf_tok:
                LEAX    1,X             ; トークン 1 バイトを消費
                RTS
ipf_ascii:
                ; ASCII 綴り (直接モード入力の綴りを照合)
                CMPA    #'P'
                BNE     ipf_and
                LDA     1,X
                CMPA    #'S'
                BNE     ipf_preset
                ; PSET
                LEAX    4,X             ; 'PSET' 4文字スキップ
                CLRB                    ; FUNC_PSET = 0
                RTS
ipf_preset:
                ; PRESET
                LEAX    6,X             ; 'PRESET' 6文字スキップ
                LDB     #FUNC_PRESET
                RTS
ipf_and:
                CMPA    #'A'
                BNE     ipf_or
                LEAX    3,X             ; 'AND' 3文字スキップ
                LDB     #FUNC_AND
                RTS
ipf_or:
                CMPA    #'O'
                BNE     ipf_xor
                LEAX    2,X             ; 'OR' 2文字スキップ
                LDB     #FUNC_OR
                RTS
ipf_xor:
                CMPA    #'X'
                BNE     ipf_default
                LEAX    3,X             ; 'XOR' 3文字スキップ
                LDB     #FUNC_XOR
                RTS
ipf_default:
                CLRB                    ; FUNC_PSET = 0
                RTS


; impl_psg_write: PSG レジスタ書込 (A=レジスタ番号 0-13, B=値)。全レジスタ保存。
;   AY-3-8910 タイプの 2 ポート手順:
;     $FD0E にレジスタ番号 → $FD0D=$03 (アドレスラッチ) → $FD0D=$00 (不活性)
;     → $FD0E に値 → $FD0D=$02 (データ書込) → $FD0D=$00 (不活性)。
;   ラッチ手順の途中でタイマ割込のプレイヤ (7t_play.inc) が別のレジスタを
;   書くとアドレスラッチが化けるため、手順全体を割込みマスクで包む。
impl_psg_write:
                PSHS    CC              ; 割込マスク状態を保存し
                ORCC    #$10            ; ラッチ手順を割込と排他にする
                PSHS    A,B,X
                LDX     #PORT_PSG_ADDR+1 ; X = $FD0E (データ), -1,X = $FD0D (コマンド)
                STA     ,X              ; データバスへレジスタ番号
                LDA     #$03
                STA     -1,X            ; アドレスラッチ
                CLR     -1,X            ; 不活性
                STB     ,X              ; データバスへ設定値
                LDA     #$02
                STA     -1,X            ; データ書込
                CLR     -1,X            ; 不活性
                PULS    A,B,X
                PULS    CC,PC

impl_beep_snd:
                CLRA                    ; (短縮)
                LDB     #$E6
                BSR     impl_psg_write
                LDA     #1
                CLRB                    ; (短縮)
                BSR     impl_psg_write
                LDD     #$07F8          ; レジスタ 7 = ミキサ
                BSR     impl_psg_write
                LDD     #$080F          ; レジスタ 8 = 音量
                BSR     impl_psg_write
                LDX     #$4000
ibs_wait:
                LEAX    -1,X
                BNE     ibs_wait
                LDA     #8
                CLRB                    ; (短縮)
                BSR     impl_psg_write
                LDD     #$07F8          ; レジスタ 7 = ミキサ
                BRA     impl_psg_write ; (末尾呼び)

;==============================================================================
; 行サーチ: impl_find_line
;   入力: D = 行番号
;   出力: Z=0 成功 X=行先頭, Z=1 失敗
;
;   行リンクを先頭から終端 [00 00] まで線形走査する。行番号の昇順は前提に
;   せず、対象より大きい行番号を見ても打ち切らない (行番号が昇順に並んで
;   いない本文でも、存在する行を必ず見つけるため)。
;==============================================================================
impl_find_line:
                PSHS    D
                LDX     TXTTAB
ifl_lp:
                LDD     ,X
                BEQ     ifl_fail
                LDD     2,X
                CMPD    ,S
                BEQ     ifl_ok
                LDX     ,X
                BRA     ifl_lp
ifl_ok:
                PULS    D
                ANDCC   #$FB            ; Z=0
                RTS
ifl_fail:
                PULS    D
                ORCC    #$04            ; Z=1
                RTS

; impl_jump_to_line: 行レコード先頭 X へ実行を移す。
;   入力: X = 行レコード先頭。CURLINE_PTR/CURLIN を更新し、行本文 (原文) を
;   実行用行バッファへ変換して CURIP = 変換済み本文の先頭。X も同位置へ。
;   GOTO/GOSUB/ON 後に行末で iep_next_line が正しい次行を辿れるようにする。
;   CURLIN も飛び先の行番号へ更新する (次の行境界まで更新を遅らせると、
;     GOTO 直後のエラー行表示・TRON・実行行の観測がいずれもズレる。動作観察)。
impl_jump_to_line:
                JSR     exec_conv_line  ; 変換 + CURLINE_PTR/CURLIN 更新, X=作業列先頭
                STX     CURIP
                ; 行ジャンプでも事象の発生を見る。IF ... THEN <自行> や GOTO の
                ;   自己ループは文の切れ目 (iep_stmt_new) を通らないため、ここが
                ;   唯一の取り込み・分岐の機会になる。取り込みも判定も文の切れ目と
                ;   同じ手順 (key_attn_poll → iep_jump_gate) を通し、INTERVAL と
                ;   機能キーのどちらも同じ条件で拾う。
                JSR     key_attn_poll   ; 機能キーの発生を取り込む (X 温存)
                JMP     iep_jump_gate   ; X = 飛び先の文頭 (分岐すれば X はハンドラ行へ)


;==============================================================================
; impl_expect_tok — 次バイトが指定トークンか確認
;   呼び出し方: JSR impl_expect_tok / FCB <token>
;
;   スタック上の 2 バイトのリターンアドレスは直後の FCB バイトを指す。
;   その FCB を読んで入力トークン (,X) と比較し、一致すれば X を進めて
;   リターンアドレスを FCB の次へ更新して復帰する。
;==============================================================================
impl_expect_tok:
                PULS    U               ; U = リターンアドレス (FCB を指す)
                LDA     ,U+             ; A = 期待トークン, U を FCB の次へ
                PSHS    A               ; 期待トークン退避
                JSR     skip_sp_x       ; 入力側の先行空白を読み飛ばす
                PULS    A               ; 期待トークン復元
                CMPA    ,X+             ; 入力トークンと比較 (一致なら X 前進)
                BNE     iet_err
                PSHS    U               ; リターンアドレス (FCB の次) を戻す
                RTS
iet_err:
                JMP     errc_2

;==============================================================================
; FAC (Floating Point Accumulator) — 内部表現
;
; FAC1 / FAC2 は各 8B (置き場所は 7t_defs.inc)。
;   FACTYPE: 0=整数, 1=単精度浮動小数点, 2=倍精度, 3=文字列
;
; 整数 (FACTYPE=0):   FAC1+2..+3 = 符号付き 16bit 整数。FAC1+0..+1 は 0。
; 単精度 (FACTYPE=1): FAC1+0 = 指数, +1..+4 = 仮数 32bit, +5 = 符号。
; 倍精度 (FACTYPE=2): FAC1+0 = 指数, +1 の bit7 = 符号, +1..+7 = 仮数。
;==============================================================================

; impl_int_to_fac: D(符号付き16bit整数) → FAC1
int_to_fac_m1:                          ; FAC1 = -1
                LDD     #-1
                BRA     impl_int_to_fac
int_to_fac_zero:
                CLRB              ; FAC1 = 0 (呼出側の LDD を畳んだ入口)
int_to_fac_b:
                CLRA              ; FAC1 = B (0-255。呼出側の CLRA を畳んだ入口)
impl_int_to_fac:
                CLR     FAC1
                CLR     FAC1+1
                STD     FAC1+2
                CLR     FAC1+4
                CLR     FAC1+5
                CLR     FAC1+6
                CLR     FAC1+7
                CLR     FACTYPE
                RTS

; impl_fac_to_int / impl_fac_to_int_round: FAC1 → D (符号付き16bit整数, 最近接丸め)
;   「整数を要求する文脈」の共通の整数化。両名は同一の入口である。
;   MOD / \ のオペランド整数化用 (参考書籍の整数変換規約):
;     FACTYPE=0 (整数): FAC1+2 をそのまま返す。
;     FACTYPE=1 (単精度): 最近接丸め。半数 (端数ちょうど 0.5) はゼロから
;       遠い側へ。|v|<0.5→0。ただしちょうど -0.5 だけは 0 にする。
;   int16 範囲 (-32768..32767) を外れたら Overflow (ERR 6)。
;   FAC1 仮数を破壊 (終端変換)。X は保存。破壊: A,B,CC。
fac_to_int_x1:                          ; 1 進めてから整数化
                LEAX    1,X
impl_fac_to_int:
impl_fac_to_int_round:
                LDA     FACTYPE
                BNE     f2ir_flt
                LDD     FAC1+2
                RTS
f2ir_flt:
                ; 倍精度の 0.5<=|v|<1 の範囲は、単精度へ落とす**前**にここで決着させる。
                ; 落としてからでは 0.5 のすぐ上の値まで 0.5 へ丸め込まれてしまい、
                ; ちょうど -0.5 かどうかを区別できなくなる。
                CMPA    #2              ; A = FACTYPE
                BNE     f2ir_dem        ; 単精度はそのまま丸め経路へ
                LDA     FAC1
                CMPA    #128            ; 倍精度で 0.5<=|v|<1 を表す指数
                BNE     f2ir_dem
                LDA     FAC1+1          ; bit7=符号 / bit6-0=仮数上位 7bit
                ANDA    #$7F
                ORA     FAC1+2          ; 仮数下位 48bit に端数が有るか
                ORA     FAC1+3
                ORA     FAC1+4
                ORA     FAC1+5
                ORA     FAC1+6
                ORA     FAC1+7          ; A=0 なら |v| は厳密に 0.5
                LDB     FAC1+1
                STB     FAC1+5          ; 符号を単精度と同じ位置へ移す
                TSTA
                BNE     f2ir_one1       ; 端数あり → ±1 (符号は FAC1+5)
                TSTB
                BMI     f2ir_zero       ; ちょうど -0.5 → 0
                BRA     f2ir_one1       ; ちょうど +0.5 → 1
f2ir_dem:
                JSR     dbl_demote      ; 倍精度は単精度へ落としてから丸め
                LDA     FAC1
                BEQ     f2ir_zero       ; 指数0 → 値0
                LDA     #159
                SUBA    FAC1            ; A = 159 - exp = 右シフト量
                BMI     f2ir_ovf        ; exp>159 → |v|>=2^32 → Overflow
                CMPA    #33
                BHS     f2ir_zero       ; |v| < 0.5 → 0
                CMPA    #32
                BEQ     f2ir_one        ; 0.5 <= |v| < 1 → 1
                TSTA
                BEQ     f2ir_take       ; 端数なし (シフト不要)
f2ir_rsh:
                JSR     fac1_shr32
                DECA                    ; DEC は C を変えない
                BNE     f2ir_rsh
                BCC     f2ir_take       ; 端数 < 0.5 → 切り捨て
                INC     FAC1+4          ; 端数 >= 0.5 → |値| に 1 繰り上げ
                BNE     f2ir_take
                INC     FAC1+3
                BNE     f2ir_take
                INC     FAC1+2
                BNE     f2ir_take
                INC     FAC1+1
f2ir_take:
                LDD     FAC1+1          ; 上位16bit が非0 → 範囲外
                BNE     f2ir_ovf
                LDD     FAC1+3          ; |値| 下位16bit
                BPL     f2ir_sign
                ; bit15=1: 負で $8000 (=-32768) のときだけ許容
                TST     FAC1+5
                BPL     f2ir_ovf
                CMPD    #$8000
                BNE     f2ir_ovf
                RTS                     ; D=$8000 = -32768
f2ir_one:
                ; 0.5 <= |v| < 1 の範囲。ちょうど -0.5 は 0 にする。仮数がちょうど
                ; $80000000 (= |v| が厳密に 0.5) で負のときだけ 0 を返す。
                TST     FAC1+5          ; 符号
                BPL     f2ir_one1       ; 正 → 1
                LDD     FAC1+3          ; 仮数の下位 16bit
                BNE     f2ir_one1
                LDD     FAC1+1          ; 仮数の上位 16bit
                CMPD    #$8000
                BEQ     f2ir_zero       ; ちょうど -0.5 → 0
f2ir_one1:
                LDD     #1
f2ir_sign:
                TST     FAC1+5          ; 符号適用 (被変換値の符号)
                BPL     f2ir_ret
                JSR     negd
f2ir_ret:
                RTS
f2ir_zero:
                LDD     #0
                RTS
f2ir_ovf:
                JMP     errc_6

; impl_is_false: FAC1が0か判定 (Z=1なら真=0, Z=0なら偽=非0)
impl_is_false:
                LDA     FAC1
                ORA     FAC1+1
                ORA     FAC1+2
                ORA     FAC1+3
                RTS

impl_load_1:
                LDD     #1
                JMP     impl_int_to_fac

;==============================================================================
; 数値解析 impl_parse_num
;   入力: X = 文字列ポインタ
;   出力: FAC1 = 数値, X = 次のトークン位置
;==============================================================================
; impl_parse_num: 数値リテラルを読み FAC1 に格納, X を前進させる
; 対応: 符号付き10進整数, &Hxx (16進), &Oxx (8進), 小数点/指数は整数部のみ
impl_parse_num:
                ; 符号チェック ('-'/'+' は VAL/DATA 等の生テキストで現れる。
                ;   '-' は値を再帰で読み、型に応じた符号反転を行う。pn2_done へ
                ;   直行すると PULS D が戻りアドレスを壊すため通さない)
                LDA     ,X
                CMPA    #'-'
                BNE     pn2_pos
                LEAX    1,X
                BSR     impl_parse_num  ; 値 (整数/単精度) を FAC1 へ
                JMP     impl_fac_negate ; 型対応の符号反転
pn2_pos:
                CMPA    #'+'
                BNE     pn2_noplus
                LEAX    1,X             ; 先頭 '+' は読み飛ばす (以降は数字/'.')
pn2_noplus:
                CMPA    #'&'
                BNE     pn2_dec
                ; 累算値は作業レジスタ U に保持する (PSHS A,D / LDD 2,S 方式は
                ; 添字がずれて桁加算を誤るため)。ただし U は呼び出し元
                ; (LET 等の impl_get_var_ref が返す VARPTR) が保持している
                ; 場合があるため、ループ前後で必ず退避/復元する。
                PSHS    U
                LEAX    1,X
                LDA     ,X+
                CMPA    #'H'
                BEQ     pn2_hex
                ; 8進
                LDU     #0
pn2_oct_lp:
                LDA     ,X
                CMPA    #'0'
                BLO     pn2_radix_done  ; (短縮)
                CMPA    #'7'
                BHI     pn2_radix_done  ; (短縮)
                LEAX    1,X
                SUBA    #'0'            ; A = 桁 (0-7)
                PSHS    A               ; 桁を退避
                TFR     U,D             ; D = 累算値
                CMPD    #$2000          ; これ以上を 8 倍すると 16 ビットを超える
                BHS     f2ir_ovf        ; 基数付き定数の値域は 16 ビット (Overflow)
                ASLB                    ; D *= 8
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
                ADDB    ,S+             ; + 桁 (S++)
                ADCA    #0
                TFR     D,U             ; 累算値を U に戻す
                BRA     pn2_oct_lp
pn2_hex:
                LDU     #0              ; 累算値は U
pn2_hex_lp:
                LDA     ,X
                CMPA    #'0'
                BLO     pn2_radix_done
                CMPA    #'9'
                BLS     pn2_hex_d
                CMPA    #'A'
                BLO     pn2_radix_done
                CMPA    #'F'
                BHI     pn2_radix_done
                SUBA    #'A'-10
                BRA     pn2_hex_add
pn2_hex_d:
                SUBA    #'0'
pn2_hex_add:
                LEAX    1,X
                ANDA    #$0F            ; A = 桁 (0-15)
                PSHS    A               ; 桁を退避
                TFR     U,D             ; D = 累算値
                CMPD    #$1000          ; これ以上を 16 倍すると 16 ビットを超える
                LBHS    errc_6          ; 基数付き定数の値域は 16 ビット (Overflow)
                ASLB                    ; D *= 16
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
                ADDB    ,S+             ; + 桁 (S++)
                ADCA    #0
                TFR     D,U             ; 累算値を U に戻す
                BRA     pn2_hex_lp
pn2_radix_done:
                ; 16進/8進ループは U に結果を持つ。D へ転送し、退避していた
                ; U (呼び出し元の VARPTR 等) を復元してから値を FAC1 へ移す。
                ; 基数付き定数の値は**符号なし**の 16 ビット (0-65535) である
                ; (参考書籍。整数の上限を超える値は実数として扱う)。
                ; よって符号付き整数化ではなく impl_uint_to_fac (プール) へ渡す。
                TFR     U,D
                PULS    U               ; 呼び出し元の U を復元
                JMP     impl_uint_to_fac
pn2_dec:
                ; 小数点/指数付きは単精度化, 純整数は整数化。本体は空きプール。
                JMP     pn_decimal

; 符号なし10進整数を D に読む (X を前進)
; D = result (0 で初期化し各桁で 10倍 + 桁を加算)
; A は D_hi として使用, B は D_lo として使用
pn2_uint:
                CLRA              ; result = 0 (A=0, B=0)
                CLRB
pn2u_lp:
                ; B を使って次の文字をチェック (A=D_hi を保持)
                PSHS    D               ; D(result) を保存: [S]=D_hi=A, [S+1]=D_lo=B
                LDA     ,X             ; A = 次の文字 (D_hi が壊れる)
                CMPA    #'0'
                BLO     pn2u_restore
                CMPA    #'9'
                BHI     pn2u_restore
                LEAX    1,X
                SUBA    #'0'            ; A = 桁 (0-9)
                ; A = digit, スタック: [D_hi, D_lo]
                PSHS    A               ; 桁保存: [S]=digit, [S+1]=D_hi, [S+2]=D_lo
                ; D = result (元の値) を復元して *10 計算
                LDD     1,S             ; D = result: M[S+1:S+2] = [D_hi:D_lo]
                ; 16 ビットに収まらなくなる綴りは上限へ張り付かせる。巻き付かせると
                ; 66000 が 464 に化けて「小さな行番号」として通ってしまうため、
                ; 上限判定 (行番号は 0-63999) が働くよう $FFFF で留める。
                CMPD    #6554           ; これ以上を 10 倍すると 16 ビットを超える
                BLO     pn2u_mul
                LEAS    3,S             ; 桁と直前の result の枠を捨てる
                LDD     #$FFFF          ; 以降の桁は読み飛ばし、値は上限のまま
                BRA     pn2u_lp
pn2u_mul:
                ; D * 8 (16bit 左シフト ×3 = 下位 ASLB → 上位 ROLA):
                ASLB
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
                ; D = result*8. result も [S+1:S+2] に残っている
                PSHS    D               ; D*8 保存: [S]=D8_hi,[S+1]=D8_lo,[S+2]=digit,[S+3]=D_hi,[S+4]=D_lo
                LDD     3,S             ; D = result (from [S+3:S+4])
                ; D * 2 (16bit 左シフト):
                ASLB
                ROLA
                ; D = result*2
                ADDD    ,S++            ; D += result*8 (pop D*8); S+=2
                ; D = result*10, stack: [digit, D_hi, D_lo]
                ADDB    ,S              ; B += digit at [S]
                ADCA    #0
                ; D = result*10 + digit = new result
                LEAS    3,S             ; pop [digit, D_hi, D_lo]
                BRA     pn2u_lp
pn2u_restore:
                PULS    D,PC               ; D = 元の result を復元

;==============================================================================
; 式評価 impl_eval_expr
;   入力: X = トークン列ポインタ
;   出力: FAC1 = 評価結果, X = 次位置
;
;   優先順位 (低→高):
;     IMP → EQV → XOR → OR → AND → NOT → 関係(<=>)
;     → + - → MOD → \ → * / → 単項- → ^ → 関数/プライマリ
;==============================================================================

eval_expr_ac:                           ; 1 文字取り込んでから式を評価
                LDA     ,X+
                BRA     impl_eval_expr
eval_expr_x1:                           ; 1 進めてから式を評価
                LEAX    1,X
impl_eval_expr:
                JMP     expr_imp

; skip_sp_x: X が指す位置から空白 ($20) を読み飛ばす。
;   SAVE 済みプログラム本文 (テープ由来) には演算子・キーワード前後の空白が
;   残るため、トークン/演算子を読む前に呼び出す。
;   入力: X = 走査位置  出力: X = 最初の非空白を指す, A = その非空白バイト
;   破壊: A のみ (CC)
skip_sp_lparen:                         ; 空白を読み飛ばして '(' か調べる
                BSR     skip_sp_x
                CMPA    #'('
                RTS
skip_sp_comma:                          ; 空白を読み飛ばして ',' か調べる
                BSR     skip_sp_x
                CMPA    #','
                RTS
skip_sp_take:                           ; 空白を読み飛ばして 1 文字取り込む
                BSR     skip_sp_x
                LDA     ,X+
                RTS
skip1_sp_x:
                LEAX    1,X             ; 1 バイト進めてから空白を読み飛ばす
skip_sp_x:
                LDA     ,X
                CMPA    #' '
                BNE     ssx_done
                LEAX    1,X
                BRA     skip_sp_x
ssx_done:
                RTS
; --- 頻出する短い並びの括り出し (機能は不変。容量捻出) --------------------
; skip_sp_tst — 空白を飛ばし、続く 1 文字の 0 判定まで行う定型 (18 箇所共通)。
skip_sp_tst:
                BSR     skip_sp_x
                TSTA
                RTS
; chk_comma_x — 本文の現在位置が ',' かを見る定型 (20 箇所共通)。X 不変、A = 文字。
chk_comma_x:
                LDA     ,X
                CMPA    #','
                RTS
; chk_type_str — FAC1 の型が文字列かを見る定型 (13 箇所共通)。
chk_type_str:
                LDA     FACTYPE
                CMPA    #3
                RTS
; fac_type_sng — 型を単精度にして復帰する定型 (7 箇所共通)。
fac_type_sng:
                LDA     #1
                STA     FACTYPE
                RTS
; out_vec_screen — 出力先ベクタを画面へ戻す定型 (5 箇所共通)。
out_vec_screen:
                LDD     #impl_putchar
                STD     out_vec
                RTS

; プライマリ式: 数値リテラル, 変数, 括弧, 関数
expr_primary:
                ; SAVE 済みプログラム本文にはオペランド前の空白 ($20) が
                ; 残っている (本文は再トークン化されないため)。プライマリ
                ; を読む前に先頭の空白を読み飛ばす。
                BSR     skip_sp_x
                ; 括弧
                CMPA    #'('
                BNE     epr_noparen
                LEAX    1,X
                JSR     chk_stack       ; 入れ子 1 段ごとにシステムスタックの空きを確かめる
                JSR     expr_imp
                LDA     ,X
                CMPA    #')'
                BNE     epr_paren_err
                LEAX    1,X
                RTS
epr_paren_err:
                ; 括弧の対応が取れないまま式が切れた (')' が来ない)。
                ; 行末で切れても文の途中 (':') で切れても Syntax Error。
                JMP     errc_2          ; Syntax Error
epr_noparen:
                ; 文字列リテラル "..."
                CMPA    #'"'
                BNE     epr_nostr
                LEAX    1,X
                STX     STRPTR
                CLRB                    ; (短縮)
epr_strloop:
                LDA     ,X+
                BEQ     epr_strz
                CMPA    #'"'
                BEQ     epr_strend
                INCB
                BRA     epr_strloop
epr_strz:
                ; 閉じ引用符の無い文字列定数は行末で閉じる。行末の $00 は文の
                ; 終端として実行ループが見るので消費してはならない。
                LEAX    -1,X
epr_strend:
                ; 定数の実体はプログラム本文にあるので複写しない。
                STB     STRLEN
                JMP     fret_str_own
epr_nostr:
                ; 数値リテラル (0-9 / . / &H 16進 / &O 8進)
                CMPA    #'&'            ; &Hxx / &Oxx 基数付き定数
                BEQ     epr_num
                CMPA    #'.'
                BEQ     epr_num
                CMPA    #'0'
                BLO     epr_nonum
                CMPA    #'9'
                BHI     epr_nonum
epr_num:
                JMP     impl_parse_num
epr_nonum:
                ; 関数トークン $FF xx
                CMPA    #TOK_FUNC_PFX
                BNE     epr_nofunc
                ; 呼び分けの本体は当区間に余白が無いため別プール
                ; (7t_float.inc の空きプールの手前) に置く。
                JMP     pool_fn_dispatch
epr_nofunc:
                ; USR 関数呼び出し  USRn(引数)
                CMPA    #TOK_USR        ; USR トークン
                BEQ     epr_usr         ; (短縮)
                ; FN トークン ($CF): 使用者定義関数 FNname(...) の呼出し
                CMPA    #TOK_FN
                LBEQ    epr_fn
                ; ERR / ERL — 直近エラーのコード/行番号 (未消費のまま 0 を返すと
                ;   PRINT ERR が無限に反復するため、専用の出口へ分ける)。
                CMPA    #TOK_ERR        ; ERR トークン
                BEQ     epr_err
                CMPA    #TOK_ERL        ; ERL トークン
                BEQ     epr_erl
                ; 変数名 (A-Z)
                CMPA    #'A'
                BLO     epr_zero
                CMPA    #'Z'
                BHI     epr_zero
                JMP     impl_get_var_val
; プライマリになり得ない綴りが来た。
;   読み取り位置を進めないまま値 0 を返すと、呼び元が同じ綴りを何度でも読み
;   直し、`PRINT A+ORD` (= A + OR D) のように演算子で終わる本文が 0 を延々と
;   出し続けて止まらない。よってここは誤りとして
;   打ち切る。番号は引数が尽きた場合 (行末・文の区切り) が Missing Operand、
;   綴りはあるがプライマリになり得ない場合が Syntax Error。
;   (A は expr_primary が読んだ現在バイトのまま。以降の CMPA 群は A を壊さない)
epr_zero:
                LDB     #22             ; Missing Operand
                TSTA
                BEQ     epz_err         ; 行末まで来てしまった
                CMPA    #':'
                BEQ     epz_err         ; 文の区切りまで来てしまった
                LDB     #2              ; Syntax Error
                CMPA    #TOK_FIRST
                BHS     epz_err         ; 予約語・演算子のトークン
                ; ここへ来るのは ',' ')' など生の区切り文字である。引数を省いた
                ; 位置 (CIRCLE の ",," など) で式評価に入る書き方があるので、
                ; ここでは値 0 を返す。読み位置は進めないが、呼び元は必ず
                ; その区切り文字を見て次へ進むので読み直しにはならない。
; 値 0 を返す出口 (上の落ち込みが使う)。
epr_ret0:
                JMP     int_to_fac_zero         ; FAC1 = 0
epz_err:
                JMP     impl_error_handler
epr_err:
                LEAX    1,X             ; ERR トークンを消費
                LDB     ERRNUM
                JMP     int_to_fac_b    ; A=0 を畳んだ入口
epr_erl:
                LEAX    1,X             ; ERL トークンを消費
                ; ERRLIN は符号なし 16bit (直接モードのエラーは $FFFF=65535)。
                ; FAC の 32bit 整数域へ上位 0 で置き、65535 を正数として返す。
                CLR     FAC1
                CLR     FAC1+1
                LDD     ERRLIN
                STD     FAC1+2
                CLR     FACTYPE
                RTS

;==============================================================================
; USRn(引数) の呼び出し (式プライマリから)
;   入力: X = USR トークン ($D2) を指す
;   次の USR 呼出規約で DEF USRn の機械語ルーチンを JSR する:
;     A = 引数型コード (整数=2。参考書籍の機械語連携仕様に従う)
;     X = 値ブロックへのポインタ (整数値は 2,X..3,X の 16bit)
;     戻り値 = 値ブロック (ML が 2,X へ書いた結果。書かなければ引数のまま)
;   「D=引数, X=ML エントリ」のような独自規約では、上記の規約に沿って書かれた
;   機械語ルーチンが 2,X から引数を読んだときに ML エントリ
;   直後のコードバイトを引数と誤認し、誤ったブロックポインタで自コードを
;   上書き破壊する。
;==============================================================================
epr_usr:
                ; USR トークン ($D2) を消費し、USRn のアドレス表 (usr_slot_tbl $0219-)
                ; から ML エントリを取る。未設定のスロットは Illegal Function Call の
                ; 入口 (ramstub_ifc) を指している (コールドスタートで入れる) ので、
                ; ここでは定義済みかを判定せず、そのまま呼ぶ (参考書籍)。
                LEAX    1,X             ; USR トークンをスキップ
                JSR     read_usr_slot   ; B = スロット番号, X 前進
                ASLB                    ; 2*slot
                PSHS    X
                LDX     #usr_slot_tbl
                ABX
                LDY     ,X              ; Y = ML エントリ
                PULS    X               ; 本文ポインタ復帰
                PSHS    Y               ; ML エントリを退避 (引数評価をまたぐ)
                JSR     skip_sp_lparen
                BNE     epr_usr_noarg
                LEAX    1,X
                JSR     expr_imp        ; 引数式を評価 → FAC1
                LDA     ,X
                CMPA    #')'
                BNE     epr_usr_perr
                LEAX    1,X
                BRA     epr_usr_call
epr_usr_noarg:
                ; 引数なし: 0 を渡す
                JSR     int_to_fac_zero         ; FAC1 = 0
epr_usr_call:
                ; 引数は FAC1 (文字列は文字列レジスタ) に入っている。
                ; 参考書籍 DEF USR の受け渡し: A = 引数の型 (2 整数 / 3 文字 /
                ; 4 単精度 / 8 倍精度)、X = 引数の値が格納されているアドレス (整数は
                ; 2,X〜3,X。文字列は長さ 1B + アドレス 2B の記述子)。
                PULS    Y               ; Y = ML エントリ
                PSHS    X               ; 本文ポインタを退避
                LDB     FACTYPE         ; 0 整数 / 1 単精度 / 2 倍精度 / 3 文字列
                LDX     #usr_tcode_tbl
                LDA     B,X             ; A = 型コード
                LDX     #FAC1           ; 数値: X = 値ブロック (整数は 2,X〜3,X)
                CMPA    #3
                BNE     euc_go
                LDB     STRLEN          ; 文字列: 記述子 [長さ][アドレス] を組む
                STB     cmt_hdr_buf     ;   (テープ用の一時域を借りる。式評価中は空き)
                LDX     STRPTR
                STX     cmt_hdr_buf+1
                LDX     #cmt_hdr_buf
euc_go:
                JSR     ,Y              ; ML 呼び出し (ML が自前で stack 平衡)
                PULS    X               ; 本文ポインタを復元
                LDA     FACTYPE
                BNE     euc_ret         ; 数値 (実数) と文字列は値ブロックをそのまま返す
                LDD     FAC1+2          ; 整数は値ブロックから取り直す (ML が 2,X へ書いた結果)
                JMP     impl_int_to_fac ; 戻り値 D を FAC1 へ (整数形へ正規化)
euc_ret:
                RTS
usr_tcode_tbl:  FCB     2,4,8,3         ; FACTYPE (整数/単精度/倍精度/文字列) → A の型コード
epr_usr_perr:
                PULS    Y               ; 退避していた ML エントリを破棄
                LDB     #22             ; Missing Operand
                BRA     epz_err

; べき乗 (左結合)。底=FAC1。指数評価で FAC2 が壊れるため左辺は型込みで
; スタック退避する (2 バイトの退避では実数の底・指数が壊れる)。
; 指数が符号で始まる場合のみ expr_unary へ委ね、後続のべき列を丸ごと従える
; (動作観察)。
expr_power:
                JSR     expr_primary
epow_loop:
                JSR     skip_sp_x
                CMPA    #TOK_POW        ; ^ トークン
                BNE     epow_done
                JSR     push_fac1_x1
                JSR     skip_sp_x
                SUBA    #TOK_PLUS       ; 単項プラス ($D9)/マイナス ($DA) か
                CMPA    #1
                BLS     epow_sgn
                JSR     expr_primary    ; 指数 → FAC1
                BRA     epow_apply
epow_sgn:
                BSR     expr_unary      ; 符号付き指数 (べき列を従える)
epow_apply:
                JSR     pop_to_fac2     ; 底を FAC2 へ復元 (FAC2TYPE 設定)
                JSR     impl_fp_pow     ; FAC1 = 底^指数
                BRA     epow_loop
epow_done:
                RTS

; 単項マイナス (単項プラスは読み飛ばすのみ)
expr_unary:
                JSR     skip_sp_x
                CMPA    #TOK_MINUS      ; - トークン
                BEQ     eun_neg
                CMPA    #TOK_PLUS       ; + トークン
                BNE     eun_nomneg
                LEAX    1,X
                BRA     expr_unary
eun_neg:
                LEAX    1,X
                BSR     expr_power
                JMP     impl_fac_negate ; (tail)
eun_nomneg:
                BRA     expr_power

; MOD (\ より低優先)
expr_mod:
                BSR     expr_idiv
emod_loop:
                JSR     skip_sp_x
                CMPA    #TOK_MOD        ; MOD
                BNE     emod_done
                JSR     fac_to_int_x1
                PSHS    D
                BSR     expr_idiv
                PULS    D
                STD     FAC2+2
                JSR     impl_fp_mod
                BRA     emod_loop
emod_done:
                RTS

; 整数除算 (\) (* / より低, MOD より高)
expr_idiv:
                BSR     expr_muldiv
eidiv_loop:
                JSR     skip_sp_x
                CMPA    #TOK_IDIV       ; backslash
                BNE     eidiv_done
                JSR     fac_to_int_x1
                PSHS    D
                BSR     expr_muldiv
                PULS    D
                STD     FAC2+2
                JSR     impl_fp_idiv
                BRA     eidiv_loop
eidiv_done:
                RTS

; 乗除
; 注: 左オペランドは FAC2 ではなくハードウェアスタックに退避する。
;     右辺評価 (expr_*) が内部で FAC2 を上書きするため、FAC2 に退避すると
;     ネスト式 (1+2*3 等) の左辺が壊れる。
expr_muldiv:
                BSR     expr_unary
emd_loop:
                JSR     skip_sp_x
                CMPA    #TOK_MUL        ; *
                BEQ     emd_mul
                CMPA    #TOK_DIV        ; /
                BNE     emd_done
                BSR     emd_prep
                JSR     fdiv_dispatch   ; / 実数除算 (型二重パス。両整数=fdiv_entry)
                BRA     emd_loop
emd_mul:
                BSR     emd_prep
                JSR     fp_mul_impl     ; * 整数は 16x16→16 ビット、単精度は fmul_core
                BRA     emd_loop
emd_done:
                RTS
; 乗除共通: 演算子を消費し、左辺退避→右辺評価→左辺を FAC2 へ復元 (共通化)
emd_prep:
                JSR     push_fac1_x1
                BSR     expr_unary
                JSR     pop_to_fac2     ; 左辺を FAC2 へ復元 (FAC2TYPE 設定)
                RTS                     ;!NOTAIL 末尾呼び (JMP) 化は不可。
                                        ;   pop_to_fac2 は自身の戻りアドレスを PULS して
                                        ;   から退避 FAC を取り出すスタック規約のため

; 加減 (最低優先)
expr_addsub:
                BSR     expr_mod
eas_loop:
                JSR     skip_sp_x
                CMPA    #TOK_PLUS       ; +
                BEQ     eas_add
                CMPA    #TOK_MINUS      ; -
                BNE     eas_done
                JSR     push_fac1_x1
                BSR     expr_mod
                JSR     chk_num         ; 右辺が文字列 → Type Mismatch
                JSR     pop_to_fac2
                JSR     fp_sub_impl     ; - FAC2 が引かれる数
                BRA     eas_loop
eas_add:
                LEAX    1,X
                ; 文字列連結か数値加算かを左辺の型で判定
                JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
                BEQ     eas_strcat
                ; 数値加算 (左辺が数値なので右辺も数値でなければならない)
                JSR     push_fac1
                JSR     expr_mod
                JSR     chk_num         ; 右辺が文字列 → Type Mismatch
                JSR     pop_to_fac2
                JSR     fp_add_impl     ; + 型二重パス
                BRA     eas_loop
eas_strcat:
                ; 左辺 STRPTR/STRLEN を退避 → 右辺評価 → str_work_buf へ連結
                ; スタック割付: 0,S=左STRLEN, 1,S=左STRPTR_hi, 2,S=左STRPTR_lo
                LDU     STRPTR          ; X は本文ポインタなので触らない
                LDA     STRLEN
                PSHS    U,A             ; [len][Phi][Plo] (PSHS は U→A の順に積む)
                JSR     expr_mod        ; 右辺 → STRPTR/STRLEN
                ; 連結は両辺とも文字列でなければならない (参考書籍の
                ; 加算演算子)。右辺が数値のときに続けると、数値の内部形式を
                ; 文字列の長さ・アドレスとして読んで無関係な文字を連ねてしまう。
                JSR     chk_str         ; 右辺が文字列でない → Type Mismatch
                STX     escat_save_x    ; 右辺評価後のプログラム本文ポインタを退避
                ; 連結結果を固定緩衝の先頭へ置くと、
                ;   同じ文で 2 回連結したときに先の結果が潰れる。合計長ぶんの
                ;   一時領域を確保してそこで組み立てる。
                LDB     ,S              ; 左 STRLEN
                ADDB    STRLEN          ; + 右 STRLEN = 合計長 (桁上がり = 255 超)
                JSR     str_cat_alloc   ; C=1 なら String Too Long / X = 結果領域
                                        ;   片方が一時ならそのアドレスを取り直す
                                        ;   (右辺だったときは STRPTR も退かし済)
                PSHS    X               ; [Xhi][Xlo][左len][左Phi][左Plo]
                ; 左をコピー
                LDU     3,S             ; 左 STRPTR (3,S:4,S)
                LDB     2,S             ; 左 STRLEN
                BEQ     escat_right
escat_lcopy:
                LDA     ,U+
                STA     ,X+
                DECB
                BNE     escat_lcopy
escat_right:
                ; 右をコピー
                LDU     STRPTR          ; 右 STRPTR
                LDB     STRLEN          ; 右 STRLEN
                BEQ     escat_done
escat_rcopy:
                LDA     ,U+
                STA     ,X+
                DECB
                BNE     escat_rcopy
escat_done:
                ; X は結果領域への書込で破壊済み。プログラム本文ポインタは
                ; 右辺評価後の STRPTR2 退避(後述)ではなく、右辺 expr_mod が
                ; 残した X を別途保持していないため、ここで連結結果長のみ確定し、
                ; プログラム本文ポインタを escat_save_x から復元する。
                ; 新 STRPTR = 結果領域, 新 STRLEN = 左 + 右
                PULS    X               ; X = 結果領域の先頭
                STX     STRPTR
                LDA     ,S+             ; 左 STRLEN (pop)
                ADDA    STRLEN          ; + 右 STRLEN
                LEAS    2,S             ; 左 STRPTR を捨てる
                STA     STRLEN
                LDA     #3
                STA     FACTYPE
                LDX     escat_save_x    ; プログラム本文ポインタを復元
                BRA     eas_loop
eas_done:
                RTS

; 関係演算子 (= < > <= >= <>)
;   演算子コード: TOK_GT / TOK_EQ / TOK_LT。複合は 2 トークン (< > → <>, < = → <=,
;   > = → >=)。比較は数値/文字列両対応。結果は真=-1 / 偽=0。
;   比較種別をビットで表現: bit0=LT許可, bit1=EQ許可, bit2=GT許可。
expr_rel:
                BSR     expr_addsub
                JSR     skip_sp_x
                CMPA    #TOK_GT         ; >
                BEQ     erel_op_gt
                CMPA    #TOK_EQ         ; =
                BEQ     erel_op_eq
                CMPA    #TOK_LT         ; <
                BEQ     erel_op_lt
                RTS
erel_op_gt:
                LEAX    1,X
                LDB     #%00000100      ; GT
                JSR     skip_sp_x
                CMPA    #TOK_EQ         ; >=
                BNE     erel_have
erel_geq:
                LEAX    1,X
                LDB     #%00000110      ; GT|EQ
                BRA     erel_have
erel_op_lt:
                LEAX    1,X
                LDB     #%00000001      ; LT
                JSR     skip_sp_x
                CMPA    #TOK_EQ         ; <=
                BNE     erel_lt_ck2
erel_leq:
                LEAX    1,X
                LDB     #%00000011      ; LT|EQ
                BRA     erel_have
erel_lt_ck2:
                CMPA    #TOK_GT         ; <>
                BNE     erel_have
                LEAX    1,X
                LDB     #%00000101      ; LT|GT (= 不等)
                BRA     erel_have
erel_op_eq:
                ; 逆順複合形 => / =< を受理する (それぞれ >= と <= に同じ。
                ; 動作観察)。尾部は
                ; erel_geq / erel_leq (通常順複合形) を共用してバイトを節約
                JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
                CMPA    #TOK_GT         ; => → >= 等価
                BEQ     erel_geq
                CMPA    #TOK_LT         ; =< → <= 等価
                BEQ     erel_leq
                LDB     #%00000010      ; EQ
erel_have:
                ; B = 比較種別マスク。左辺は FAC1 (数値 or 文字列)。
                PSHS    B               ; 種別マスク退避
                LDB     FACTYPE
                CMPB    #3
                BEQ     erel_str
                ; --- 数値比較 ---
                JSR     push_fac1
                JSR     expr_addsub
                JSR     pop_to_fac2
                JSR     fp_cmp_impl     ; CC: FAC2 - FAC1。LT/EQ/GT を判定
                ; cmp 結果を 0=LT,1=EQ,2=GT のいずれかへ
                BLT     erel_res_lt
                BGT     erel_res_gt
                BRA     erel_res_eq
erel_str:
                ; --- 文字列比較 ---
                ; 左 STRPTR/STRLEN を退避 → 右辺評価 → 比較
                LDU     STRPTR          ; X は本文ポインタなので触らない
                LDA     STRLEN
                PSHS    U,A             ; 左 STRPTR/STRLEN (PSHS は U→A の順に積む)
                JSR     expr_addsub     ; 右辺 → STRPTR/STRLEN
                ; str_cmp: 左(スタック) vs 右(STRPTR/STRLEN) → A=0(LT)/1(EQ)/2(GT)
                ; str_cmp3 は X(本文ポインタ)を壊すため DP 一時へ退避する
                ; (スタック退避は str_cmp3 の引数オフセットを崩すため不可)。
                STX     fn_xsave
                BSR     str_cmp3
                LDX     fn_xsave
                LEAS    3,S             ; 左 STRPTR/STRLEN を捨てる
                ; A に 0/1/2
                TSTA
                BEQ     erel_res_lt
                CMPA    #1
                BEQ     erel_res_eq
                BRA     erel_res_gt
erel_res_lt:
                LDB     #%00000001
                BRA     erel_apply
erel_res_eq:
                LDB     #%00000010
                BRA     erel_apply
erel_res_gt:
                LDB     #%00000100
erel_apply:
                ; 比較結果ビット (B) と 種別マスク (0,S) の AND が非0 なら真
                ANDB    ,S+             ; 種別マスク pop
                BEQ     erel_false
                JMP     int_to_fac_m1
erel_false:
                JMP     int_to_fac_zero         ; FAC1 = 0

; str_cmp3: 左文字列(スタック上: 0,S=左STRLEN,1,S:2,S=左STRPTR, ただし呼出時
;   さらに上に種別マスク等) vs 右文字列(STRPTR/STRLEN)。
;   実際の呼出規約: 呼出直前スタック = [左STRLEN][左STRPTRhi][左STRPTRlo][戻りアドレス...]
;   出力: A = 0(左<右) / 1(左=右) / 2(左>右)。スタックは不変。
str_cmp3:
                ; 戻りアドレスを跨いで引数へアクセスするため一旦戻りアドレスを退避
                PULS    Y               ; Y = 戻りアドレス
                ; 0,S=左STRLEN, 1,S:2,S=左STRPTR
                LDU     1,S             ; U = 左 STRPTR
                LDA     ,S              ; A = 左 STRLEN
                PSHS    Y               ; 戻りアドレスを戻す
                ; B = 右 STRLEN, X = 右 STRPTR
                LDX     STRPTR
                LDB     STRLEN
                ; 比較ループ: min(len) まで 1 バイトずつ
sc3_lp:
                TSTA
                BEQ     sc3_left_end
                TSTB
                BEQ     sc3_right_end
                PSHS    A,B
                LDA     ,U+
                CMPA    ,X+
                BLO     sc3_lt
                BHI     sc3_gt
                PULS    A,B
                DECA
                DECB
                BRA     sc3_lp
sc3_lt:
                LEAS    2,S
                CLRA
                RTS
sc3_gt:
                LEAS    2,S
                LDA     #2
                RTS
sc3_left_end:
                ; 左が尽きた: 右も尽きていれば等しい, でなければ左<右
                TSTB
                BEQ     sc3_eq
                CLRA
                RTS
sc3_right_end:
                ; 右が尽き左が残る → 左>右
                LDA     #2
                RTS
sc3_eq:
                LDA     #1
                RTS

; NOT
expr_not:
                JSR     skip_sp_x
                CMPA    #TOK_NOT        ; NOT
                BNE     enot_pass
                LEAX    1,X
                JSR     expr_rel
                JSR     impl_fac_to_int_round
                COMA
                COMB
                JMP     impl_int_to_fac
enot_pass:
                JMP     expr_rel

; AND
expr_and:
                BSR     expr_not
eand_loop:
                JSR     skip_sp_x
                CMPA    #TOK_AND        ; AND
                BNE     eand_done
                JSR     fac_to_int_x1
                PSHS    D
                BSR     expr_not
                JSR     impl_fac_to_int_round
                ANDA    ,S
                ANDB    1,S
                LEAS    2,S
                JMP     impl_int_to_fac
eand_done:
                RTS

; --- 論理演算の段 ---
;   AND より優先度の低い論理演算子は OR / XOR / EQV / IMP の順に低くなり、
;   低いものほど外側の段になる。よって内側から AND → OR → XOR → EQV → IMP の
;   順に段を重ね、同じ優先度の演算子は左から順に評価するので各段を左結合の
;   繰り返しで書く。
;   IMP / EQV はビットごとに
;     X IMP Y = (NOT X) OR Y 、 X EQV Y = NOT (X XOR Y)
;   で求めるので、否定を 1 つ足すだけで OR / XOR の段と同じ形になる。
;   被演算数は整数へ直してから演算する (参考書籍)。

; OR
expr_or:
                BSR     expr_and
eor_loop:
                JSR     skip_sp_x
                CMPA    #TOK_OR         ; OR
                BNE     eor_done
                JSR     fac_to_int_x1
                PSHS    D
                BSR     expr_and
                JSR     impl_fac_to_int_round
                ORA     ,S
                ORB     1,S
                LEAS    2,S
                JSR     impl_int_to_fac
                BRA     eor_loop
eor_done:
                RTS

; XOR
expr_xor:
                BSR     expr_or
exor_loop:
                JSR     skip_sp_x
                CMPA    #TOK_XOR        ; XOR
                BNE     exor_done
                JSR     fac_to_int_x1
                PSHS    D
                BSR     expr_or
                JSR     impl_fac_to_int_round
                EORA    ,S
                EORB    1,S
                LEAS    2,S
                JSR     impl_int_to_fac
                BRA     exor_loop
exor_done:
                RTS

; EQV
expr_eqv:
                BSR     expr_xor
eeqv_loop:
                JSR     skip_sp_x
                CMPA    #TOK_EQV        ; EQV
                BNE     eeqv_done
                JSR     fac_to_int_x1
                PSHS    D
                BSR     expr_xor
                JSR     impl_fac_to_int_round
                EORA    ,S
                EORB    1,S
                LEAS    2,S
                COMA
                COMB                    ; NOT (X XOR Y)
                JSR     impl_int_to_fac
                BRA     eeqv_loop
eeqv_done:
                RTS

; IMP (最も低い = 式の一番外側)
expr_imp:
                BSR     expr_eqv
eimp_loop:
                JSR     skip_sp_x
                CMPA    #TOK_IMP        ; IMP
                BNE     eimp_done
                JSR     fac_to_int_x1
                COMA
                COMB                    ; 左辺を否定 → (NOT X) OR Y
                PSHS    D
                BSR     expr_eqv
                JSR     impl_fac_to_int_round
                ORA     ,S
                ORB     1,S
                LEAS    2,S
                JSR     impl_int_to_fac
                BRA     eimp_loop
eimp_done:
                RTS

;==============================================================================
; 浮動小数点演算 (内部形式 単精度・倍精度)
;
; 整数は FAC1+2..+3 の 16bit 値、実数は指数・仮数・符号で演算する
; (形式は上の FAC の節を参照)。
;==============================================================================

;------------------------------------------------------------------------------
; (kl_body は 7t_color_video.inc に置く)
; (配列表のヘッダ固定部を求める arr_hdrskip を、配列側の区間予算が
;  足りないためこの空きプールへ置く。参照はラベル経由のみで位置依存はない)
; arr_hdrskip: X = エントリ先頭 → X = len 欄先頭 (= X + 1 + 名前長)。A 破壊。
arr_hdrskip:
                LDA     ,X
                ANDA    #$3F            ; 名前長
                INCA                    ; + ヘッダ 1B
                LEAX    A,X
                RTS

;------------------------------------------------------------------------------
; skip_comma — 空白を読み飛ばし、',' が来ていれば消費する
;   引数の区切りを見る定型。呼出側は JSR skip_comma / Bcc <なし> で使う。
;   入力: X = 本文。出力: C=1 … ',' を消費した (X はその次)
;                      C=0 … ',' でない (X は非空白文字の位置のまま)
;   A = 読み飛ばし後の非空白文字 (呼出側が続けて見るため保存する)。B は不変。
;------------------------------------------------------------------------------
skip_comma:
                JSR     skip_sp_comma
                BNE     skc_no
                LEAX    1,X             ; ',' を消費
                ORCC    #$01            ; C=1
                RTS
skc_no:
                ANDCC   #$FE            ; C=0
                RTS

;------------------------------------------------------------------------------
; sca_intmp — D の実体が一時領域の中なら、そのアドレスまで一時を返す (連結の確保口用)
;   入力: D = 実体の先頭アドレス。
;   出力: C=0 … 一時領域の中だった。TMPPTR を「先頭 - 1」へ戻してあり、その実体
;               より上に在った一時は返却済み。D は「先頭 - 1」。
;         C=1 … 一時領域の外 (本文中の定数・恒久ブロック等)。TMPPTR は不変。
;   一時領域は STRBOT の 1 つ上から TMPPTR までなので、判定は STRBOT < D <= TMPPTR。
;   C=0 側に ANDCC を置いていないのは、この経路の D が必ず STRBOT より上 (= 0 アドレス
;   ではない) で SUBD #1 の借りが立たず C=0 が確定するためで、続く STD は桁上げを
;   変えない。
;   (本片は空きプールに置く。参照はラベル経由で位置依存なし)
;------------------------------------------------------------------------------
sca_intmp:
                CMPD    STRBOT
                BLS     sci_out         ; 一時領域より下 = 本文中の定数など
                CMPD    TMPPTR
                BHI     sci_out         ; 一時領域より上 = 恒久ブロック
                SUBD    #1
                STD     TMPPTR          ; その実体のアドレスまで一時を返す
                RTS                     ; C=0 (SUBD #1 で借りは立たない)
sci_out:
                ORCC    #$01            ; C=1
                RTS

;------------------------------------------------------------------------------
; fold_a — A が小文字 'a'-'z' なら大文字へ畳む (それ以外は不変)。
;   予約語照合の語頭判定用 (レジスタ上のみ。読み元へは書き戻さない)。
;   (本片は空きプールに置く。参照はラベル経由で位置依存なし)
;------------------------------------------------------------------------------
fold_a:
                CMPA    #'z'
                BHI     fda_done
                CMPA    #'a'
                BLO     fda_done
                SUBA    #$20
fda_done:
                RTS

;------------------------------------------------------------------------------
; tmk_idx — 予約語表の先頭字索引引き (tok_match_kw / tok_match_fn の本体)
;   入力の 1 文字目 (大文字へ畳む) が英字なら、先頭字索引 (7t_deffn.inc の
;   kw_idx / fn_idx、生成器が予約語表と同期生成) でその英字の群の先頭へ
;   走査開始位置を直接送る。同一語頭の群は平均 4-6 語なので、照合が表の
;   総当りでなくなる。英字以外は、命令語表は表先頭から (記号語は表の前方に
;   あり、語頭バイト昇順の早期打切りが数語で決着させる)。関数語はすべて
;   英字始まりなので即失敗にする。
;   入口条件: Y = 入力先頭、X = 索引の先頭 (kw_idx / fn_idx。どちらの表かは
;             X のアドレスで判別する。kw_idx より fn_idx が後方にあることに依存)、
;             スタック = [表終端 2B][入力先頭 2B][呼出元 Y 2B]
;   出口: X = 走査開始位置, B = その位置のトークン値 → tmk_outer へ
;   索引の欄形式 (2B/字): byte0 = 群先頭の表内変位の下位 8 ビット、
;   byte1 = bit7:変位の第 9 ビット + 下位 7 ビット:群先頭トークンの下位 7 ビット
;   (トークンは常に bit7=1 のため復元は下位 7 ビット | $80 で足りる)。
;   (本片も 空きプールへ。参照はラベル経由で位置依存なし)
;------------------------------------------------------------------------------
tmk_idx:
                LDA     ,Y              ; 入力の 1 文字目
                BSR     fold_a
                STA     tok_fold1       ; 走査本体の語頭比較用 (呼出し毎に 1 回)
                CMPA    #'A'
                BLO     tki_def
                CMPA    #'Z'
                BHI     tki_def
                SUBA    #'A'
                LSLA                    ; 2B/字 (A ≦ 50 なので符号安全)
                LEAX    A,X             ; X = 索引の欄 (呼出元が索引先頭を渡す)
                LDD     ,X              ; A = 変位下位, B = 詰込 (bit7=変位第9bit)
                CMPX    #fn_idx         ; どちらの索引か (fn_idx が後方)
                BHS     tki_fn
                LDX     #kw_tbl
                BRA     tki_dec
tki_fn:
                LDX     #kw_fn_tbl
tki_dec:
                TSTB
                BPL     tki_lo
                LEAX    256,X           ; 変位の第 9 ビット
tki_lo:
                PSHS    B
                TFR     A,B
                CLRA                    ; D = 変位 (0-255)
                LEAX    D,X             ; X = 群の先頭
                PULS    B
                ANDB    #$7F
                ORB     #$80            ; B = 群先頭のトークン値
                JMP     tmk_outer
tki_def:
                CMPX    #fn_idx         ; 英字以外: どちらの表か
                BHS     tki_dfn
                LDX     #kw_tbl         ; 命令語表は表先頭から (記号語)
                LDB     #TOK_FIRST
                JMP     tmk_outer
tki_dfn:
                JMP     tmk_fail        ; 関数語はすべて英字始まり → 失敗


; FAC1 を否定 (6809 での実装: COMA/COMB/ADDD)
impl_fac_negate:
                ; 型対応の符号反転: 整数は 2 の補数、単精度は符号ビット反転。
                ;   単精度昇格した定数 (32768 等) への単項マイナスに必須。
                LDA     FACTYPE
                BNE     ifn_flt
                ; 整数の符号反転は 0 - 値 として減算本体に委ねる。
                ;   -32768 の符号反転は整数では表せないが、減算本体があふれを
                ;   見て単精度へ移してくれるので、ここで別扱いする必要がない。
                CLRA
                CLRB
                STD     FAC2+2          ; FAC2 = 0 (引かれる数)
                STA     FAC2TYPE        ; FAC2 も整数型
                JMP     fp_sub_impl     ; FAC2 - FAC1 → FAC1 (末尾呼び)
ifn_flt:
                CMPA    #2              ; 倍精度: 符号はパック形 FAC1+1 bit7
                BEQ     ifn_dbl
                JSR     fac1_flip_nz    ; 指数 0 = 値ゼロ → 符号は触らない
ifn_done:
                RTS
ifn_dbl:
                LDA     FAC1            ; 指数 0 = 値ゼロ
                BEQ     ifn_done
                LDA     FAC1+1
                EORA    #$80
                STA     FAC1+1
                RTS

; FAC1 + FAC2 → FAC1。型二重パス本体は空きプール。

; FAC2 - FAC1 → FAC1 [FAC2 が引かれる数]。

; FAC2 * FAC1 → FAC1。整数=16x16→16bit, 単精度=fmul_core。

; FAC2 / FAC1 → FAC1 (符号付き16bit除算)
; FAC2 / FAC1 → FAC1 (符号付き16bit整数除算, 商を切り捨て)
; 本文ポインタ X を退避して保護する。
; impl_fp_div (/): 実数除算。両オペランドを float 昇格し fdiv_core で除算。
; impl_fp_idiv (\): 整数除算。
impl_fp_idiv:
                JSR     impl_fac_to_int_round ; 右辺(除数)を整数化
                STD     FAC1+2
                BNE     fdiv_ok
                JMP     errc_11
fdiv_ok:
                PSHS    X               ; 本文ポインタ退避
                LDX     FAC2+2          ; X = 被除数
                LDD     FAC1+2          ; D = 除数
                BSR     div16s          ; D = 商, Y = 余り
                JSR     impl_int_to_fac
                PULS    X,PC ; 本文ポインタ復元

;==============================================================================
; div16s: 符号付き16bit除算  X / D → 商=D, 余り=Y
;   余りの符号は被除数に合わせる (BASIC \ / MOD の規約に準拠)。
;   除数 D は呼び出し前に非0 を保証すること。
;   破壊: A,B,X,Y,U,CC。
;==============================================================================
div16s:
                ; 除数の符号を退避し |除数| を div_div へ
                PSHS    X               ; 0,S=被除数(符号付き)
                STA     div_qsign       ; 除数上位 (商符号計算の一部)
                BPL     div16s_dvpos
                JSR     negd
div16s_dvpos:
                STD     div_div         ; |除数|
                ; 被除数の符号
                LDD     ,S              ; 被除数 (符号付き)
                STA     div_rsign       ; 余りの符号 = 被除数符号
                ; 商の符号 = 被除数符号 XOR 除数符号
                EORA    div_qsign
                STA     div_qsign
                ; |被除数|
                LDD     ,S++            ; D = 被除数, スタック後始末
                TSTA
                BPL     div16s_nupos
                JSR     negd
div16s_nupos:
                STD     div_num         ; |被除数| (商を構築する作業ワード (2 バイト))
                ; --- 符号なし 16/16 復元法除算 ---
                LDX     #0              ; rem = 0
                LDB     #16             ; ビットカウンタ
div16s_loop:
                PSHS    B               ; カウンタ退避
                ; num を左シフト、MSB を C へ
                ASL     div_num+1
                ROL     div_num
                ; rem = (rem<<1) | C
                TFR     X,D
                ROLB
                ROLA
                ; rem >= |除数| ?
                SUBD    div_div
                BCC     div16s_setbit   ; C=0 (借り無し) → rem>=div
                ADDD    div_div         ; 戻す
                TFR     D,X
                BRA     div16s_next
div16s_setbit:
                TFR     D,X             ; rem -= div を確定
                INC     div_num+1       ; 商ビット=1
div16s_next:
                PULS    B
                DECB
                BNE     div16s_loop
                ; 商 = div_num, |余り| = X
                TFR     X,Y             ; Y = |余り|
                ; 商の符号適用
                LDA     div_qsign
                BPL     div16s_qpos
                LDD     div_num
                JSR     negd
                BRA     div16s_qdone
div16s_qpos:
                LDD     div_num
div16s_qdone:
                ; 余りの符号 (被除数の符号) を Y に適用
                TST     div_rsign
                BPL     div16s_rpos
                PSHS    D               ; 商退避
                TFR     Y,D
                JSR     negd
                TFR     D,Y
                PULS    D               ; 商復元
div16s_rpos:
                RTS

; (impl_fp_mod は 7t_float.inc に置く。div16s は本区間)

; FAC2 ^ FAC1 → FAC1 (単精度べき乗)
;   結果 = EXP(指数 * LOG(|底|)) に底の符号規則を適用:
;     指数 0 → 1 / 底 0 → 0 (指数の正負を問わない) /
;     負の底は指数が整数値のときのみ有効 (奇数乗で負)。非整数指数は
;     Illegal Function Call / 結果あふれは EXP 側で Overflow、下位あふれは 0。
;   指数は fsc_u へ退避 (LOG/EXP 核は fsc_u を使わない)。
impl_fp_pow:
                PSHS    X               ; 本文ポインタ退避
                JSR     fx_ensure       ; 指数 → 単精度
                LDA     FAC1
                BEQ     fpow_one        ; 指数 0 → 1
                JSR     fx_st1_fsc_u
                ; 底 (FAC2 8B+型) → FAC1 へ複写し単精度化
                PSHS    U
                LDX     #FAC1
                LDU     #FAC2
fpow_cp:
                LDA     ,U+
                STA     ,X+
                CMPX    #FAC1+8
                BNE     fpow_cp
                PULS    U
                LDA     FAC2TYPE
                STA     FACTYPE
                JSR     fx_ensure       ; 底 → 単精度
                LDA     FAC1
                BEQ     fpow_zero       ; 底 0 → 0 (指数≠0)
                CLR     fsc_flag        ; 符号フラグ (bit0=奇数乗)
                LDA     FAC1+5
                BPL     fpow_go
                ; 負の底: 指数が整数値でなければ Illegal Function Call
                CLR     FAC1+5          ; |底|
                JSR     fx_st1_fsc_x            ; FAC1 → fsc_x
                LDX     #fsc_u
                JSR     fx_ld1          ; FAC1 = 指数
                JSR     float_to_int_trunc ; D = trunc(指数)
                STB     fsc_flag        ; bit0 = 奇偶
                JSR     impl_int_to_fac
                JSR     impl_int_to_float ; FAC1 = float(trunc(指数))
                JSR     fx_ld2_u
                JSR     fcmp_core       ; 一致 (Z) なら整数指数
                BNE     fpow_err
                JMP     ppb_have        ; |底| は fsc_x に退避済
fpow_go:
                ; 本体はプールへ (正の整数指数は反復乗算・他は EXP/LOG 合成)。
                ; FAC1 = |底| (正の底経路。負の底は上で fsc_x へ退避済)。
                JMP     pool_pow_body
fpow_sgn:
                LDA     fsc_flag
                LSRA                    ; 奇数乗?
                BCC     fpow_ret
                JSR     impl_fac_negate ; 負の底の奇数乗 → 負
fpow_ret:
                PULS    X,PC
fpow_one:
                LDD     #1
                FCB     $8C             ; CMPX # (次の CLRA/CLRB を読み飛ばす)
fpow_zero:
                CLRA
                CLRB
fpow_int:
                JSR     impl_int_to_fac
                PULS    X,PC
fpow_err:
                JMP     errc_5

;==============================================================================
; 超越関数
;   SIN/COS/TAN/ATN/SQR/LOG/EXP の本体は別領域 (cmt_dispatch 近傍) に置く。
;==============================================================================

; ここには機械語カセット I/O 連携サービス入口への 3 バイト トランポリンのみを
; 置く。
;
;   機械語は BIOS の間接ベクタ (2 バイト) 経由で 1 バイト単位のテープ読み取り等を
;   要求する。制御ブロック ($00,X=コマンド / $01,X=結果) 形式。入口は参考書籍に
;   載る間接ベクタ 1 か所だけである。
                include "7t_using_scan.inc"     ; PRINT USING 書式走査ループ (プール)
; (7t_using_exp2.inc は 7t_color_video.inc に置く)
;------------------------------------------------------------------------------
; igc_fixarr: GET@/PUT@ の配列アドレスを引数解析後に再解決する。
;   オプション引数の式評価が未定義変数を新規作成すると、スカラ域が伸びて配列
;   ブロック全体が後方へ移動する。解析中に確定させた gp_arr_ptr はその瞬間に
;   陳腐化し、そのまま書き戻すと配列ヘッダや隣接する別の配列を破壊する。
;   記録時 ARYTAB からのオフセットを現在の ARYTAB へ結び直して救済する。
;   gp_arr_ptr が記録時 ARYTAB より下 (= スカラ域を指す防御経路) なら補正しない。
;   破壊: D (CC)。
;------------------------------------------------------------------------------
igc_fixarr:
                LDD     gp_arr_ptr
                BEQ     igfa_ret                ; 配列引数なし
                SUBD    gp_arr_base             ; D = 記録時 ARYTAB からのオフセット
                BCS     igfa_ret                ; 下位 = スカラ域 → 補正対象外
                ADDD    ARYTAB                  ; 現在の配列ブロックへ結び直す
                STD     gp_arr_ptr
igfa_ret:
                RTS

;------------------------------------------------------------------------------
; str_cat_alloc — 連結結果の一時領域を確保する。
;
;   連結は左結合なので `A$+B$+C$+D$` は `((A$+B$)+C$)+D$` と組まれ、途中結果が
;   1 段ごとに一時領域へ積まれる。積んだままにすると、文の切れ目まで一時が
;   返らないので、瞬間使用量が **途中結果の長さの総和** に膨らみ、文字列領域を
;   絞ったプログラムが Out Of String Space で止まる。この式は最終長ぶんの空きが
;   あれば通るようにする。
;
;   そこで、連結の両辺のうち一方が一時領域の実体なら、確保の前にそのアドレスまで
;   一時を返してから取り直す (sca_intmp)。結果領域はそのアドレスから始まるので、
;   途中結果が 1 段ごとに使い回され、瞬間使用量が最終長で頭打ちになる。
;
;     ・左辺が一時 — 左結合の連鎖 `((a+b)+c)+d` がこの形。結果は左辺と同じアドレス
;       から組む。左辺の複写は 複写先 = 読出元 (同一アドレス) なので前進複写で崩れず、
;       右辺は左辺より後に評価されるため、右辺が一時ならそのアドレスは必ず左辺の実体
;       末尾より上にある。複写先 (左辺先頭 + 左長) は読出元以下になるので、
;       こちらも前進複写で崩れない。
;     ・左辺が一時でなく右辺が一時 — 右結合の連鎖 `a+(b+(c+d))` がこの形。各段の
;       左辺は恒久ブロックか本文中の定数で、途中結果は必ず右辺に来る。結果領域は
;       右辺の実体と同じアドレスから始まるため、左辺の複写先が右辺の実体を踏む。
;       そこで確保の直後に **右辺の実体を左辺の長さぶん上へ後退複写して退かし**、
;       STRPTR をその新しいアドレスへ差し替える。呼出側は左→右の順に前進
;       複写すればよい (右辺は自分自身への複写になる)。右辺より上に在る一時は
;       右辺の評価が終わった時点でどれも使われない (右辺より後に確保されたものしか
;       無い) ので、まとめて返してよい。
;     ・どちらも一時でないときは、一時領域の上へ素直に積む。
;
;   返すのは片方の実体のアドレスより上だけなので、外側の式が抱えている一時
;   (= その実体より前に確保されたもの) には手が届かない。どちらの辺も、恒久ブロック
;   (FRETOP より上) や本文中の定数 (STRBOT より下) のときは結果領域と重ならない。
;
;   入力: B = 合計長 / C = 合計長の桁上がり (呼出側 ADDB の結果)
;         呼出側の枠 3,S = 左 STRLEN, 4,S:5,S = 左 STRPTR (PSHS B の後の位置)
;   出力: X = 結果領域の先頭。破壊: D。右辺を退かしたときは STRPTR も更新する。
;------------------------------------------------------------------------------
str_cat_alloc:
                BCS     sca_long        ; 255 超 → String Too Long
                PSHS    B               ; 合計長を退避 (以降 枠が 1 ずれる)
                LDB     3,S             ; 左 STRLEN
                BEQ     sca_right       ; 空文字列は実体を持たない → 右辺を見る
                LDD     4,S             ; 左 STRPTR
                JSR     sca_intmp
                BCC     sca_done        ; 左辺が一時 → そのアドレスから組み直す
sca_right:
                LDB     STRLEN          ; 右 STRLEN
                BEQ     sca_done        ; 空文字列は実体を持たない
                LDD     STRPTR          ; 右 STRPTR
                JSR     sca_intmp
                BCS     sca_done        ; 右辺も一時でない → 素直に積む
                ; 右辺が一時。結果は右辺と同じアドレスから組むので、先に右辺の実体を
                ;   左辺の長さぶん上へ退かす (複写先が読出元より上なので後退複写)。
                PULS    B               ; 合計長を戻す
                JSR     str_tmp_alloc   ; X = 結果領域 (= 右辺の元のアドレス)
                PSHS    X
                LDB     4,S             ; 左 STRLEN
                ABX                     ; X = 右辺の新しいアドレス
                JSR     str_cpy_in      ; STRPTR から STRLEN を後退複写 (X 保存)
                STX     STRPTR          ; 右辺は左辺の直後へ移った
                PULS    X,PC            ; X = 結果領域の先頭
sca_done:
                PULS    B               ; 合計長を戻す
                JMP     str_tmp_alloc   ; 足りなければ Out Of String Space
sca_long:
                LDB     #15             ; String Too Long
                JMP     impl_error_handler
