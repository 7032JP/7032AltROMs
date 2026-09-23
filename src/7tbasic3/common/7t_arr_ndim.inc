; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa

;==============================================================================
; 配列の多次元機構
;
; DIM 文の配列は次元数に小さな固定上限を持たない。
; 本実装は次元数を決め打ちせず、宣言された次元数のぶんだけ繰り返して扱う。
;
; 「次元の要素数の表」と「添字の表」は次元数ぶん要るので、両方の表を CPU
; スタックに置き、その先頭アドレスだけを RAM (arr_sptr / arr_dptr) に持つ。
; 添字は gvr_array が CPU スタックへ積み、次元も DIM 解析が式評価の間だけ
; 抱えていればよいので、生存期間はスタックの寿命と一致する。
;
; ---- 要素の並びと次元欄の並び ----
;   要素は「最初の添字が最も速く変わる」順に並ぶ。第 k 添字の要素数を dk と
;   すると
;       通し番号 = i0 + i1*d0 + i2*d0*d1 + ...
;   エントリの次元欄は「最後の添字の要素数」から順に並ぶ (s0=d(n-1), s1=d(n-2),
;   ..., s(n-1)=d0)。この並びのおかげで通し番号は次元欄を先頭から辿るだけの
;   ホーナー法で求まる:
;       acc = i(n-1)
;       acc = acc*s1 + i(n-2) ; acc = acc*s2 + i(n-3) ; ... ; acc = acc*s(n-1) + i0
;   添字表も同じ向き (低位アドレスから i(n-1), i(n-2), ..., i0) に積むので、
;   次元欄と添字表を同じ向きに 1 本の繰り返しで歩ける。
;
; 次元数の上限は ARR_MAXDIM。超えた宣言/参照は Subscript Out Of Range (9)。
; 上限値は 7t_defs.inc の ARR_MAXDIM を参照。
;==============================================================================

;------------------------------------------------------------------------------
; arr_resolve: 添字表 (arr_sptr/arr_nsub) と VNPTR/VNLEN/VARTYP から
;   要素アドレスを U に返す。未宣言なら各次元 11 要素 (0..10) で自動確保する。
;------------------------------------------------------------------------------
arr_resolve:
                JSR     arr_find
                BEQ     ares_auto       ; Z=1 (未発見) → 自動 DIM
ares_have:
                LDX     arr_entry
                JSR     arr_entry_size  ; arr_ndim/VARTYP/arr_elsz 設定 (X 保存)
                JSR     arr_hdrskip     ; X = len 欄先頭
                LEAX    3,X             ; X = 次元欄先頭 (len2 + ndim1 の次)
                BSR     arr_chk_subs    ; 添字の値域検査 (X は保存される)
                PSHS    X               ; [S] = 次元欄先頭
                LDB     arr_ndim
                ASLB
                ABX                     ; X = 要素領域先頭
                PSHS    X               ; [S]=要素領域先頭 [2,S]=次元欄先頭
                ; --- 通し番号をホーナー法で求める ---
                LDU     arr_sptr
                LDD     ,U++            ; acc = 添字表の先頭 (= 最後の添字)
                STD     arr_idx
                LDX     2,S
                LEAX    2,X             ; 次元欄は s1 から使う (s0 は掛からない)
                LDB     arr_ndim
                DECB
                BEQ     ares_idx_done   ; 1 次元 → 添字そのものが通し番号
                PSHS    B               ; 残り回数
ares_idx_lp:
                LDD     arr_idx
                STD     arr_mul_a
                LDD     ,X++            ; s(j)
                STD     arr_mul_b
                PSHS    X               ; arr_mul は X を積の累算に使う
                JSR     arr_mul         ; D = acc * s(j)
                PULS    X
                ADDD    ,U++            ; + 次の添字
                STD     arr_idx
                DEC     ,S
                BNE     ares_idx_lp
                LEAS    1,S             ; 回数を捨てる
ares_idx_done:
                ; バイトオフセット = 通し番号 * 要素サイズ
                LDD     arr_idx
                STD     arr_mul_a
                LDB     arr_elsz
                CLRA
                STD     arr_mul_b
                JSR     arr_mul
                ADDD    ,S++            ; + 要素領域先頭
                TFR     D,U
                LEAS    2,S             ; 次元欄先頭を捨てる
                RTS
ares_auto:
                ; 自動 DIM (未宣言配列): 各次元 11 要素 (0..10) で確保する。
                ; 次元表は CPU スタックへ積む (添字表より下位に積まれるので
                ; arr_sptr は無効にならない)。
                LDA     arr_nsub
                STA     arr_ndim
                LDB     arr_nsub
