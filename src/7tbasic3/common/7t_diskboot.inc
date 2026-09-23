; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 7t_diskboot.inc — 低位 RAM の BIOS ジャンプの設置、BIOS 入口のフロッピィ関係の
;   リクエスト、ディスク (2D) の ASCII 形式の LOAD / SAVE / FILES
;
;   参考書籍 (BIOS と、ミニフロッピィ
;   ディスクの構成: FAT / ディレクトリ / クラスタ) に基づく。
;==============================================================================

;------------------------------------------------------------------------------
; cfd_disk — BIOS 入口のフロッピィ関係リクエスト (8 / 9 / 10。番号の表から JSR、X = RCB)
;
;   参考書籍のリクエスト番号表のうち、フロッピィの 3 つ
;     8 = RESTOR / 9 = DWRITE / 10 = DREAD
;   は、起動 ROM の BIOS 処理アドレス $FE02 / $FE05 / $FE08
;   (参考書籍) へそのまま渡す。
;   RCB は参考書籍の割付のまま X で渡し、結果 (RCB+1 のエラー番号 (参考書籍
;   の 10〜15) とキャリー) もそのまま呼出側へ返す。直接の呼出しは DP=$FD で行う
;   (参考書籍)。DP は入口の枠で元に戻る。
;   表に載る他の番号は未実装のため、2 (Device Unavailable) を RCBSTA へ返す
;   (dmd_unavail)。誤りのときはキャリーを 1 にする (参考書籍) ので、既定応答も
;   無言の成功ではなくキャリーを立てて返す (bios_rcb_ret)。
;------------------------------------------------------------------------------
cfd_disk:
                IFNDEF  STAGE_ALT_RAM
                LDA     ,X              ; リクエスト番号
                SUBA    #$08            ; 0 / 1 / 2
                LDB     #3
                MUL                     ; D = 3 × (番号 − 8)
                LDU     #$FE02          ; RESTOR の処理アドレス。DWRITE / DREAD は 3 バイトずつ後ろ
                LEAU    D,U
                LDA     #$FD
                TFR     A,DP            ; DP = $FD (直接の呼出しの条件)
                JMP     ,U              ; RCB+1 とキャリーは起動 ROM が設定し、RTS で入口へ戻る
                ENDC

dmd_unavail:
                LDB     #2              ; 未実装 → Device Unavailable (参考書籍)
                                        ;   (bios_rcb_ret へはそのまま落ちる)
;------------------------------------------------------------------------------
; bios_rcb_ret — BIOS の共通の出口。参考書籍が定める復帰の作法を 1 か所に置く。
;   入力: B = 参考書籍の誤り番号 (0 = 正常)、X = RCB 先頭。
;   処理: B を RCBSTA (相対 1) へ書き、キャリーへ誤りの有無を写して、番号の表から
;   JSR した入口へ戻る (レジスタの枠は入口が降ろす)。
;   今後リクエスト番号を足すときは、番号ごとの処理の末尾で B に誤り番号を入れて
;   ここへ分岐すれば、相対 1 とキャリーの作法が自動的に揃う。
;------------------------------------------------------------------------------
bios_rcb_ret:
                STB     1,X             ; RCBSTA (B=0 なら Z=1)
                ANDCC   #$FE            ; キャリー = 0 (Z は保たれる)
                BEQ     brr_done        ; RCBSTA = 0 → 誤り無し
                ORCC    #$01            ; 誤り有り → キャリー = 1 (参考書籍)
brr_done:
                RTS

;------------------------------------------------------------------------------
; cmt_thresh_clamp — cmt_read_byte の適応閾値クランプ
;   (置き場所は容量の都合。参照はラベル経由のみ)
;------------------------------------------------------------------------------
cmt_thresh_clamp:
                CMPX    #CRB_THRESH_LO
                BHS     ctc_hi
                LDX     #CRB_THRESH_LO  ; 下限クランプ
ctc_hi:
                CMPX    #CRB_THRESH_HI
                BLS     ctc_rts
                LDX     #CRB_THRESH_HI  ; 上限クランプ
ctc_rts:
                RTS

;------------------------------------------------------------------------------
; impl_on_entry — ON 文の入口
;   cmd_jt の $97 はここを指す。`ON INTERVAL GOSUB <行>` だけを先に引き取り、
;   それ以外は impl_on (選択値を評価する ON) へ渡す。
;
;   INTERVAL を先に見るのは、選択値の式として評価させられないため。
;   `ON INTERVAL GOSUB` の INTERVAL は値ではなく割込み源の名前で、式評価に
;   落とすとプライマリになり得ない綴りとして弾かれる。
;   `ON INTERVAL=60 GOSUB` のように '=' を挟む形は参考書籍に無いので、
;   GO/SUB の照合で外れて Syntax Error になる。
;
;   行番号の存在検査に加えて、分岐先として intv_line へ登録する。
;     発生時の経路は iep_stmt_gate (下) を参照。
;   区画の都合でこのプールへ置く。参照はラベル経由のみで位置依存はない。
;------------------------------------------------------------------------------
impl_on_entry:
                JSR     skip_sp_x
                CMPA    #TOK_INTERVAL   ; INTERVAL
                BEQ     ione_intv
                CMPA    #TOK_KEY        ; KEY (ON KEY(n) GOSUB)
                LBEQ    impl_on_key     ; 本体は別プール (7t_core.s 末尾)
                CMPA    #TOK_FUNC_PFX   ; TIME は関数語トークン ($FF xx)
                BNE     ione_plain
                LDA     1,X             ; 第 2 バイト
                CMPA    #TOK2_TIME
                BEQ     ione_time       ; TIME 以外の関数語は選択値の式として扱う
ione_plain:
                JMP     impl_on         ; 通常の ON (先頭で空白を読み直すだけ)
ione_time:
                LEAX    1,X             ; 前置バイトを消費 (第 2 バイトは下で)
                LDU     #timer_line
                BRA     ione_go
ione_intv:
                LDU     #intv_line
ione_go:
                JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
                CMPA    #'$'                    ; TIME$ と綴った形も同じ文
                BNE     ione_nodol
                JSR     skip1_sp_x
ione_nodol:
                LDA     ,X+
                CMPA    #TOK_GO         ; GO
                BNE     ione_syn
                JSR     skip_sp_take
                CMPA    #TOK_SUB        ; SUB (GOSUB のみ。GOTO は不可)
                BNE     ione_syn
                JSR     skip_sp_x       ; A = 行番号の先頭
                CMPA    #'0'
                BLO     ione_syn        ; 行番号が無い
                CMPA    #'9'
                BHI     ione_syn
ione_line:
                JSR     impl_eval_linenum       ; D = 行番号, X 前進
                PSHS    X               ; impl_find_line は X (本文ポインタ) を壊す。
                JSR     impl_find_line  ;   退避しないと復帰後に飛び先の行を実行し始める
                PULS    X               ;   (PULS は CC 不変なので Z は保たれる)
                BEQ     isg_undef
                STD     ,U              ; 分岐先として登録 (impl_find_line は D 温存)
                RTS
ione_syn:
                JMP     ion_err                 ; Syntax Error (尾部併合)

;------------------------------------------------------------------------------
; INTERVAL 割込の発生時の分岐と状態遷移
;   参考書籍の INTERVAL 文 / ON INTERVAL GOSUB 文の意味論を実装する:
;     - `INTERVAL <n>` は割込間隔を n 秒に設定する (n=0 は Illegal Function
;       Call。負値は整数化の前に Illegal Function Call とする)
;     - `INTERVAL ON` は割込を許可する。禁止 (OFF) からの許可は計数を最初から
;       やり直し、保留していた発生分は捨てる。保留 (STOP) からの許可は計数を
;       続け、保留していた発生分 (高々 1 件) を直後の文の切れ目で分岐する
;     - `INTERVAL OFF` は割込を禁止する。計数を止め、保留中の発生分も捨てる
;     - `INTERVAL STOP` は分岐だけを保留する。計数は続き、間隔満了は 1 件に
;       畳んで保持される
;     - 発生した割込はプログラム実行中の文の切れ目でのみ分岐し、登録行への GOSUB と
;       して入る (復帰フレームは中断した文の先頭)。ハンドラ実行中は自動的に
;       保留となり、RETURN で解除される (= 入れ子にならない)
;     - 直接モード (Ready) では分岐しない。間隔未設定のままの許可は発生しない
;   周期の計数はタイマ割込ハンドラ (7t_time.inc: tick_irq_src) が行い、
;   ここでは保留フラグ intv_pend を見るだけ。状態 intv_st の値は
;   0=禁止 / 1=許可 / bit1=STOP / bit2=ハンドラ実行中 (分岐は 1 のときだけ)。
;------------------------------------------------------------------------------
; iep_stmt_gate — 実行ループの文の切れ目 (iep_stmt_new から JSR)。
;   本務は str_tmp_clear (一時文字列の一括開放) で、呼出契約も同じ
;   (破壊: D。分岐しない限り X = 文の先頭を温存)。INTERVAL と機能キーの
;   発生保留があればここで GOSUB として分岐する。
iep_stmt_gate:
                JSR     key_attn_poll   ; 機能キーの発生を取り込む (X 温存)
                JSR     brk_gate        ; 本務を先に (D 破壊, X 温存)
                ; 分岐判定へ落ちる (X = 文の先頭)