ares_au_lp:
                LDX     #11
                PSHS    X
                DECB
                BNE     ares_au_lp
                TFR     S,U
                STU     arr_dptr
                BSR     arr_alloc       ; VARTYP は gvr_array が設定済
                LDB     arr_nsub
                ASLB
                LEAS    B,S             ; 次元表を捨てる
                JSR     arr_find
                BRA     ares_have

;------------------------------------------------------------------------------
; arr_chk_subs: 添字が確保済みの値域に収まっているかを検査する。
;   DIM D(n) が確保するのは添字 0..n の n+1 個
;   であり、その外側の添字は誤りである。
;   入力: X = エントリ内の次元欄先頭, arr_ndim, arr_nsub, arr_sptr
;   出力: 正常なら素通りする。X は保存する (呼出元が使い続ける)。
;   誤り:
;     添字が負                                → Illegal Function Call (5)
;     添字が要素数以上 / 添字の個数が次元数と違う → Subscript Out Of Range (9)
;------------------------------------------------------------------------------
arr_chk_subs:
                LDA     arr_nsub
                CMPA    arr_ndim
                BNE     arr_err_sor     ; 添字の個数が宣言と違う
                PSHS    X
                LDU     arr_sptr
                LDA     arr_ndim
                PSHS    A               ; 残り回数
acs_lp:
                LDD     ,U++            ; 添字 (添字表と次元欄は同じ並び)
                BMI     acs_fc          ; 負 → Illegal Function Call
                CMPD    ,X++            ; 要素数 (= 宣言値+1) と比較
                BHS     arr_err_sor     ; 上限超過 → Subscript Out Of Range
                DEC     ,S
                BNE     acs_lp
                LEAS    1,S
                PULS    X,PC
acs_fc:
                JMP     errc_5          ; Illegal Function Call
; 添字/次元に関する Subscript Out Of Range の共通出口 (誤り時は復帰しないので、
; 積んだ作業値の始末は実行ループの巻戻し (EXEC_SP) に委ねる)。
arr_err_sor:
                LDB     #9
                JMP     impl_error_handler

;------------------------------------------------------------------------------
; arr_total_elems: 次元表 (arr_dptr) の要素数 arr_ndim 個の積を D に返す。
;------------------------------------------------------------------------------
arr_total_elems:
                LDX     arr_dptr
                LDD     ,X++
                STD     arr_idx
                LDA     arr_ndim
                DECA
                BEQ     ate_done
                PSHS    A               ; 残り回数
ate_lp:
                LDD     arr_idx
                STD     arr_mul_a
                LDD     ,X++
                STD     arr_mul_b
                PSHS    X               ; arr_mul は X を壊す
                JSR     arr_mul
                PULS    X
                STD     arr_idx
                DEC     ,S
                BNE     ate_lp
                LEAS    1,S
ate_done:
                LDD     arr_idx
                RTS

;------------------------------------------------------------------------------
; arr_alloc: 新規配列を STREND (配列領域末尾 = プログラム/スカラ変数の直後
;   から上方) に確保する (上方確保レイアウト)。
;   エントリ: [型|名前長][名前][len(2)][ndim][s0(2)]..[s(n-1)(2)][要素... ゼロ]
;   入力: arr_ndim / arr_dptr (次元表) / VNPTR・VNLEN・VARTYP
;   OM 検査: 新末尾がシステム S の手前 STK_RESERVE を侵すなら Out Of Memory
;   (alloc_limit 参照。システムスタックの上端は文字領域の下限なので、S が
;   未使用領域の境界である)。
;------------------------------------------------------------------------------
arr_alloc:
                JSR     arr_elem_bytes  ; D = 要素バイト数 (arr_elsz も設定)
                ; len 値 = 3 (len欄2+ndim1) + ndim*2 + 要素バイト
                ADDD    #3
                PSHS    D
                LDB     arr_ndim
                ASLB
                CLRA
                ADDD    ,S++            ; D = len 値
                PSHS    D               ; len を退避
                ADDB    VNLEN
                ADCA    #0
                ADDD    #1              ; + ヘッダ 1B + 名前長 = エントリ総バイト数
                ADDD    STREND          ; D = 新末尾候補
                BCS     aalc_oom        ; 16bit 折返し → OM
                STD     ARR_NEWEND
                JSR     alloc_limit     ; D = S − STK_RESERVE
                CMPD    ARR_NEWEND
                BLO     aalc_oom        ; 上限 < 新末尾 → OM
                ; 確保確定
                LDX     STREND          ; X = 新エントリ先頭
                LDD     ARR_NEWEND
                STD     STREND
                ; ヘッダ書込 ([型|名前長] + 名前本体)
                LDA     VNLEN
                ORA     VARTYP
                STA     ,X+
                JSR     vn_copy         ; 名前本体を ,X+ へ複写 (X 前進, U 破壊)
                PULS    D               ; len
                STD     ,X++
                LDA     arr_ndim
                STA     ,X+
                ; 次元欄 (並びは次元表と同じなので先頭から素直に複写する)
                LDU     arr_dptr
                LDB     arr_ndim