; iep_jump_gate — INTERVAL 発生時の分岐判定と GOSUB 突入
;   入口は 2 つ:
;     - iep_stmt_gate (上): 文の切れ目。X = これから実行する文の先頭
;     - impl_jump_to_line (7t_expr.inc): 行ジャンプ直後。X = 飛び先の文頭
;   いずれも「X の文を実行する直前」であり、分岐時は X を復帰 IP として
;   GOSUB フレームを積み、登録行へ制御を移す (RETURN で X の文からやり直す)。
;   分岐しないときは X を温存して RTS (str_tmp_clear と同じ呼出契約)。
;   割込の源ごとに欄 (INTERVAL / TIMER) の発生保留を順に確かめる。保留の無い
;   欄は素通りするので、機能キーの保留だけで入ってきたときに別の源の登録行へ
;   誤って分岐することはない。
iep_jump_gate:
                LDU     #intv_st        ; INTERVAL の欄
                BSR     ijg_try
                LDU     #timer_st       ; TIMER の欄 (参考書籍 ON TIME GOSUB)
                BSR     ijg_try
                LBRA    ijg_key         ; どちらも分岐しない → 機能キーの保留を見る
; ijg_try — U = 割込テーブルの 1 欄。分岐する条件が揃っていれば呼出元へ戻らず
;   登録行へ GOSUB として入る (BSR の戻りアドレスは捨てる)。破壊: A,B,U
ijg_try:
                TST     2,U             ; +2 = 発生保留があるか
                BEQ     ijg_rts
                TST     ,U              ; +0 = ON 状態か
                BEQ     ijg_rts
                TST     1,U             ; +1 = STOP / 割込処理ルーチン実行中か
                BNE     ijg_rts
                LDD     3,U             ; +3,+4 = 飛び先行番号
                BEQ     ijg_drop        ; 飛び先未登録 → 発生分は捨てる
                LEAS    2,S             ; 以降は戻らないので BSR の戻りアドレスを捨てる
                PSHS    D               ; 行番号を退避 (以下 A/D を使うため先に積む)
                CLR     2,U             ; 保留は 1 件に畳んで分岐
                LDA     #2
                STA     1,U             ; 割込処理ルーチン実行中 (RETURN で解除)
                JSR     gosub_push      ; 復帰枠 = 中断する文の先頭 (X)。溢れは Out Of Memory
                PULS    D
                JSR     impl_find_line  ; D = 登録行 → X = 行レコード (存在は登録時に確認済
                BEQ     isg_undef       ;   だが、実行中の DELETE 等に備えて防御)
                JMP     impl_jump_to_line ; CURLIN/CURIP/X を飛び先へ (保留は消してあるので
                                        ;   再入しても素通り)。復帰先は呼出元の実行ループ
ijg_drop:
                CLR     2,U
ijg_rts:
                RTS
isg_undef:
                JMP     irst_undef      ; Undefined line number (尾部併合)

; impl_return_entry — RETURN の入口 (cmd_jt $8B)
;   ON〜GOSUB 割込テーブルの +1 が 2 (割込処理ルーチン実行中) の欄を倒してから
;   本体へ。走査するのは KEY(n) ($073C) から INTERVAL ($0773) までの 12 件で、
;   1 = STOP は倒さない。RETURN が実行中の印 (2) を倒す。
impl_return_entry:
                ; RETURN [行番号] (参考書籍 RETURN)。行番号の指定があるときは
                ;   その行へ復帰し、無いときは GOSUB の直後の文へ復帰する。
                ;   控えは LIST / EDIT の開始行番号の枠を借りる (プログラムの実行中に
                ;   LIST / EDIT が走ることはないので時間排他)。0 = 指定なし。
                JSR     skip_sp_x
                CMPA    #'0'
                BLO     pret_noln
                CMPA    #'9'
                BHI     pret_noln
                JSR     impl_eval_linenum       ; D = 行番号, X 前進
                BRA     pret_stln
pret_noln:
                CLRA
                CLRB
pret_stln:
                STD     >il_start_ln
                LDX     #key_line
ire_lp:
                LDA     1,X             ; +1 = STOP / 割込処理ルーチン実行中
                CMPA    #2
                BNE     ire_nx
                CLR     1,X
ire_nx:
                LEAX    5,X
                CMPX    #intv_st+5
                BNE     ire_lp
                JMP     impl_return

; impl_interval — INTERVAL 文 (受理、発生処理を実装)
;   受理する 4 形式 (参考書籍):
;     INTERVAL <数式> / INTERVAL ON / INTERVAL OFF / INTERVAL STOP
;   引数はこの 1 つだけで、後に続けられるのは文の区切り (':' か行末) のみ。
;   ("INTERVAL ON,<間隔>" は参考書籍に無い形式なので Syntax Error)。
;   状態遷移の意味論は上の総説を参照。
;   (本体は区画の残量の都合でこのプールに置く。参照は cmd_jt の FDB のみ)
impl_interval:
                JSR     skip_sp_x       ; A = 引数の先頭
                CMPA    #TOK_OFF        ; OFF
                BEQ     iintv_off
                CMPA    #TOK_ON         ; ON
                BEQ     iintv_on
                CMPA    #TOK_STOP       ; STOP
                BEQ     iintv_stop
                JSR     eval_to_uaddr   ; 間隔 = 非負の数式 (整数部 1..65535)
                ADDD    #0              ; D = 0 か (CMPD #0 と同じフラグで 1 バイト短い)
                BEQ     iintv_fc        ; 0 は Illegal Function Call (動作観察)
                STD     >intv_n
                ; 許可中なら新しい間隔で計数を刻み直す。禁止/保留中は設定のみ
                ; (保留中に刻み直すと保留 1 件の分岐の時機が動くため触らない)
                TST     >intv_st        ; +0 = ON 状態
                BEQ     iintv_eos
                TST     >intv_stp       ; +1 = STOP / 実行中
                BNE     iintv_eos
                ; X は本文ポインタなので触らない (X を使うと引数の残りの検査が
                ;   飛び、"INTERVAL 10,20" の尾部が黙って捨てられる)
                LDD     >intv_n
                STD     >intv_sec
                JSR     intv_arm_tick   ; 計時を刻み直し、周期タイマの所有を取る
                BRA     iintv_eos
iintv_on:
                LEAX    1,X             ; 制御語を消費
                LDB     >intv_st        ; B = 旧 ON 状態
                LDA     #1
                STA     >intv_st        ; +0 = ON 状態
                BSR     intv_unstop     ; +1 の STOP を解除 (実行中 (2) は温存)
                TSTB
                BNE     iintv_eos       ; 許可/保留から → 計数継続・保留分は分岐へ
                ; 禁止からの許可 → 保留を捨てて計数を最初から
                CLR     >intv_pend
                LDD     >intv_n
                BEQ     iintv_eos       ; 間隔未設定なら計時は始めない (発生なし)
                STD     >intv_sec
                JSR     intv_arm_tick   ; 計時開始は最後 (タイマ割込との競合回避)
                BRA     iintv_eos
iintv_stop:
                LEAX    1,X             ; 制御語を消費
                LDA     #1
                STA     >intv_stp       ; +1 = STOP (計数は止めない)
                BRA     iintv_eos
                ; intv_unstop — +1 が 1 (STOP) のときだけ倒す。2 (割込処理ルーチン
                ;   実行中) は RETURN が倒すのでここでは触らない。破壊: A
intv_unstop:
                LDA     >intv_stp
                CMPA    #1
                BNE     iiu_rts
                CLR     >intv_stp
iiu_rts:
                RTS
iintv_off:
                LEAX    1,X             ; 制御語を消費
                CLR     >intv_st        ; +0 = ON 状態を倒す
                BSR     intv_unstop     ; +1 の STOP も倒す
                CLRA
                CLRB
                STD     >intv_sub       ; 計時停止が先 (以降タイマ割込は何もしない)
                CLR     >intv_pend      ; 保留中の発生分も捨てる
                JSR     time_hw_sync    ; 刻みが要らなくなれば周期タイマの所有を返す
iintv_eos:
                ; 引数はここまで。続きがあれば Syntax Error
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     iintv_done      ; 行末
                CMPA    #':'
                BEQ     iintv_done      ; 文の区切り
iintv_syn:
                JMP     ion_err         ; Syntax Error (尾部併合)
iintv_fc:
                JMP     errc_5
iintv_done:
                RTS

;------------------------------------------------------------------------------
; ies_alpha — 文の位置に来た $80 未満の生バイトを処理する
;   入力: A = 文の先頭 1 バイト (実行ループが $00 と ':' を除外済み), X = その直後
;   空白 ($20) は保存済み本文の行頭・文頭に残る生バイトなので読み飛ばして
;   文の走査を続ける。英字 ('A'..'Z') は '=' (または配列要素の '(') が続けば
;   真の代入なので暗黙 LET を実行し、続かなければ Syntax Error。文の位置に
;   置ける英字始まりの綴りは、予約語 (トークン化済みなのでここへは来ない) か
;   代入の左辺しかないためである。それ以外の生の記号で始まる文も Syntax Error
;   (動作観察)。
;   記号を黙って 1 バイト読み飛ばすと、`(1)` のような生の記号で始まる文が
;   無言で素通りしてしまう。
;   is_implicit_let は名前と '=' の間の空白も配列要素の '(' も見るので、
;   保存済み本文の "X = 5" や "A(1) = 5" はここでは弾かれない。
;   実行ループ側の区画 (ZMB $C29F) に残余が無いためこのプールへ置く。
;------------------------------------------------------------------------------
ies_alpha:
                CMPA    #' '            ; 文頭の空白 (SAVE 済み本文の生バイト)
                BEQ     iea_rts         ;   → 読み飛ばして文の走査を続ける
                CMPA    #'A'
                BLO     iintv_syn       ; 英字より小さい生の記号 → Syntax Error
                CMPA    #'Z'
                BHI     iintv_syn       ; 英字より大きい生の記号 → Syntax Error
                JSR     is_implicit_let ; '=' / '(' が続けば Z=1
                BNE     iintv_syn
                LEAX    -1,X            ; 名前の先頭へ戻す
                JMP     impl_let        ; 末尾呼び (復帰先は呼出元の次命令)
iea_rts:
                RTS

                include "7t_midstmt.inc"        ; MID$ 文 (区画の都合でここへ)
                include "7t_numchk.inc"        ; 値域検査つき整数化 / INT・FIX 本体

; (浮動小数乗算は MUL の部分積方式 (fmul_bmul, 7t_startup.inc 末尾プール) で
;  行うため、乗数側の 0 バイト縮約前処理は置かない)

;------------------------------------------------------------------------------
; fti_rsh8 — 浮動小数→整数の右詰めを 8 歩単位で縮約する。
;   float_to_int_trunc から JSR。A = これから落とすビット数 (0-31)。
;   1 ビットずつの 4 バイト右シフトは 1 歩 34 サイクルかかるので、8 歩ぶんは
;   「仮数を 1 バイト下へずらして上位に 0 を入れる」に置き換える (厳密に等価)。
;   小さな整数へ落とす変換では 24 歩が 3 回のバイト移動に畳まれる。
;   出力: A = 残りビット数 (0-7)。壊す: A, B。
;   直前は fmp_tzr の RTS なので、この定義へ落ち込む経路は無い。
;------------------------------------------------------------------------------
fti_rsh8:
                CMPA    #8
                BLO     ftr8_rts
                LDB     FAC1+3
                STB     FAC1+4
                LDB     FAC1+2
                STB     FAC1+3
                LDB     FAC1+1
                STB     FAC1+2
                CLR     FAC1+1
                SUBA    #8
                BRA     fti_rsh8
ftr8_rts:
                RTS

;------------------------------------------------------------------------------
; iitf_shl8 — 整数→浮動小数の正規化を 8 歩単位で縮約する。
;   impl_int_to_float の正規化ループ (iitf_norm) から JSR。
;   仮数の最上位バイトが丸ごと 0 のとき、左詰め 8 歩は「仮数を 1 バイト上へ
;   ずらして指数を 8 引く」と厳密に等価である。最上位バイトが負 (bit7=1) に
;   なり得るのはちょうど 8 歩目だけなので、途中で抜ける経路は無く、
;   指数が 8 より大きければ指数 0 での打切りにも掛からない。
;   16bit 整数由来の値は下位 2 バイトが常に 0 なので、16 歩ぶんがバイト移動
;   2 回に畳まれる。
;   復帰: C=1 = 1 バイトぶん進めた / C=0 = 指数が足りずビット単位に委ねる。
;   壊す: A。
;   直前は ftr8_rts の RTS なので、この定義へ落ち込む経路は無い。
;------------------------------------------------------------------------------
iitf_shl8:
                LDA     FAC1
                CMPA    #8
                BLS     iis8_no         ; 指数が 8 以下 → ビット単位へ委ねる
                SUBA    #8
                STA     FAC1
                LDD     FAC1+2          ; +2 → +1、+3 → +2 (ワード (2 バイト) で 1 度に)
                STD     FAC1+1
                LDA     FAC1+4
                STA     FAC1+3
                CLR     FAC1+4
                ORCC    #$01            ; C=1
                RTS
iis8_no:
                ANDCC   #$FE            ; C=0
                RTS

;==============================================================================
; pf_init_defaults — 機能キー (PF1〜PF10) の起動時既定値を定義表へ展開する
;
;   電源投入直後の機能キーには
;   よく使う命令があらかじめ割り当てられている。利用者は最初の 1 打で
;   一覧表示や実行を呼び出せる。既定値は pf_def_tbl に置く。
;
;   定義表そのものはサブシステム側の RAM にあり、メイン CPU からは直接
;   書けない。サブ CPU 側のコマンド $2A (機能キー文字列の設定) を
;   キー番号ごとに 1 回ずつ発行して展開する。
;     $D383 = キー番号 (1〜10) / $D384 = 文字数 / $D385〜 = 文字列
;   ステージング側では SUBCMD_P1 / SUBCMD_P2 / SUBCMD_P3 が同位置に対応する。
;
;   呼び出しはコールドスタートの 1 回だけである。`NEW` / `CLEAR` / `RUN` /
;   画面系の各文はいずれも機能キーの割当てを既定へ戻さない (動作観察)。利用者が
;   `KEY n,"…"` で設定した内容は以後そのまま保たれる。
;
;   直前は iis8_no の RTS なので、この定義へ落ち込む経路は無い。
;   壊す: A,B,X,U (呼出元はコールドスタート本体で退避を要しない)。
;==============================================================================
pf_init_defaults:
                LDU     #pf_def_tbl     ; U = 既定値表 (先頭 = PF10 の長さ)
                LDA     #10             ; A = キー番号 (10 から 1 へ降順)
pfi_key:
                PSHS    A               ; キー番号を退避 ($2A 発行で A が壊れる)
                LDB     #$2A            ; $2A = Define String of PF
                STB     SUBCMD_CMD
                STA     SUBCMD_P1       ; キー番号
                LDB     ,U+             ; 文字数 (1 以上。0 の枠は表に持たない)
                STB     SUBCMD_P2
                LDX     #SUBCMD_P3      ; 文字列の置き場
pfi_copy:
                LDA     ,U+
                STA     ,X+
                DECB
                BNE     pfi_copy
                JSR     impl_sub_send_cmd   ; U は send 側が退避・復元する
                PULS    A
                DECA                    ; PF1 まで数え下げる
                BNE     pfi_key
                RTS

;--- pf_def_tbl — 機能キーの起動時既定値 (1 件 = [文字数][文字列]) -----------
;   並びは PF10 から PF1 へ向かう降順で、展開もその順に回す (キー番号の
;   引き算だけでループの終りが判り、番号との突合せが要らない)。
;   末尾の $0D (復帰) が付くキーは 1 打でそのまま実行まで進み、付かないキーは
;   続けて引数やファイル名を打ち込ませる割当てである。
;   直前は pf_init_defaults の RTS なので、この表へ落ち込む経路は無い。
pf_def_tbl:
                FCB     6
                FCC     "HARDC"
                FCB     $0D                     ; PF10
                FCB     11
                FCC     "SCREEN 7,7"
                FCB     $0D                     ; PF9
                FCB     13
                FCC     "?DATE$,TIME$"
                FCB     $0D                     ; PF8
                FCB     5
                FCC     "SAVE"
                FCB     $22                     ; PF7  (復帰なし: 名前を続ける)
                FCB     5
                FCC     "LOAD"
                FCB     $22                     ; PF6  (復帰なし: 名前を続ける)
                FCB     6
                FCC     "LLIST"
                FCB     $0D                     ; PF5
                FCB     5
                FCC     "CONT"
                FCB     $0D                     ; PF4
                FCB     4
                FCC     "RUN"
                FCB     $0D                     ; PF3
                FCB     5
                FCC     "LIST"
                FCB     $0D                     ; PF2
                FCB     5
                FCC     "AUTO "                 ; PF1  (復帰なし: 行番号指定を続ける)

                include "7t_datestr.inc"        ; DATE$ 代入の本体 (区画の都合でここへ)