aalc_dim_lp:
                LDA     ,U+
                STA     ,X+
                LDA     ,U+
                STA     ,X+
                DECB
                BNE     aalc_dim_lp
                ; 要素領域をゼロクリア (総要素数 × elsz バイト)
                ; arr_elem_bytes は内部の arr_mul が X を積のアキュムレータに
                ;   使うため X (要素領域先頭) を破壊する。退避して呼ぶ。
                PSHS    X               ; 要素領域先頭を退避
                JSR     arr_elem_bytes  ; D = 要素バイト数 (X 破壊)
                TFR     D,Y             ; Y = 残バイト
                PULS    X               ; X = 要素領域先頭を復元
aalc_zero:
                LEAY    ,Y
                BEQ     aalc_done
                CLR     ,X+
                LEAY    -1,Y
                BRA     aalc_zero
aalc_done:
                RTS
aalc_oom:
                JMP     err_rg          ; Out Of Memory

;------------------------------------------------------------------------------
; impl_decl_array: DIM 宣言 (配列確保)
;   DIM NAME(d0[,d1...])[, NAME2(...)] : 0..dK の (dK+1) 要素を確保。
;   複数宣言はカンマ区切り。型は名前サフィックス/DEFTYPE で決定。
;   添字なしの単純変数も受理する (例: DIM ZX,ZY,D(7))。単純変数は生成する
;   だけで既存の値は変えない。gvr_find_scalar (検索/新規作成) の末尾呼びで
;   実現する。
;   既に存在する配列を宣言し直したら Duplicate Definition (10)。
;   参照で自動確保された配列も「存在する」に含む。ERASE 済みなら
;   もう存在しないので改めて宣言できる。
;------------------------------------------------------------------------------
ida_arr:
                ; 添字の式評価は変数を読むため parse_var_name を経由し、
                ;   VNPTR/VNLEN/VARTYP を「添字側の変数の名前と型」で上書きする。
                ;   退避せずに arr_alloc を呼ぶと、添字に使った変数の名前で配列が
                ;   作られてしまうので、要素参照 (gvr_array) と同じく名前を
                ;   退避・復元する。
                ;   退避先は CPU スタック (添字が別の配列参照でも壊れない)。
                LDU     VNPTR
                PSHS    U
                LDA     VNLEN
                LDB     VARTYP
                PSHS    D
                ; 各次元サイズ (宣言値+1 = 要素数) を CPU スタックへ積む。
                ; 次元式に配列参照 (例 DIM Q(Z(0))) が現れても、積んだ値は
                ;   スタック上にあるので被参照配列の解決に壊されない。
                ;   数えている次元数も同じ理由で共有域 (arr_ndim) には置けない
                ;   (被参照配列の arr_entry_size が上書きする)。スタック最上位の
                ;   1 byte に置く。式評価はスタックを釣り合わせて戻るので、
                ;   評価を跨いでも最上位のままである。
                CLRA
                PSHS    A               ; [S] = ここまでの次元数
ida_dim_lp:
                JSR     eval_to_int     ; 式評価→整数化 (共通化)
                TSTA                    ; 負の宣言値は誤り
                LBMI    errc_5          ; DIM D(-1) → Illegal Function Call
                ADDD    #1              ; 0-origin → 要素数
                TFR     D,U             ; 要素数を退避 (最上位の次元数を取り出す)
                PULS    A               ; A = ここまでの次元数
                INCA
                PSHS    A,U             ; 要素数を積む
                CMPA    #ARR_MAXDIM
                LBHI    arr_err_sor     ; 次元が多すぎる
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     ida_dim_done
                LEAX    1,X
                BRA     ida_dim_lp