; (parse_prog_linenum の置き場所は容量の都合で 7t_float.inc の空きプール)


                include "7t_using_str.inc"      ; PRINT USING 文字列フィールド (プール)
                include "7t_using_nscan2.inc"   ; PRINT USING 書式スキャナ 2/2 (プール)
;------------------------------------------------------------------------------
; impl_files — FILES 命令 ($B0): ドライブ内ディスクのファイル名一覧を表示
;
; 参考書籍の FILES に相当する。指定ドライブの
; ディレクトリを走査し、登録ファイルの一覧を表示する。
;
; ディスクのディレクトリは物理シリンダ 1・サイド 0 のセクタ 4〜15 に置かれ、
; 1 エントリ 32 バイト (先頭 8 バイト = ファイル名, +11 = 種別) で構成される
; (参考書籍の 2D ディスク書式)。FDC (MB8877, I/O = $FD18-$FD1F) を直接駆動して
; 各セクタを読み込み、有効エントリ (先頭バイトが $21〜$FE) の名前と種別を
; 整形出力する。
;
; FDC を本体から直接扱う理由: 起動 ROM の FDC サービスはドライブ選択状態を
; $FFE0-$FFE8 (起動時のみ書込可能な作業域) にキャッシュする前提で、BASIC 実行中
; からの呼出しでは正しいドライブを選べず不作動となるため、ここでは MB8877 の
; 標準手順 (ドライブ選択→RESTORE→SEEK→READ SECTOR) を自前で実行する。
; 自前で動かしたヘッドの位置は dl_track_note で起動 ROM の控え ($FFE1+ドライブ)
; に写し、後から機械語が起動 ROM の DREAD / DWRITE を呼んでも位置決めが合うようにする。
;
; 引数 (FILES ["n:"]) のドライブ指定子を解析し、指定ドライブ (0/1) の一覧を
; 表示する。装置名の指定なし ("FILES") は CAS0: (参考書籍。
; ROM モードの既定装置) で、テープの一覧 (files_cas) になる。
; X = 文の引数位置で入り、復帰時は文末 (':'/$00) を指す。
;------------------------------------------------------------------------------
FDC_STAT        EQU     $FD18           ; 状態(R)/コマンド(W)
FDC_SEC         EQU     $FD1A           ; セクタ レジスタ
FDC_DATA        EQU     $FD1B           ; データ レジスタ
FDC_SIDE        EQU     $FD1C           ; サイド/密度
FDC_DRV         EQU     $FD1D           ; ドライブ選択/モータ (bit7=モータ)
FDC_DRQ         EQU     $FD1F           ; DRQ(bit7)/IRQ 完了(bit6)
BIOS_TRACK      EQU     $FFE1           ; 起動 ROM のディスク BIOS のワーク: +ドライブ番号 = そのドライブの
                                        ;   ヘッドのあるトラックの控え (参考書籍の
                                        ;   ワークエリア $FFE0〜$FFE8)

; FILES ["装置名"]。
impl_files:
                CLR     dl_device       ; 既定 = カセット。参考書籍:
                                        ;   装置名を省略すると ROM モードでは CAS0: が
                                        ;   指定されたものと見なす (LOAD/SAVE の
                                        ;   impl_parse_fname と同じ既定)。"0:"/"1:" の
                                        ;   指定があるときだけディスクへ
                JSR     skip_sp_x
                CMPA    #'"'
                BNE     if_device
                JSR     impl_parse_fname
if_device:
                JSR     stmt_end_check
                TST     dl_device
                LBEQ    files_cas
                ; FAT バッファと演奏キューの同時使用を避ける。
                JSR     play_stop
                PSHS    X               ; 文末位置を退避
                JSR     disk_crlf       ; 一覧の前に 1 行あける
                BSR     disk_fdc_ready  ; ドライブ選択+モータON, サイド0, RESTORE
                JSR     dl_seek_cyl1    ; 物理シリンダ 1 (ディレクトリ) へシーク
                ; --- 割当表 (シリンダ1 セクタ1) を DA_SECBUF へ読む ---
                ;   FILES のサイズ欄 (ディスクはクラスタ数)
                ;   と末尾の空きクラスタ数の表示に使う。
                LDY     #DA_SECBUF
                LDA     #1
                JSR     disk_read_sector
                ; --- ディレクトリ (通し番号 4〜32 = 232 スロット) を読む ---
                LDA     #4
                STA     files_blk       ; セクタ番号 (作業 RAM)
if_sec_lp:
                LDY     #str_work_buf
                LDA     files_blk
                JSR     dl_dirsec       ; 通し番号 → 面 + 物理セクタ
                JSR     disk_read_sector ; セクタ読取 (共用)。A=RNF フラグ (0=成功)
                BNE     if_next_sec     ; 読取失敗 → 走査せずスキップ
                ; --- セクタ内 8 エントリを走査 ---
                LDY     #str_work_buf
                LDB     #8
if_ent_lp:
                LDA     ,Y              ; エントリ先頭バイト
                CMPA    #$FF
                BEQ     if_ent_skip     ; $FF = 未使用/削除 → スキップ
                CMPA    #$21
                BLO     if_ent_skip     ; $00/空白/制御 = 無効 → スキップ
                BSR     if_print_entry  ; 名前 + 種別を出力 (Y/B 保存)
if_ent_skip:
                LEAY    32,Y
                DECB
                BNE     if_ent_lp
if_next_sec:
                INC     files_blk       ; 次セクタ
                LDA     files_blk
                CMPA    #33             ; セクタ 4〜32 (33 で終了)
                BLO     if_sec_lp
                JSR     if_free_out     ; 末尾の `nnn Clusters Free`
                PULS    X,PC ; 文末位置を復帰

; if_fdc_wait — Type I (RESTORE/SEEK) の BUSY 解除を待つ
if_fdc_wait:
                LDA     FDC_STAT
                ANDA    #$01            ; BUSY (bit0)
                BNE     if_fdc_wait
                RTS

; disk_fdc_ready — FDC 準備: 指定ドライブ選択+モータ ON、サイド 0、
;   RESTORE (トラック 0 へ復帰してヘッド位置とトラックレジスタを確定)。
;   ディレクトリ一覧・ディスク LOAD・機械語 LOAD の共通の入口。
;   RESTORE の後は起動 ROM の控え ($FFE1+ドライブ) も 0 にする (dl_track_note)。
;   壊す: A, B, X
disk_fdc_ready:
                LDA     dl_device
                ANDA    #$01            ; ドライブ番号 (0/1)
                ORA     #$80            ; bit7 = モータ ON
                STA     FDC_DRV         ; 指定ドライブ + モータ ON
                CLR     FDC_SIDE        ; サイド 0
                LDA     #$03            ; RESTORE (トラック 0 へ復帰)
                STA     FDC_STAT
                BSR     if_fdc_wait     ; BUSY 解除まで待つ
                ; RESTORE 完了時の FDC ステータス (参考書籍の $FD18)
                ;   の bit7 は NOT READY。ディスクが入っていない (ドライブが
                ;   Ready 状態にない) ときはここで止める。放っておくと後続の
                ;   READ SECTOR が Ready でないまま即終了し、緩衝に残った古い
                ;   中身を一覧やプログラムとして読んでしまう。
                ;   参考書籍: 72 Drive Not Ready
                LDA     FDC_STAT
                BPL     dfr_ok
                CLR     FDC_DRV         ; モータ OFF
                LDB     #72             ; Drive Not Ready
                JMP     impl_error_handler
dfr_ok:
                CLRB                    ; RESTORE 完了 = ヘッドはトラック 0
                BRA     dl_track_note   ; 起動 ROM の控えも 0 にして戻る

;==============================================================================
; disk_clu_seek — クラスタ番号 B の物理位置へ FDC をシークし、そのクラスタの
;   先頭セクタ番号と残セクタ数を設定する。
;   2D 書式 (下の disk_load_body の注記どおり): blk = クラスタ + 8 /
;   cyl = blk >> 2 / side = (blk >> 1) & 1 / 先頭セクタ = (blk & 1) ? 9 : 1 /
;   1 クラスタ = 8 セクタ。
;   入力: B = クラスタ番号 (上限・終端の判定は呼出側で済ませておくこと)
;   出力: dl_blk, FDC のトラック/サイド, dl_secidx, dl_nsec, 起動 ROM の控え
;   壊す: A, B, X
;   ディスク LOAD 本体・逐次読取・機械語読取の共通の入口。
;   並び: 側・先頭セクタ・残数を blk から先に決め (LSRB で落ちるビットを使う)、
;     最後に SEEK を出して控えの更新 (dl_track_note) へ落ちる。SEEK の部分
;     (dcs_seek) はシリンダ 1 への dl_seek_cyl1 (7t_save_list.inc) と共用する。
;==============================================================================
disk_clu_seek:
                ADDB    #8
                STB     dl_blk
                LDA     #8              ; 1 クラスタ = 8 セクタ
                STA     dl_nsec
                LDA     #1              ; 先頭セクタ = (blk & 1) ? 9 : 1
                LSRB                    ; C = blk の bit0、B = blk >> 1
                BCC     dcs_r0_set
                LDA     #9
dcs_r0_set:
                STA     dl_secidx
                CLRA
                LSRB                    ; C = blk の bit1 (= side)、B = blk >> 2 (= cyl)
                ROLA                    ; A = side
                STA     FDC_SIDE
dcs_seek:                               ; B = 目標トラック (側は設定済)
                STB     FDC_DATA        ; SEEK 目標トラック
                LDA     #$13
                STA     FDC_STAT
                BSR     if_fdc_wait
                ; 続けて dl_track_note へ落ちる (B = ヘッドのトラック)

; dl_track_note — 起動 ROM のディスク BIOS が持つ「ドライブごとのヘッドの
;   トラックの控え」($FFE1+ドライブ。参考書籍のワーク
;   エリア $FFE0〜$FFE8) を、本 ROM が自前で動かしたヘッドの位置に合わせる。
;   起動 ROM の DREAD / DWRITE は位置決めの前にこの控えをトラックレジスタへ
;   書き戻すので、BASIC のディスク処理の後に機械語が起動 ROM を呼んでも位置決めが
;   狂わない。
;   入力: B = ヘッドのトラック (RESTORE の後は 0、SEEK の後は目標トラック)、
;         dl_device bit0 = ドライブ番号。壊す: A, X
dl_track_note:
                LDA     dl_device
                ANDA    #$01            ; ドライブ番号 (0/1)
                LDX     #BIOS_TRACK
                STB     A,X             ; $FFE1+ドライブ = トラック
                RTS

; if_print_entry — 1 エントリを整形出力 (入力 Y=エントリ先頭)。Y,B を保存。
;   体裁: <名前 8 桁> "  " <ファイル形式 0-2> " " <属性 A/B> " " <編成 S/R> " " <サイズ>
;   CR LF
if_print_entry:
                PSHS    B,Y             ; エントリ走査状態を退避
                BSR     if_pe_body      ; 名前 + ファイル形式 + 属性
                BSR     if_pe_tail      ; 編成 + サイズ + 改行
                PULS    B,Y,PC

; if_pe_body — 一覧の 1 行のうち、`ファイル名 ファイル形式 属性` の部分を出す
;   (入力 Y = 名前 8B + ファイル形式 (+11) + 属性 (+12) の並び)。テープの一覧
;   (files_cas) とも共用する。破壊: A, B, X, Y
if_pe_body:
                LDB     #8              ; 名前 8 バイト
if_pe_name:
                LDA     ,Y+
                JSR     impl_putchar
                DECB
                BNE     if_pe_name
                JSR     print_inline
                FCC     "  "            ; 区切りスペース
                FCB     0
                LDA     3,Y             ; +11 ファイルタイプ (Y=エントリ+8)
                ADDA    #'0'            ; ファイル形式 0〜2 を 1 桁の数字で (参考書籍 FILES)
                JSR     impl_putchar
                JSR     put_space               ; 空白 1 個を出す
                LDA     #'A'            ; 形式: アスキーフラグ (+12) $FF なら A
                LDB     4,Y
                INCB                    ; $FF → 0
                BEQ     ipe_fmt
                LDA     #'B'            ; それ以外 (バイナリ) は B
ipe_fmt:
                JMP     impl_putchar    ; 形式 (末尾呼び)

;------------------------------------------------------------------------------
; if_pe_tail — FILES の 1 行の後半 `編成 サイズ` と改行を出す。
;   参考書籍 FILES: 編成は S (シーケンシャル) / R (ランダム)、
;   サイズはフロッピィディスクではクラスタ数。ディレクトリスロットの +13 が
;   ランダムアクセスフラグ、+14 が先頭クラスタ番号 (参考書籍)。
;   クラスタ数は割当表の鎖を辿って数える (参考書籍の FAT)。
;   入力: Y = ディレクトリスロット + 8。破壊: A, B, X
;------------------------------------------------------------------------------
if_pe_tail:
                JSR     put_space
                LDA     #'S'            ; 編成 S = シーケンシャル
                TST     5,Y             ; +13 ランダムアクセスフラグ
                BEQ     ipt_org
                LDA     #'R'            ; 編成 R = ランダム
ipt_org:
                JSR     impl_putchar
                JSR     put_space
                LDA     6,Y             ; +14 先頭クラスタ番号
                CLRB
ipt_lp:
                INCB                    ; 今のクラスタを 1 個ぶん数える
                PSHS    B
                LDX     #DA_SECBUF+5    ; 割当表の先頭 5 バイトはヘッダ
                TFR     A,B
                ABX
                LDA     ,X              ; 鎖の次のクラスタ番号
                PULS    B
                CMPA    #152            ; クラスタは 0〜151 (参考書籍)
                BLO     ipt_lp          ; 152 以上 = 最終クラスタの印 → 終り
                CLRA
                JSR     print_uint16    ; サイズ (クラスタ数)
                JMP     disk_crlf

; if_msg_free — 末尾の空きクラスタ数の行に添える文言 (本体 if_free_out は別区画)
if_msg_free:
                FCC     " Clusters Free"
                FCB     $0D,$0A,0


;==============================================================================
; ファイル名解析 impl_parse_fname
;   書式: ["装置:"]"filename" [,A]。装置プレフィクス判定を含む。
;   出力: cmt_filename にファイル名 / dl_device に装置+ドライブ。
;   既定装置: カセット (ROM モードの既定)。プレフィクスがあれば上書き。
;     "CAS:"/"CAS0:" → カセット / "0:"/"1:" → ディスク ドライブ0/1
;==============================================================================
impl_parse_fname:
                CLR     dl_device               ; 既定 = カセット (参考書籍:
                                                ;   ROM モードの既定デバイスは CAS0)
                LDU     #cmt_filename           ; 名前の指定が無ければ空 (= 任意のファイル)
                                                ;   にする。ここで U を確定しておかないと、
                                                ;   引用符なしの経路 (ipf_done) が前の呼出の
                                                ;   U を書き潰し、前に指定した名前が残って
                                                ;   しまう (LOAD / SKIPF が別のファイルを
                                                ;   探し続ける)
                LDA     ,X
                CMPA    #'"'
                BNE     ipf_done
                LEAX    1,X
                ; 閉じ '"' より前に ':' があれば、その ':' 前を装置プレフィクスと
                ; みなし判定して読み飛ばす。ファイル名自体に ':' は現れない。
                PSHS    X                       ; 開始位置を退避
ipf_scan:
                LDA     ,X+
                BEQ     ipf_noscolon            ; $00 終端 → ':' なし
                CMPA    #'"'
                BEQ     ipf_noscolon            ; 閉じ '"' → ':' なし
                CMPA    #':'
                BNE     ipf_scan
                ; ':' を発見 → X は ':' の次。プレフィクス先頭文字で装置分類。
                LDU     ,S                      ; U = プレフィクス先頭 (退避した X)
                LDA     ,U
                CMPA    #'C'                    ; "CAS"/"CAS0" → カセット
                BEQ     ipf_pfx_cas
                CMPA    #'0'                    ; "0:" → ディスク ドライブ0
                BEQ     ipf_pfx_d0
                CMPA    #'1'                    ; "1:" → ディスク ドライブ1
                BEQ     ipf_pfx_d1
                BRA     ipf_pfx_done            ; 不明プレフィクス → 既定装置のまま
ipf_pfx_cas:
                CLR     dl_device               ; カセット
                BRA     ipf_pfx_done
ipf_pfx_d0:
                LDA     #DL_DISK0
                STA     dl_device
                BRA     ipf_pfx_done
ipf_pfx_d1:
                LDA     #DL_DISK1
                STA     dl_device
ipf_pfx_done:
                LEAS    2,S                     ; 退避した X を捨てる (X は ':' の次)
                BRA     ipf_copy_start
ipf_noscolon:
                PULS    X                       ; ':' 無し → 元の位置へ戻す
ipf_copy_start:
                LDU     #cmt_filename
                LDB     #16
ipf_copy:
                LDA     ,X+
                BEQ     ipf_eol
                CMPA    #'"'
                BEQ     ipf_done
                STA     ,U+
                DECB
                BNE     ipf_copy
ipf_eol:
ipf_done:
                CLR     ,U
                ; ファイル名長 / ファイル名 / 物理機番を参考書籍の
                ;   位置 ($02DD-$02E6) へ写す (X は保存される)
                JMP     fileinfo_set

;------------------------------------------------------------------------------
; disk_open_name — FDC 準備 (ドライブ選択+モータ ON, サイド 0, RESTORE) と
;   ディレクトリ検索をまとめた共通の入口。見つからなければモータを止めて
;   File Not Found (63) で中止する。ディスク LOAD と機械語 LOAD が共用する。
;------------------------------------------------------------------------------
disk_open_name:
                JSR     disk_fdc_ready
                BSR     disk_dir_find
                BCS     don_notfound
                RTS