ida_dim_done:
                PULS    A
                STA     arr_ndim        ; 入れ子は済んだので共有域へ書いてよい
                LEAX    1,X             ; ')' を消費
                STX     arr_xsave
                TFR     S,U
                STU     arr_dptr        ; 次元表 = 積んだ順の逆 = 次元欄の並び
                LDB     arr_ndim
                ASLB
                LEAU    B,U             ; U = 退避した名前長/型の位置
                LDA     ,U
                STA     VNLEN
                LDA     1,U
                STA     VARTYP
                LDU     2,U
                STU     VNPTR           ; 配列名の本文位置を復元
                JSR     arr_find        ; 既に在る配列の宣言し直しは誤り
                BNE    ida_dup
                JSR     arr_alloc       ; 次元表/arr_ndim/VNAME* から確保
                LDB     arr_ndim
                ASLB
                ADDB    #4              ; 次元表 + 名前長/型 (2) + 本文位置 (2)
                LEAS    B,S
                LDX     arr_xsave
                ; 次の宣言 (',' 区切り) があるか
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     ida_done
                LEAX    1,X
                ; 宣言処理が伸びて短分岐が届かない (ここへ展開)
impl_decl_array:
ida_one:
                ; 変数名+型を解析 (VNPTR/VNLEN/VARTYP を設定, X を '(' へ)
                JSR     parse_var_name
                LDA     ,X+             ; '(' を消費
                CMPA    #'('
                BEQ     ida_arr         ; '(' あり → 配列宣言
                LEAX    -1,X            ; 区切りを戻す (',' は impl_dim 側が処理)
                JMP     gvr_find_scalar ; 単純変数: 生成のみ (X 保存・末尾呼び)
ida_done:
                RTS
ida_dup:
                LDB     #10
                JMP     impl_error_handler ; Duplicate Definition

;------------------------------------------------------------------------------
; gvr_array — 配列要素の参照
;------------------------------------------------------------------------------
gvr_array:
                ; 配列参照 NAME(i0[,i1...]) : 添字を収集 → 配列を検索/自動確保 →
                ; 要素アドレスを U に, X を ')' の次に進めて返す。
                ; 添字の式が別の配列参照 (例 A(B(0))) を含むと、入れ子で走る
                ;   この処理自身が arr_nptr / arr_nlen / arr_vty / 添字表 /
                ;   arr_nsub を上書きしてしまい、外側は内側の配列名で引いてしまう
                ;   (DIM A(5),B(5) : B(0)=2 : A(2)=99 : PRINT A(B(0)) が
                ;    99 でなく 2 を返す)。退避先を CPU スタックへ移し、共有の
                ;   作業域へは入れ子が終わってから書く。
                ; 添字は個数を決め打ちせず CPU スタックへ積み続ける。積んだ並び
                ;   (低位アドレスから 最後の添字 → 最初の添字) がそのままエントリの
                ;   次元欄の並びと揃うので、arr_resolve は両者を同じ向きに 1 本の
                ;   繰り返しで歩ける。
                LDU     VNPTR
                PSHS    U               ; 配列名の本文位置
                LDA     VNLEN
                LDB     VARTYP
                PSHS    D               ; 名前長 / 型
                ; 収集中の個数も共有域 (arr_nsub) には置けない。添字の式が
                ;   別の配列参照なら入れ子で arr_nsub を書き換えてしまう
                ;   (PRINT A(B(0)) が 2 個の添字を数えて Subscript Out Of Range
                ;    になる)。個数は常にスタック最上位の 1 byte に置く。式評価は
                ;   スタックを釣り合わせて戻るので、評価を跨いでも最上位のまま。
                CLRA
                PSHS    A               ; [S] = 収集した添字の個数
                LEAX    1,X             ; '(' をスキップ
gar_lp:
                JSR     eval_to_int_round ; 添字は整数を要求する文脈
                TFR     D,U             ; 添字を退避 (最上位の個数を取り出すため)
                PULS    A               ; A = ここまでの個数
                INCA
                PSHS    A,U             ; 添字を積む
                CMPA    #ARR_MAXDIM
                LBHI    arr_err_sor     ; 添字が多すぎる
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     gar_fin
                LEAX    1,X
                BRA     gar_lp
gar_fin:
                PULS    A
                STA     arr_nsub        ; 入れ子は済んだので共有域へ書いてよい
                LEAX    1,X             ; ')' をスキップ
                STX     arr_xsave       ; 本文ポインタ退避
                TFR     S,U
                STU     arr_sptr        ; 添字表 = 積んだ順の逆
                LDB     arr_nsub
                ASLB
                LEAU    B,U             ; U = 退避した名前長/型の位置
                LDA     ,U
                STA     VNLEN
                LDA     1,U
                STA     VARTYP
                LDU     2,U
                STU     VNPTR           ; 配列名の本文位置を復元
                JSR     arr_resolve     ; U = 要素アドレス
                LDB     arr_nsub
                ASLB
                ADDB    #4              ; 添字表 + 名前長/型 (2) + 本文位置 (2)
                LEAS    B,S             ; U は保たれる
                LDX     arr_xsave
                RTS