don_notfound:
                CLR     FDC_DRV         ; モータ OFF
                JMP     err_c63         ; File Not Found

;==============================================================================
; disk_read_sector — MB8877 で 1 セクタ (256B) を読み取る共用ヘルパ
;   入力: A = セクタ番号, Y = 転送先バッファ (256B)
;         (FDC_DRV モータ ON / トラック / FDC_SIDE は呼出側で設定済のこと)
;   出力: A = 完了状態の NOT READY / RNF ビット ($80 / $10 = 失敗, 0 = 成功)
;         Y は 256 バイト前進する。B/X/U は保存。
;   FILES (impl_files) と disk_load_body から共用する。
;==============================================================================
disk_read_sector:
                STA     FDC_SEC         ; セクタ レジスタ
                ORCC    #$50            ; 割込み禁止 (データ転送の取りこぼし防止)
                LDA     #$80
                STA     FDC_STAT        ; READ SECTOR
drs_poll:
                LDA     FDC_DRQ
                BMI     drs_drq         ; bit7=DRQ → データ有り
                ASLA
                BMI     drs_done        ; bit6=IRQ → コマンド完了
                BRA     drs_poll
drs_drq:
                LDA     FDC_DATA        ; データ 1 バイト
                STA     ,Y+
                BRA     drs_poll
drs_done:
                ANDCC   #$AF            ; 割込み再許可
                LDA     FDC_STAT        ; 完了状態 (IRQ クリア)
                ANDA    #$90            ; NOT READY (bit7) / RNF (bit4 該当セクタ無し)
                RTS

;==============================================================================
; disk_dir_find — ディレクトリを走査し cmt_filename に一致するエントリを探す
;   前提: FDC モータ ON / RESTORE 済 (トラック0)。
;   入力: cmt_filename (8桁まで, 空白パディング相当)
;   出力: 一致時 C=0, dl_start_clu=開始クラスタ, dl_ftype=種別(+11),
;         dl_ascii=ASCIIフラグ(+12)。不一致時 C=1。
;   ディレクトリは物理シリンダ1 のセクタ 4〜32 (通し番号。面 0 のセクタ 4〜16 と
;   面 1 のセクタ 1〜16)、1 スロット 32B の 232 スロット。
;
;   スロット記録 (ディスク SAVE 用の拡張):
;     一致時はそのスロットの位置 (セクタ番号 → dl_clu_adv / セクタ内オフセット
;     → dl_clu_cnt) を記録する。走査中に見つけた最初の空きスロット (先頭バイト
;     $FF=未使用 または <$21=削除/無効) も、dl_clu_adv が $FF (未記録) の間だけ
;     同じ組へ記録する (一致の記録が常に優先 = 同名は上書き保存)。
;     SAVE 側は呼出前に dl_clu_adv へ $FF を入れて「未記録」を示す。LOAD 側は
;     初期化せずに呼ぶが、記録先はどちらも直後の dl_seq_init が上書きする
;     作業変数なので LOAD の動作に影響しない。
; (dl_seek_cyl1 の置き場所は容量の都合で 7t_save_list.inc の空きプール)
; (dl_seq_init の置き場所は容量の都合で 7t_cmt_io.inc の空きプール)

;==============================================================================
disk_dir_find:
                JSR     dl_seek_cyl1    ; サイド0・シリンダ1 へシーク (共通化)
                LDA     #4              ; ディレクトリの通し番号 4..32 (232 スロット)
                STA     dl_secidx
ddf_sec_lp:
                LDY     #str_work_buf
                LDA     dl_secidx
                JSR     dl_dirsec       ; 通し番号 → 面 + 物理セクタ
                BSR     disk_read_sector
                BNE     ddf_next_sec    ; RNF → このセクタは飛ばす
                LDY     #str_work_buf
                LDB     #8              ; 1 セクタ 8 エントリ
ddf_ent_lp:
                LDA     ,Y              ; エントリ先頭バイト
                CMPA    #$21
                BLO     ddf_free        ; $00/空白/制御 = 空き/無効 → 空き記録へ
                CMPA    #$FF
                BEQ     ddf_free        ; $FF = 削除/未使用 → 空き記録へ
                ; 名前照合: cmt_filename (8桁, $00 到達後は空白扱い) vs エントリ +0..7
                PSHS    B,Y             ; 走査状態退避 (Y=エントリ先頭)
                LDX     #cmt_filename
                LDB     #8
ddf_cmp:
                LDA     ,X+             ; 利用者名 1 文字 (前進)
                BNE     ddf_cmp_have
                LEAX    -1,X            ; $00 到達 → 前進を戻し空白でパディング
                LDA     #' '
ddf_cmp_have:
                CMPA    ,Y+
                BNE     ddf_cmp_no
                DECB
                BNE     ddf_cmp
                ; 8 文字一致 → Y = エントリ+8。属性を控える
                LDA     3,Y             ; +11 種別
                STA     dl_ftype
                LDA     4,Y             ; +12 ASCII フラグ
                STA     dl_ascii
                LDA     6,Y             ; +14 開始クラスタ
                STA     dl_start_clu
                ; スロット位置の記録 (SAVE 用の拡張。頭書き参照)
                LDA     dl_secidx
                STA     dl_clu_adv      ; スロットのセクタ番号
                TFR     Y,D
                SUBB    #(str_work_buf+8)&$FF ; Y = エントリ+8 → セクタ内オフセット
                                ;   (実オフセット < 256 ゆえ下位 8bit の差で足りる)
                STB     dl_clu_cnt
                PULS    B,Y             ; スタック整合
                ANDCC   #$FE            ; C=0 = 見つかった
                RTS
ddf_free:
                ; 空きスロット: 未記録 (dl_clu_adv=$FF) の間だけ最初の 1 個を記録
                LDA     dl_clu_adv
                INCA
                BNE     ddf_ent_skip    ; 記録済み (または LOAD 経路の不定値)
                LDA     dl_secidx
                STA     dl_clu_adv      ; スロットのセクタ番号
                TFR     Y,D
                SUBB    #str_work_buf&$FF ; セクタ内オフセット (下位 8bit の差で足りる)
                STB     dl_clu_cnt
                BRA     ddf_ent_skip
ddf_cmp_no:
                PULS    B,Y             ; 走査状態復元 (Y=エントリ先頭)
ddf_ent_skip:
                LEAY    32,Y            ; 次エントリ (32B)
                DECB
                BNE     ddf_ent_lp
ddf_next_sec:
                INC     dl_secidx
                LDA     dl_secidx
                CMPA    #33             ; セクタ 4〜32 (33 で終了)
                BLO     ddf_sec_lp
                ORCC    #$01            ; C=1 = 見つからなかった
                RTS

; (dl_fat_next の置き場所は容量の都合で 7t_runtime.inc の空きプール)


; (disk_clu_seek は disk_fdc_ready の直後 (dl_track_note の手前) に置く)

;==============================================================================
; disk_load_body — ディスク LOAD 本体 (impl_load の装置分岐から LBMI で入る)
;   前提: cmt_filename に対象名, dl_device bit0=ドライブ番号。
;   処理: ディレクトリ検索で開始クラスタ取得 → ASCII 保存形式のみ受理し、
;         1 行ずつ原文のまま挿入する (dl_ascii_load)。バイナリ形式は
;         Bad Data In File。RUN" 経由でも同一。
;
;   2D ディスク書式:
;     クラスタ = 8 セクタ。物理位置は システム領域 (IPL/FAT/ディレクトリ =
;     物理トラック0〜3) の分を差し引く:  blk = クラスタ + 8
;       cyl        = blk >> 2
;       side       = (blk >> 1) & 1
;       先頭セクタ = (blk & 1) ? 9 : 1   (blk 偶=セクタ1-8 / 奇=セクタ9-16)
;     LOAD は開始クラスタから割当表 (FAT) の鎖を辿り、プログラム終端で止める。
;     連番 (クラスタ, +1, +2, …) の逐次読取では、断片化ファイル (鎖が飛ぶ
;       配置) で隣接する別ファイルの内容を取り込み本文を壊す。
;==============================================================================
disk_load_body:
                PSHS    X               ; ディスパッチャ復帰用 X (LOAD 完了後に
                                        ;   直接モードの行を正しく続行する)
                ; --- Phase0: FDC 準備 (ドライブ選択+モータON, サイド0, RESTORE) ---
                ; --- Phase1: ディレクトリ検索 → 開始クラスタ ---
                JSR     disk_open_name  ; 準備 + 検索 (不在なら File Not Found)
dlb_found:
                ; ディレクトリスロットのファイルタイプ (+11) とアスキーフラグ (+12)
                ; (参考書籍):
                ;   $00 $FF = BASIC ソースのアスキー形式 → 行取込へ
                ;   それ以外 (バイナリ形式 $00 $00、機械語 $02 ほか) = Bad Data In File
                LDD     dl_ftype        ; D = 種別(+11):形式(+12)
                COMB                    ; 形式 $FF→$00 / $FE→$01 (保護)
                CMPD    #$0001          ; 種別 $00 かつ形式 $FF/$FE だけ受理
                LBHI    bdf_err         ; モータ停止 + Bad Data In File
                STB     prot_flag       ; 保護状態を本文へ引き継ぐ

;==============================================================================
; dl_ascii_load / dal_new — ディスクの BASIC プログラム LOAD 本体
;   前提: disk_load_body の Phase0/Phase1 完了済 (FDC ドライブ選択+モータON,
;         RESTORE 済, dl_start_clu=開始クラスタ, 復帰用 X はスタック上)。
;   処理: 開始クラスタから割当鎖の順にセクタを 1 つずつ読み、バイトストリーム
;         (disk_get_byte) として供給する。行の組み立て・挿入は共通の行取込
;         (stream_lines) が行う。ファイル終端は終端記号 $1A か媒体の尽きで確定。
;
;   バッファ分離: 元データ = DA_SECBUF (256B セクタバッファ) / 行組立 =
;         str_work_buf / 格納結果 = TXTTAB。3 者が重複しないので元 ASCII を
;         TXTTAB に置く自己上書きを避けられる (ストリーム処理)。
;
;   RUN" からの連鎖: impl_run が本経路で TXTTAB を構築後に impl_exec_prog を
;         呼ぶため、プログラム本文の RUN"別ファイル" 連鎖も同一経路で成立する。
;==============================================================================
dl_ascii_load:
                ; 逐次読取の初期化 + 供給 = ディスク (ここへ展開)
dal_prep:
                ; 逐次読取の初期化 + 供給ルーチンの設定 (dlb_own / dl_ascii_load 共用)。
                ; 本文供給は disk_get_byte (機械語ロードと共通のセクタストリーム。
                ; $1A の判定は stream_lines 側が行う)
                JSR     dl_seq_init
                LDU     #disk_get_byte
dal_new:
                JSR     prog_clear_new          ; プログラムを空に (NEW 相当)
                JSR     stream_lines            ; CR 区切りで各行を原文のまま挿入
                CLR     FDC_DRV                 ; モータ OFF
                JSR     il_ret_run              ; ,R 指定なら自動 RUN (戻らない)
                PULS    X,PC ; ディスパッチャ復帰用 X

;==============================================================================
; disk_write_sector — MB8877 で 1 セクタ (256B) を書き込む共用ヘルパ
;   入力: A = セクタ番号, Y = 転送元バッファ (256B)
;         (FDC_DRV モータ ON / トラック / FDC_SIDE は呼出側で設定済のこと)
;   出力: A = 完了状態のエラービット (0 = 成功。書込禁止/書込失陥/RNF/CRC/
;         転送落ちを含む)。Z フラグは A に一致。Y は 256 バイト前進する。
;         B/X/U は保存。
;   割込みは転送中のみ禁止 (取りこぼし防止) し、完了時に元へ戻す
;   (disk_read_sector と同じ流儀)。
;==============================================================================
disk_write_sector:
                STA     FDC_SEC         ; セクタ レジスタ
                ORCC    #$50            ; 割込み禁止 (データ転送の取りこぼし防止)
                LDA     #$A0
                STA     FDC_STAT        ; WRITE SECTOR
dws_poll:
                LDA     FDC_DRQ
                BMI     dws_drq         ; bit7=DRQ → データ要求
                ASLA
                BMI     dws_done        ; bit6=IRQ → コマンド完了
                BRA     dws_poll
dws_drq:
                LDA     ,Y+
                STA     FDC_DATA        ; データ 1 バイト
                BRA     dws_poll
dws_done:
                ANDCC   #$AF            ; 割込み再許可
                LDA     FDC_STAT        ; 完了状態 (IRQ クリア)
                ANDA    #$7C            ; 書込禁止/失陥/RNF/CRC/転送落ち
                RTS

; (dsv_wsec / dsv_ioerr / dsv_errb は容量の都合で 7t_startup.inc の空きプールへ)

;==============================================================================
; dsv_body — ディスク SAVE 本体 (impl_save の装置分岐から LBMI で入る。
;   参考書籍の SAVE とディスク書式による)。
;   前提: cmt_filename = 対象名, dl_device bit0 = ドライブ番号。ディスパッチャ
;         復帰用 X はスタック上 (末尾の PULS X で戻す)。
;   処理: (1) ディレクトリ走査で書込先スロットを決める (同名があればそのスロット
;         を再利用 = 上書き。無ければ最初の空きスロット。どちらも無ければ
;         Directory Full)。(2) 割当表 (FAT) を読み、同名の旧割当鎖を開放。
;         (3) クラスタを割当てながら本文 (全行 + 終端 $1A) をセクタ
;         単位で書き込む。(4) 終端クラスタの FAT 値 = $BF + 使用セクタ数
;         (参考書籍)。(5) FAT とディレクトリ項目を書き戻す。
;   本文の書込みが終わるまで FAT/ディレクトリへ書かない順序なので、途中で
;   媒体エラーになっても既存ファイルの管理情報は壊れない。
;   作業 RAM は LOAD の作業変数 (dl_*/da_bptr/str_work_buf/DA_SECBUF) を転用
;   する (SAVE と LOAD は同時に走らないため時間排他)。
;==============================================================================
dsv_body:
                JSR     disk_fdc_ready  ; ドライブ選択 + モータ ON + RESTORE
                ; --- (1) スロット決定 (disk_dir_find のスロット記録拡張を利用) ---
                LDA     #$FF
                STA     dl_clu_adv      ; スロット記録 = 未記録
                STA     dl_cluster      ; 開放すべき旧鎖 = 無し ($FF は終端値)
                JSR     disk_dir_find
                BCS     dsv_slot        ; 同名なし → 空きスロット記録のまま
                JSR     dsv_ask         ; 同名あり → 参考書籍 SAVE の問合せ (プール)
                LBCS    dsv_fin         ; N → セーブしない (モータを止めて戻る)
                LDA     dl_start_clu    ; Y → 旧鎖の開放を予約 (上書き保存)
                STA     dl_cluster
dsv_slot:
                LDA     dl_clu_adv
                INCA
                LBEQ    dsv_dirfull     ; スロット無し ($FF のまま) → Directory Full
                ; --- (2) FAT (シリンダ1 セクタ1) → str_work_buf、旧鎖の開放 ---
                JSR     dl_seek_cyl1
                LDY     #str_work_buf
                LDA     #1
                JSR     disk_read_sector
                LBNE    dsv_ioerr       ; 読めない → Device I/O Error
                LDA     dl_cluster
                JSR     dsv_free_chain  ; 旧鎖を $FF へ ($FF 開始なら何もしない)
                ; --- (3) 本文の書込み ---
                JSR     dsv_alloc       ; B = 先頭クラスタ
                STB     dl_start_clu
                STB     dl_cluster
                JSR     disk_clu_seek   ; シーク + dl_secidx / dl_nsec=8
                LDX     #DA_SECBUF
                STX     da_bptr
                LDD     #dsv_put
                STD     out_vec         ; 出力先 = セクタ緩衝
                JSR     sv_emit_lines   ; 全行 (行番号 + 本文原文 + CR)
                LDA     #$1A            ; 終端記号
                JSR     out_char
                ; 端数セクタ: 緩衝に残りがあれば $00 で満たして書き出す
dsv_pad:
                LDX     da_bptr
                CMPX    #DA_SECBUF
                BEQ     dsv_wfat        ; 緩衝先頭 = 端数なし (書出し済)
                CLRA
                JSR     dsv_put         ; 満杯になった時点で自動書出し → 先頭へ戻る
                BRA     dsv_pad
dsv_wfat:
                ; --- (4) 終端クラスタの FAT 値 = $BF + 使用セクタ数 ---
                LDA     dl_nsec
                NEGA
                ADDA    #$C7            ; $C7 - 残り = $BF + (8 - 残り)
                LDX     #str_work_buf+5
                LDB     dl_cluster
                ABX
                STA     ,X
                ; --- (5) FAT とディレクトリ項目の書き戻し ---
                JSR     dl_seek_cyl1
                LDY     #str_work_buf
                LDA     #1
                JSR     dsv_wsec        ; FAT 書き戻し (失敗は 53 で中止)
                ; ディレクトリ: スロットのセクタを読み直して項目を組み立てる
                LDY     #DA_SECBUF
                LDA     dl_clu_adv      ; スロットのセクタ番号 (通し番号)
                JSR     dl_dirsec       ; 通し番号 → 面 + 物理セクタ
                JSR     disk_read_sector
                LBNE    dsv_ioerr
                LDU     #DA_SECBUF
                CLRA
                LDB     dl_clu_cnt      ; スロットのセクタ内オフセット (0..224)
                LEAU    D,U             ; U = 項目先頭 (D は 16bit 符号無し)
                ; 名前 8B (空白詰め = ディレクトリの照合規約)
                LDX     #cmt_filename
                LDB     #8
dsv_name:
                LDA     ,X
                BEQ     dsv_name_sp     ; 終端 $00 以降は空白詰め
                LEAX    1,X
                BRA     dsv_name_st
dsv_name_sp:
                LDA     #' '
dsv_name_st:
                STA     ,U+
                DECB
                BNE     dsv_name
                ; +8..+31 を一旦 $00 で埋め、種別/形式/開始クラスタを置く
                LDB     #24
dsv_zfill:
                CLR     ,U+
                DECB
                BNE     dsv_zfill
                LEAU    -24,U           ; U = 項目+8 へ戻す
                CLRA                    ; +11 ファイルタイプ $00 (BASIC ソース)
                LDB     #$FF            ; +12 アスキーフラグ $FF (参考書籍)
                SUBB    prot_flag       ;   保護保存は $FE
                STD     3,U
                LDA     dl_start_clu    ; +14 開始クラスタ
                STA     6,U
                LDY     #DA_SECBUF
                LDA     dl_clu_adv
                JSR     dl_dirsec       ; 通し番号 → 面 + 物理セクタ
                JSR     dsv_wsec        ; ディレクトリ書き戻し (失敗は 53 で中止)
dsv_fin:
                CLR     FDC_DRV         ; モータ OFF
                PULS    X,PC ; ディスパッチャ復帰用 X (impl_save が積んだ)
;------------------------------------------------------------------------------
; dsv_ask — ディスクへの SAVE で同名のファイルが既にあるときの処理。
;   参考書籍 SAVE: 直接モードで実行しているときは
;   `Are you sure (Y or N)` を出し、Y なら既にあるファイルを消して新しい
;   ファイルとして登録し、N ならセーブしない。間接モードで実行しているときは
;   `File Already Exists In 行番号` (エラーコード 64) を出してコマンドレベルへ戻る。
;   出力: C=0 = 続ける / C=1 = セーブしない。破壊: A, B, X
;------------------------------------------------------------------------------
dsv_ask:
                LDX     CURLIN
                LEAX    1,X             ; $FFFF = 直接モード
                BNE     dsa_ind
                JSR     print_inline
                FCC     "Are you sure (Y or N)"
                FCB     0
                JSR     impl_input_line ; 応答を受ける (エコーはサブシステム)
                LDA     INPBUF
                ANDA    #$DF            ; 小文字も受ける
                CMPA    #'Y'
                BEQ     dsa_ret         ; Y → C=0 のまま続ける
                ORCC    #$01            ; それ以外 → C=1 (セーブしない)
dsa_ret:
                RTS
dsa_ind:
                LDB     #64             ; 64 File Already Exists (参考書籍)
                JMP     dsv_errb        ; モータを止めてから誤りを出す

; (dsv_dirfull の置き場所は容量の都合で 7t_float.inc の空きプール)



;==============================================================================
; disk_loadm_body — ディスクの LOADM 本体 (impl_loadm の装置分岐から LBMI で入る)
;   前提: cmt_filename = 対象名, dl_device bit0 = ドライブ番号。オプション
;         (,アドレス / ,,R) は impl_loadm 側で解析済 (ilm_have_addr/mlm_user_addr/
;         mlm_run/mlm_offset)。ディスパッチャ復帰用 X はスタックに退避済 (共通尾部
;         ilm_done が PULS X で戻す)。
;   処理: ディレクトリ検索で開始クラスタを得て、開始クラスタから連番クラスタを
;         バイトストリーム (disk_get_byte) として供給しつつ、機械語セグメント列
;         を解析して各セグメントを埋め込みアドレスへ展開する。展開後は impl_loadm の
;         共通尾部 (ilm_done: オフセット反映/実行アドレス確定/,R 実行/実行アドレスワーク複写/
;         FDC・CMT モータ停止) へ合流する。
;
;   機械語ファイルのディスク上の書式は、次のセグメント列そのものである:
;     [$00][長さHi][長さLo][アドレスHi][アドレスLo][データ×長さ] …繰り返し
;     [$FF][長さ2B(=0)][実行アドレスHi][実行アドレスLo]              (終端)
;   セグメント解析器はテープ経路 (ilm_seg_lp) と共用で、供給元の切替は
;   mlm_get_one が mlm_dev で行う。
;==============================================================================
disk_loadm_body:
                ; --- FDC 準備 (ドライブ選択+モータON, サイド0, RESTORE) ---
                ; --- ディレクトリ検索 → 開始クラスタ ---
                JSR     disk_open_name  ; 準備 + 検索 (不在なら File Not Found)
dlm_found:
                ; --- ディスクバイトストリーム状態を初期化 ---
                JSR     dl_seq_init     ; 逐次読取の初期化 (共通化)
                ; --- 機械語セグメント列の解析 ---
                ; 解析器はテープ経路と共用 (ilm_seg_lp, 7t_cmt_io.inc)。
                ; 供給元の切替は mlm_get_one が mlm_dev で行い、共通尾部 ilm_done も
                ; 同じ印で FDC モータ停止の要否を決める。
                JMP     ilm_seg_lp

;------------------------------------------------------------------------------
; mlm_get_one — LOADM セグメント解析器の論理バイト供給口 (装置で振り分ける)
;   mlm_dev bit7=1 (ディスク) なら disk_get_byte、$00 (カセット) なら
;   mlm_get_byte へ末尾分岐する。どちらも A=バイト / C=1 で終端の同じ規約。
;   テープ経路とディスク経路でセグメント解析器を共用するための振り分け。
;   破壊は各供給元と同じ (A,B,X,Y)。
;------------------------------------------------------------------------------
mlm_get_one:
                LDA     mlm_dev
                BMI     disk_get_byte           ; ディスク (末尾分岐)
                JMP     mlm_get_byte            ; カセット (末尾呼出)


;------------------------------------------------------------------------------
; disk_get_byte — ディスクセクタストリームから論理 1 バイトを供給する
;   disk_loadm_body のセグメント解析器が呼ぶ。クラスタ (blk=クラスタ+8) を
;   セクタ単位で DA_SECBUF に読み込み、1 バイトずつ返す。1 クラスタ (8 セクタ) を
;   読み終えたら割当表 (FAT) の鎖を辿って次クラスタへ進む。RNF (ディスク端/
;   セクタ欠落) またはクラスタ上限で C=1。BASIC プログラムのディスク LOAD も
;   本ルーチンを供給に使う ($1A 終端の判定は行取込 stream_lines 側が行う)。
;   出力: A = バイト, CC: C=0 成功 / C=1 終端。破壊: A,B,X,Y。
;   状態変数: dl_cluster/dl_nsec/dl_secidx/dl_blk/da_bptr (ディスク LOAD と共用)。
;------------------------------------------------------------------------------
disk_get_byte:
                LDX     da_bptr
                CMPX    #DA_SECBUF_END
                BLO     dgm_serve               ; バッファに残りあり
                ; --- バッファ枯渇 → 次セクタを読む ---
                LDA     dl_nsec
                BNE     dgm_read_sec            ; クラスタ内にまだセクタ残
                ; 直前クラスタ読了なら割当鎖 (FAT) を辿って前進 (断片化ファイル対応)
                LDA     dl_clu_adv
                BEQ     dgm_have_clu            ; 初回はディレクトリの開始クラスタ
                CLR     dl_clu_adv
                JSR     dl_fat_next             ; A = 次クラスタ ($98 以上は終端/空き)
                STA     dl_cluster
dgm_have_clu:
                INC     dl_clu_cnt              ; 鎖周回ガード (全クラスタ数超 = 異常)
                LDA     dl_clu_cnt
                CMPA    #152
                BHI     dgm_eof
                ; 新クラスタの物理位置を算出 (blk = クラスタ + 8)
                LDB     dl_cluster
                CMPB    #152                    ; データクラスタ上限/終端ガード
                BHS     dgm_eof
                JSR     disk_clu_seek           ; blk/cyl/side/先頭セクタ/セクタ数
dgm_read_sec:
                LDY     #DA_SECBUF
                LDA     dl_secidx
                JSR     disk_read_sector        ; 256B → DA_SECBUF, A=RNF
                TSTA
                BNE     dgm_eof                 ; RNF → 終端扱い
                INC     dl_secidx               ; セクタ内カーソルをクラスタ内で前進
                DEC     dl_nsec
                BNE     dgm_buf_ready
                INC     dl_clu_adv              ; クラスタ読了 → 次要求時に割当鎖を辿る
dgm_buf_ready:
                LDX     #DA_SECBUF
dgm_serve:
                LDA     ,X+
                STX     da_bptr
                ANDCC   #$FE                    ; C=0 成功
                RTS
dgm_eof:
                ORCC    #$01                    ; C=1 終端
                RTS
