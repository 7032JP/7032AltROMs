; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;--- $8000: 初期ジャンプ ---
rom_start:
                JMP     cold_start_main ; 3 バイト ($8000)

                include "7t_using_capt.inc"     ; PRINT USING 数値捕捉 (プール)
;==============================================================================
; pool_gr_opts — 図形文の任意引数 "[, [パレットコード] [, 機能]]" を解析する
;   (図形文に共通の書式。PSET / PRESET から使う)
;
;   入力: A = 既定パレットコード, X = 走査位置
;   出力: A = パレットコード, B = 機能コード (FUNC_PSET 等), X = 引数列の次
;   破壊: A, B, CC (X は前進)
;
;   skip_sp_x は A に現在文字を返す。既定値を A へ入れたまま呼ぶと壊れるので、
;     既定パレットコードはハードウェアスタックへ退避して保持する。
;   区間ごとの容量の予算上、本ルーチンは 4 つの空きプールへ分割配置する
;     (pool_gr_opts/pgo_pal は本ファイル、pgo_c2 も本ファイル、pgo_nofn は
;      7t_runtime.inc)。参照はラベル経由のみで位置依存は無い。
;==============================================================================
pool_gr_opts:
                PSHS    A                   ; 0,S = パレットコード (既定値)
                JSR     skip_comma
                LBCC    pgo_nofn
                JSR     skip_sp_comma
                LBEQ    pgo_fn2             ; ",," = パレットコード省略 → 機能のみ
                BRA     pgo_pal

;==============================================================================
; 二次コマンドのハンドラ表 (参照はラベル経由のみ。アドレスは固定しない)
;==============================================================================
cmd2_hdl_tbl:
                FDB     impl_chain      ; $EE CHAIN
                FDB     impl_erase      ; $EF ERASE
                FDB     impl_lprint     ; $F0 LPRINT
                FDB     impl_llist      ; $F1 LLIST
                FDB     impl_play       ; $F2 PLAY
                FDB     impl_sound      ; $F3 SOUND

; pool_gr_opts (続き 2/4): パレットコードの式を評価して確定する
pgo_pal:
                ; パレットコードは最近接丸め (動作観察)。
                ; 丸めた値が 0..15 の外なら Illegal Function Call (動作観察)。
                JSR     eval_to_int_15      ; 丸め + 値域検査 (0..15)
                STB     ,S                  ; 退避値を上書き
                JSR     skip_sp_x           ; 機能の前の ',' を探す (A = 現在文字)
                LBRA    pgo_c2


;==============================================================================
; 倍精度浮動小数点コア: 空きプールへ配置 (ラベル参照のみ, 位置依存なし)。
;==============================================================================
                include "7t_double.inc"

;==============================================================================
; ウォームスタート
;==============================================================================

                include "7t_strgc2.inc"         ; 詰め直しの候補選び (区画の都合でここへ)
                include "7t_strtmp.inc"         ; 一時領域の確保

;------------------------------------------------------------------------------
; brk_active — 「いま中断操作を受け付けるべきか」の共通判定。
;   中断キーの押下は $FD04 (PORT_FIRQ) の bit1 に現れる (0=押下, 1=非押下)。
;   受け付けるのはプログラムが走行中 (CURLIN != $FFFF) の間だけで、直接モードの
;   命令待ちでは対象になる走行が無いので受け付けない。
;   出力: Z=0 → 中断すべき / Z=1 → 中断しない。破壊: A, X (B/Y/U は保つ)。
;   (置き場所は区画の都合。参照はラベル経由のみで位置依存はない)
;------------------------------------------------------------------------------
brk_active:
                LDX     CURLIN          ; 実行中の行番号 ($FFFF = 直接モード)
                LEAX    1,X             ; $FFFF なら Z=1
                BEQ     ba_ret          ; 直接モード → 中断しない
                JSR     firq_read       ; $FD04 を読んで押下を BRK_FLAG へ
                TST     >BRK_FLAG       ; Z=0 なら押下 (控えが非 0)
ba_ret:
                RTS

warm_start:
                ; --- システムスタックを枠列の先頭へ戻す ---
                ; ここは「実行を打ち切って直接モードへ戻る」全経路の合流点であり、
                ; 到達したら誰にも復帰しない。END / STOP / LIST / エラー表示 /
                ; 直接モードの行の完了のいずれから来ても、途中で積んだ呼出
                ; フレームはもう使わない。放置すると打ち切りのたびにスタックが
                ; 下方へ流れ、繰り返すと枯渇する。
                ; 戻す先は EXEC_SP (= 最後の文の切れ目の S = 枠列の先頭)。入れ子の
                ; 枠 (GOSUB / FOR / WHILE) はシステムスタックに積んであり、STOP や
                ; END の後の CONT が続きを使うので、枠より下だけを捨てる。枠列を
                ; 空にするのは RUN (impl_exec_prog) だけである
                ; (参考書籍 CONT: BREAK / STOP / END の後に再開できる)。
                ; 枠列の先頭が直接モードの行の擬似レコードなら、その行はもう戻り先に
                ; ならない (下に枠が無い = RETURN / NEXT / WEND で戻る者がいない) ので
                ; 降ろす。実行し終えた直接モードの行と、LIST / END で打ち切った行が
                ; ここで片付く。
                JSR     dr_drop_top
                LDS     EXEC_SP
                JSR     print_inline    ; 先頭に改行を持つ。プロンプトは常に
                FCB     $0D,$0A         ;   「現在行を閉じてから次の行」に出る
                FCC     "Ready"         ;   体裁で、行頭にいるときはその改行が
                FCB     $0D,$0A,0       ;   空行 1 行として残る。誤りの表示と
                                        ;   Break の表示が末尾に改行を持たない
                                        ;   のは、この先頭の改行が表示行の終端を
                                        ;   兼ねるため
cmd_loop:
                ; 直接モードであることを、行の受け付けを始める前に確定させる。
                ; 行の読み込み・トークン化の段でエラーになり得る (範囲外の数値
                ; 定数など) ため、直前の実行が残した行番号のままだと、そのエラー
                ; 表示が実在しない行を指し、ON ERROR の捕捉先へも飛んでしまう。
                LDD     #$FFFF
                STD     CURLIN          ; CURLIN=$FFFF = direct モード
                JSR     impl_input_line
                JSR     expand_abbrev   ; キーワードの省略形を完全形へ (参考書籍)
                LDA     INPBUF
                BEQ     cmd_loop
                CMPA    #'0'
                BLO     cmd_direct
                CMPA    #'9'
                BHI     cmd_direct
                ; 演奏は止めない (プログラム行の受け付けは PLAY の中断事由ではない。
                ; 参考書籍)。行挿入は本バッファ (INPBUF) の上で直に
                ; 行うので、音符キュー (PLAYQ) とは領域が重ならない。
                LDX     #INPBUF         ; X = 入力バッファ先頭 (行番号から始まる)
                JSR     impl_insert_line
                BRA     cmd_loop
cmd_direct:
                ; 直接モードの行の実行開始で BREAK フラグを 0 に戻す。命令待ちの間に
                ; 押された中断キー (判定を伴わない $FD04 の読取で控えに残る) を、
                ; これから始める実行の最初の文で拾わないため。
                CLR     >BRK_FLAG
                ; --- 入力行を擬似行レコードとしてシステムスタックの枠列へ積む ---
                ;   [$FF][長さ n][$FF $FF][原文 n バイト][$00] (7t_defs.inc「システム
                ;   スタック上の枠」)。行番号欄が $FFFF なので直接モードの標識に
                ;   なり、実行用行バッファへの変換と行末・RETURN の扱いはプログラム
                ;   行と共通になる。入力バッファ (INPBUF) は文の実行中の作業域と
                ;   共用するため、実行を始める前にここへ写す。
                LDX     #INPBUF
cd_len:
                LDA     ,X+
                BNE     cd_len          ; X = 終端 $00 の次
                TFR     X,D
                SUBD    #INPBUF-4       ; D = n + 5 (枠の大きさ)
                STD     crs_new
                TFR     S,D
                SUBD    crs_new
                TFR     D,U             ; U = 新しい S (検査を通るまで S は動かさない)
                SUBD    #STK_RESERVE
                CMPD    STREND
                LBLO    err_rg          ; 未使用領域が足りない → Out Of Memory
                TFR     U,S             ; 先に S を下げる (割込のフレームに踏まれない)
                TFR     S,X
                LDA     #$FF
                LDB     crs_new+1
                SUBB    #5              ; B = n (n ≤ 255 なので下位 1 バイトで足りる)
                STD     ,X++            ; [$FF][n]
                LDD     #$FFFF
                STD     ,X++            ; 行番号欄 = $FFFF
                LDU     #INPBUF
cd_cp:
                LDA     ,U+
                STA     ,X+
                BNE     cd_cp           ; 終端 $00 まで写す
                ; 前の直接モードの行が同じ位置に載っていた控えを無効化してから変換
                JSR     clr_execrec
                TFR     S,X             ; X = 擬似行レコード
                JSR     exec_conv_line  ; CURLINE_PTR / CURLIN($FFFF) 設定 + 変換, X = 作業列
                JMP     iep_stmt_new    ; 実行ループへ (行末で iep_dend → warm_start)
; (直接モードの行は擬似行レコードとして実行ループ (iep_stmt_new) が実行する)
;------------------------------------------------------------------------------
; call_cmd2 — 二次コマンドトークン (A = TOK_2ND_FIRST..TOK_2ND_LAST) の
;   ハンドラを二次コマンドハンドラ表 (cmd2_hdl_tbl) から引いて呼ぶ。
;   直接モードの入口 (cmd_direct_2nd) と実行ループの入口 (iep_2nd) の共通の呼出。
;   入力: A = 二次コマンドトークン / X = 本文ポインタ
;   出力: X = ハンドラ復帰後の本文ポインタ
;------------------------------------------------------------------------------
call_cmd2:
                SUBA    #TOK_2ND_FIRST
                TFR     A,B
                ASLB
                PSHS    X
                LDX     #cmd2_hdl_tbl
                LDX     B,X
                JSR     ,X
                PULS    X,PC


                include "7t_using_int.inc"      ; PRINT USING 数値整形 4/4a (プール)
;==============================================================================
; $8684: コールドスタート
;==============================================================================
;------------------------------------------------------------------------------
; ssc_brk_cancel — サブ CPU 側の処理完了待ち (sscw_fall) から 1 周ごとに呼ぶ。
;   行入力 ($04) は 1 行が確定するまでサブ CPU 側で待ち続ける仕様なので、待って
;   いる間に中断操作を届けるには、キャンセル要求 ($FD05 のキャンセルビット) を
;   書いてサブ CPU 側へ割込みを掛ける必要がある。サブ CPU 側はその割込みで待ちを解き、
;   行を未完のまま返してくる。中断すべき状況でなければ何もしない。
;   破壊: A, X (B は呼出元の計数なので触らない)。
;   (置き場所は区画の都合。参照はラベル経由のみで位置依存はない)
;------------------------------------------------------------------------------
ssc_brk_cancel:
                JSR     brk_active      ; Z=0 → 中断すべき
                BEQ     sbc_ret
                LDA     #$40            ; 走行 + キャンセル要求
                STA     PORT_SUBCPU
sbc_ret:
                RTS

;==============================================================================
; コールドスタート
;==============================================================================
                include "7t_using_next.inc"     ; PRINT USING フィールド実行/次値 (プール)
; pool_gr_opts (続き 3/4): 機能 (PSET/PRESET/AND/OR/XOR) を解析する
pgo_c2:
                CMPA    #','
                LBNE    pgo_nofn            ; 機能の指定なし → 既定 (PSET)
pgo_fn2:
                LEAX    1,X                 ; ',' を消費
                JSR     impl_parse_func     ; B = 機能コード
                LBRA    pgo_ret
                include "7t_using_num4.inc"     ; PRINT USING 数値整形 4/4b (プール)
cold_start_main:
                ORCC    #$50
                CLRA
                TFR     A,DP
                LDS     #STACK_TOP
                ; 直接ページ ($0000-$00FF) を 0 に
                LDX     #$0000
clr_zp:
                CLR     ,X+
                CMPX    #$0100
                BNE     clr_zp
                ; BASIC 使用上限 −1 を既定 ($7FFE = ROM モードの上端 $7FFF − 1) に
                ;   初期化 (CLEAR n,m
                ;   で下げられる)
                LDD     #STACK_TOP-1
                STD     MEMTOP
                ; 拡張のためのフック ($022D-$02A1) を RTS で敷く (既定は RTS。参考書籍)
                IFNDEF  STAGE_ALT_RAM
                LDX     #$022D
                LDA     #$39            ; RTS
cold_hook_fill:
                STA     ,X+
                CMPX    #$02A2
                BNE     cold_hook_fill
                ENDC
                ; プログラムテキストの先頭は $0790 (ROM モードの既定。参考書籍)。
                ; $0600 以降がユーザ領域 (参考書籍)。
                IFDEF   STAGE_ALT_RAM
                LDD     #STAGE_TEXT
                ELSE
                LDD     #$0790
                ENDC
                STD     TXTTAB
                ADDD    #2              ; 空プログラム終端 [00 00] の直後 ($0792)
                STD     VARTAB
                ; 文字列領域を張り直してから、配列領域末尾と文字列領域を
                ;   共通ルーチンで空に揃える (システムスタックの上端は
                ;   warm_start が STRBOT から張り直す)。
                JSR     str_space_init
                ; 枠列の先頭 (EXEC_SP) を明示的に張る。warm_start は
                ;   LDS EXEC_SP でシステムスタックを枠列の先頭へ戻すので、
                ;   コールドスタートの経路でも先に確定させておく
                ;   (impl_exec_prog の LDS STRBOT / STS EXEC_SP と同じ値)。
                LDD     STRBOT
                STD     EXEC_SP
                JSR     vars_clear_all  ; ARYTAB = STREND = VARTAB + 文字列領域を空に
                LDX     TXTTAB
                CLR     ,X+
                CLR     ,X
                ; EXEC アドレス ($0217) と USR0〜USR9 のアドレス ($0219-$022C) の連続
                ; 11 ワード (2 バイト) を、未設定を表す Illegal Function Call の
                ; 入口 (ramstub_ifc) で埋める (未設定の既定値。参考書籍)。
                LDX     #ramstub_ifc
                LDU     #EXEC_ADDR
                LDB     #11
cold_fill_usr:
                STX     ,U++
                DECB
                BNE     cold_fill_usr
                JSR     fn_clear_tbl            ; DEF FN 定義表もクリア (未定義=0)
                JSR     devtbl_init     ; デバイス登録テーブル・ON〜GOSUB 割込
                                        ;   テーブル・ルーチンベクトル (7t_core.s)
                ; --- キーボード割込制御レジスタ ($FD02) の起動時初期化 ---
                ; 本 ROM は起動時にメイン CPU 側のキーボード割込制御
                ; レジスタ ($FD02) を既定状態へ初期化する。$FD02 はキーボード押下を
                ; どちらの CPU へ割込ませるかを司る制御レジスタで、この初期化を欠くと
                ; 制御ラインが未確定のまま残り、以後キーボード割込をメイン CPU の
                ; IRQ 経路へ切替える利用者の機械語で
                ; 割込が発生しなくなる。既定値 (bit0=0) は BASIC 自身の入力用に
                ; キーボードをサブ CPU 側へ向ける設定である。
                ; 値は $00。bit2 (周期タイマ割込の許可) は平常時は倒しておく。
                ;   刻みを要る場面 (PLAY の割込演奏・INTERVAL の計時) だけ BASIC が
                ;   取りに行く所有規約とし、それ以外の時は利用者の常駐機械語に
                ;   譲る (7t_time.inc 頭書き)。
                ;   bit6 (シリアル受信割込の許可) も立てない。本 ROM は受信要因を
                ;   清算しないので、立てると IRQ が塞がらず全入力が固まるため。
                ; この時点では I フラグがマスク中で、割込入口 (IRQ_HOOK) は下の
                ; time_init が設置してから ANDCC #$EF (本ルーチン末尾) で開く。
                CLR     PORT_CMT                ; $FD02 = 割込制御 初期化 (既定状態)
                ; --- BREAK/ホットリセット通知の ack ($FD04 読み捨て) ---
                ;   BREAK 通知が残ったままだと、本レジスタを一度読むまで FIRQ 要求線が
                ;   立ち続けるので、起動時に 1 回読んで ack する (F マスク中なので安全)。
                LDA     PORT_FIRQ               ; $FD04 読み = BREAK/リセット通知 ack
                JSR     init_ram_vars           ; RAM に置く作業変数の初期値設定
                ; サブシステム側の初期設定を先に済ませ、低位 RAM への常駐コード
                ;   設置は 2 本まとめて後段で行う (設置する側と使う側を分けて並べる)。
                JSR     impl_sub_init
                JSR     pf_init_defaults        ; 機能キー PF1〜PF10 の起動時既定値
                ; 低位 RAM の BIOS ジャンプ設置 (7t_diskboot.inc) (ここへ展開)
;------------------------------------------------------------------------------
; disk_lowmem_install — 低位 RAM の BIOS ジャンプ ($00DE-$00E0) の設置 (コールドから JSR)
;   ここに BIOS へのジャンプ命令を置く (JSR $DE で BIOS を呼べる)。飛び先は本 ROM の BIOS 入口。
;------------------------------------------------------------------------------
disk_lowmem_install:
                LDA     #$7E            ; JMP 拡張
                STA     BIOS_JMP
                LDX     #ml_io_dispatch
                STX     BIOS_JMP+1
                ; 低位RAM常駐スタブを配置 (ここへ展開)
;------------------------------------------------------------------------------
; 未設定の EXEC / USR の入口を低位 RAM に配置する。
;------------------------------------------------------------------------------
mlm_install_ramstub:
                ; ramstub_ifc は JMP errc_5 の 3 バイトだけなので直に組み立てる。
                LDA     #$7E                    ; JMP (拡張)
                STA     ramstub_ifc
                LDX     #errc_5
                STX     ramstub_ifc+1
                JSR     time_init               ; TIME$ 実時計の初期化 (7t_time.inc)
                JSR     play_stop               ; PLAY ワーク初期化 + PSG 初期状態 (7t_play.inc)
cold_banner:
                ; タイトル 2 行 → 空きバイト数 → Ready の順に出す (Ready は warm_start が出す)。
                ; 1 行目は画面の幅いっぱいを青の地にし、その上に文言を語ごとの色で描く。
                ;   文字表示はセルの地を黒に塗り替えるので、先に「全ドットの字形 ($87) を
                ;   青で 40 桁 (= 640 ドット)」出して帯を作り、文言は帯を残す SYMBOL ($19、
                ;   字形の立った点だけを打つ) で 40 桁の倍幅 (W=2) に合わせて上から描く。
                ;   桁が尽きた時点でカーソルは 2 行目の先頭へ移るので行送りは送らない
                ;   (行送りではないので文字色の送り直しも起きず、2 行目の先頭の $11 が効く)。
                JSR     out_vec_screen  ; 出力先を画面へ戻す (共通化)
                JSR     print_inline    ; 直後の文言を出して、その次から続ける
                FCB     $11,1,0         ; 文字色 = 青 (帯の色)
                LDB     #40             ; 40 桁 = 640 ドット
cb_band:
                LDA     #$87            ; 全ドットの字形 (impl_putchar は A を壊す)
                JSR     impl_putchar    ; B は保たれる
                DECB
                BNE     cb_band
                ; --- 1 行目の文言を SYMBOL で語ごとに描く (表は 7t31_banner.inc) ---
                ;   表の先頭 8 バイト = 相対 4〜11 の固定部 (F, A, W, H, X, Y)。続いて
                ;   語ごとに [色][X 座標の下位][文字数][文字列]、色 0 で終り。
                LDU     #title_tbl
                LDX     #SUBCMD_P2
                JSR     mcopy8          ; 固定部を写す (U は最初の語へ進む)
cb_word:
                LDD     ,U++            ; A = 色 (0 = 終り), B = X 座標の下位
                TSTA
                BEQ     banner_line2    ; 表の後ろの 2 行目の出力へ
                STA     SUBCMD_P1       ; CL
                STB     SUBCMD_P7       ; X の下位 (上位は固定部の 0)
                LDB     ,U              ; 文字数
                INCB                    ; 文字数の欄ごと写す
                LDX     #SUBCMD_P10
                JSR     mcp_l           ; [文字数][文字列] → 相対 12〜 (U は次の語へ)
                LDA     #$19
                JSR     sub_send_cmd_a  ; SYMBOL
                BRA     cb_word
                include "7t31_banner.inc"       ; タイトルの表・著作権行 (バージョン固有層)
                ; --- nnnnn bytes free 行 (FRE(0) と同じ式: 文字列領域の下限 - 変数域末尾) ---
                LDD     STRBOT
                SUBD    STREND          ; D = 空きバイト数
                JSR     print_uint16    ; out_vec は上の out_vec_screen で画面へ向けてある
                JSR     print_inline
                FCC     " bytes free"
                FCB     $0D,$0A,0
                ; --- メイン CPU IRQ 受付を許可 (I フラグ解除) ---
                ; 起動仕様: 初期化完了後は BASIC 本文をメイン CPU IRQ 許可状態で
                ; 走らせる。周期タイマ割込 ($FD02 bit2=1) は TIME$ 実時計の tick
                ; として IRQ_HOOK → TICK_ENTRY (低位 RAM 常駐) で受ける。
                ; 利用者機械語へ制御を渡す時は time_disarm でタイマを止めるため、
                ; 利用者機械語 (LOADM ",R") が自前の IRQ ハンドラ・割込源を設定した
                ; 後は、その周期割込を BASIC から引き継いだ I=0 のまま受け付けられる。
                ANDCC   #$EF            ; I フラグ解除 (IRQ 許可)
                JMP     warm_start

;==============================================================================
; 行挿入 impl_insert_line (置き場所は容量の都合。参照はラベル経由のみで
;   位置依存なし)
;
;   入力: X = 入力行 (先頭が行番号の ASCII)。Y/U は保存、D/X/CC は破壊。
;
;   行リンクリスト形式:
;     [次行ptr 2B][行番号 2B][本文 ASCII 原文][00 1B] ... 終端 [00 00]
;   (最終行の次行ptr は終端 [00 00] の位置を指す)
;
;   対話編集の仕様:
;     - 同一行番号の再入力は既存行を置換する
;     - 行番号のみ (以降が空白のみ含む) の入力は該当行を削除する。
;       該当行が無ければ何もしない (エラー表示なし)
;     - 行番号順を保って途中挿入する (LIST/RUN は常に行番号昇順)
;     - 行番号 0 も有効な行として格納する
;     - いずれの形態でも変数は全消去 (ARYTAB/STREND を VARTAB へ同期)
;==============================================================================

; ins_line_dx — 行番号と本文を別々に受け取る入口 (AUTO が使う)。
;   入力: D = 行番号 / X = 本文 (原文) の先頭。以降は impl_insert_line と同じ。
ins_line_dx:
                PSHS    Y,U             ; Y, U を保存
                BRA     iil_have
impl_insert_line:
                PSHS    Y,U             ; Y, U を保存
                JSR     parse_prog_linenum  ; X を進めながら D = 行番号 (上限検査つき)
iil_have:
                PSHS    D               ; [S+0:1] = 行番号
                ; ステートメント部分 (X 以降) は**原文のまま**格納する。
                ; 行入力時の変換は行わない。本文は行番号の数字が尽きた位置から
                ; 行末までで、空白・大小文字とも打ち込んだままである。
                ; ステートメント開始位置は mlm_offset に保持する (LOADM 専用
                ; ワークの借用。行挿入と LOADM は非同時、かつ stream_lines 側は
                ; 挿入後に毎行再設定するため安全)。
                STX     mlm_offset
                ;
                ; --- 既存の同番号行があれば削除 (置換・削除の共通前段) ---
                ; 詰め直しの実体は DELETE と共用する prog_cut に持たせてある。
                LDD     ,S              ; D = 行番号
                JSR     impl_find_line  ; Z=0: X = 行レコード先頭
                BEQ     iil_no_old
                LDU     ,X              ; U = 行末尾 (= 次レコード先頭)
                JSR     prog_cut        ; [X, U) を切り取って後続を詰める
iil_no_old:
                ; --- 本文が空 (行番号のみ) なら削除のみで完了 ---
                LDX     mlm_offset      ; X = 本文 (原文) 先頭
                LDA     ,X
                BEQ    iil_finish
                ; --- トークン長からレコードサイズを計算 ---
iil_len1:
                LDA     ,X+
                BNE     iil_len1
                TFR     X,D
                SUBD    mlm_offset      ; D = 本文長 (終端 0 含む)
                ADDD    #4              ; + 次行ptr 2B + 行番号 2B
                PSHS    D               ; [S+0:1] = recsize, [S+2:3] = 行番号
                ; --- 挿入位置 P = 最初に行番号 > 対象 となる行 (無ければ終端) ---
                LDX     TXTTAB
iil_pos:
                LDD     ,X
                BEQ     iil_pos_ok      ; 終端 [00 00] → 末尾へ追記
                LDD     2,X
                CMPD    2,S             ; 対象行番号と比較 (符号なし)
                BHI     iil_pos_ok
                LDX     ,X
                BRA     iil_pos
iil_pos_ok:
                ; --- [P .. 旧VARTAB) を recsize 分だけ後方から上へ移動 ---
                LDU     VARTAB          ; U = 旧末尾 (排他, コピー元)
                TFR     U,D
                ADDD    ,S
                STD     VARTAB          ; VARTAB += recsize
                TFR     D,Y             ; Y = 新末尾 (排他, コピー先)
iil_up_mv:
                PSHS    X
                CMPU    ,S++            ; コピー元が挿入位置 P に達したら終了
                BEQ     iil_up_done
                LDA     ,-U
                STA     ,-Y
                BRA     iil_up_mv
iil_up_done:
                ; --- 新レコードを P (=X) へ書き込む ---
                TFR     X,D
                ADDD    ,S              ; D = P + recsize = 直後レコードの新位置
                STD     ,X              ; next_ptr
                TFR     D,U             ; U = 直後レコード
                ; リンク補正: 上へ移動したレコードの次行ptr は移動前の値の
                ; ままなので一律 +recsize する (終端 [00 00] で停止)
                BRA     iil_in_fx1
iil_in_fx2:
                ADDD    ,S              ; 新値 = 旧値 + recsize
                STD     ,U
                TFR     D,U             ; 次レコードへ
iil_in_fx1:
                LDD     ,U
                BNE     iil_in_fx2
                ; 行番号 + 本文 (原文) を書き込む (次行ptr は書込済)
                LDD     2,S             ; D = 行番号
                STD     2,X
                LEAX    4,X
                LDU     mlm_offset      ; 本文 = 呼出元バッファ上
iil_tok_wr:
                LDA     ,U+
                STA     ,X+
                BNE     iil_tok_wr
                LEAS    2,S             ; recsize を捨てる
iil_finish:
                LEAS    2,S             ; 行番号を捨てる
                ; 行入力は形態を問わず全変数を消去する (挿入・置換・削除・
                ; 該当なし削除のいずれでも。動作観察)。
                ; ARYTAB/STREND を VARTAB へ同期する (同期を維持)。
                JSR     vars_clear_all  ; 共通ルーチン
                PULS    Y,U             ; Y, U を復元
                ; 行の追加・置換・削除の後は CONT で再開できない。
                ; 記録済みの再開位置は本文の移動で無効になっているため破棄する。
                JMP     cont_clear

                include "7t_using_num2b.inc"    ; PRINT USING 数値整形 3/4 (プール)
;==============================================================================
; 関数ジャンプテーブル (43 エントリ × 2B = 86B)
;==============================================================================
fn_jt:
                FDB     impl_fn_anport  ; $80 ANPORT
                FDB     impl_fn_abs     ; $81 ABS
                FDB     impl_fn_asc     ; $82 ASC
                FDB     impl_fn_atn     ; $83 ATN
                FDB     impl_fn_csrlin  ; $84 CSRLIN
                FDB     impl_fn_cdbl    ; $85 CDBL
                FDB     impl_fn_chr_d   ; $86 CHR$
                FDB     impl_fn_cint    ; $87 CINT
                FDB     impl_fn_csng    ; $88 CSNG
                FDB     impl_fn_cos     ; $89 COS
                FDB     impl_fn_date    ; $8A DATE
                FDB     impl_fn_eof     ; $8B EOF
                FDB     impl_fn_exp     ; $8C EXP
                FDB     impl_fn_fix     ; $8D FIX
                FDB     impl_fn_fre     ; $8E FRE
                FDB     impl_fn_hex_d   ; $8F HEX$
                FDB     impl_fn_inkey_d ; $90 INKEY$
                FDB     impl_fn_input   ; $91 INPUT
                FDB     impl_fn_instr   ; $92 INSTR
                FDB     impl_fn_int     ; $93 INT
                FDB     impl_fn_left_d  ; $94 LEFT$
                FDB     impl_fn_len     ; $95 LEN
                FDB     impl_fn_lof     ; $96 LOF
                FDB     impl_fn_log     ; $97 LOG
                FDB     impl_fn_mid_d   ; $98 MID$
                FDB     impl_fn_oct_d   ; $99 OCT$
                FDB     impl_fn_point   ; $9A POINT
                FDB     impl_fn_peek    ; $9B PEEK
                FDB     pen_dev60       ; $9C PEN — 装置なし → Error 60
                FDB     impl_fn_pos     ; $9D POS
                FDB     impl_fn_right_d ; $9E RIGHT$
                FDB     impl_fn_rnd     ; $9F RND
                FDB     impl_fn_strng_d ; $A0 STRING$
                FDB     impl_fn_screen  ; $A1 SCREEN
                FDB     impl_fn_space_d ; $A2 SPACE$
                FDB     impl_fn_str_d   ; $A3 STR$
                FDB     impl_fn_sgn     ; $A4 SGN
                FDB     impl_fn_sin     ; $A5 SIN
                FDB     impl_fn_sqr     ; $A6 SQR
                FDB     impl_fn_time    ; $A7 TIME
                FDB     impl_fn_tan     ; $A8 TAN
                FDB     impl_fn_varptr  ; $A9 VARPTR
                FDB     impl_fn_val     ; $AA VAL

                include "7t_using_exp3.inc"     ; PRINT USING 指数部出力 (プール)
;------------------------------------------------------------------------------
; nl_blank — 表示の前置き改行。プール配置 (呼出は JSR 拡張のみ)。
;   「桁 0 へ揃えたうえで、さらに 1 行空ける」。行頭にいれば改行 1 回 (= 空行が
;   1 行残る)、行の途中にいれば改行 2 回 (= その行を閉じてから空行 1 行)。
;   これによりエラー名・Break は常に行頭から始まり、直前に必ず空行が 1 行入る。
;   破壊: A のみ (disk_crlf と同じ)。
;   BSR nb_one は続く JMP をサブルーチンとして呼ぶ書き方で、改行 2 回を
;   JSR/JMP を重ねずに 9 バイトへ収めるためのもの (当プールの残余が 9)。
;------------------------------------------------------------------------------
nl_blank:
                TST     PRINT_COL       ; 行の途中か?
                BEQ     nb_one          ; 桁 0 → 改行 1 回だけ
                BSR     nb_one          ; 行の途中 → まずその行を閉じる
nb_one:
                JMP     disk_crlf       ; 空行を 1 行入れて復帰
;==============================================================================
; コマンドジャンプテーブル (105 エントリ × 2B = 210B)
;   命令トークン TOK_FIRST- を順に引く。欄の並びは予約語表 kw_tbl と同順で、
;   scripts/gen_7t_keywords.py が同期生成する (手書き禁止。1 欄のずれが暴走に
;   なる)。表の置き場所は本実装の都合で決めた (参照はラベル経由のみ)。
;   ion_err = 句の一部にしかなれない綴り (TO / THEN / 演算子など)。文の先頭に
;     来たら Syntax Error として打ち切る。綴り間違いで予約語が割れた本文
;     (`TO=5` など) を黙って読み飛ばさないため。
;   impl_stub = 文としては正しいが本 ROM が持たない命令 (HARDC/COM/BUBINI
;     等)。あちらは黙って読み飛ばす。
;==============================================================================
cmd_jt:
                FDB     impl_rem        ; $80 '
                FDB     ion_err         ; $81 *
                FDB     ion_err         ; $82 +
                FDB     ion_err         ; $83 -
                FDB     ion_err         ; $84 /
                FDB     ion_err         ; $85 <
                FDB     ion_err         ; $86 =
                FDB     ion_err         ; $87 >
                FDB     impl_print_disp ; $88 ? (PRINT の省略形)
                FDB     impl_auto       ; $89 AUTO
                FDB     ion_err         ; $8A AND
                FDB     impl_stub       ; $8B BUBINI
                FDB     impl_beep_snd   ; $8C BEEP
                FDB     impl_stub       ; $8D BUBR
                FDB     impl_stub       ; $8E BUBW
                FDB     impl_connect    ; $8F CONNECT
                FDB     impl_console    ; $90 CONSOLE
                FDB     impl_circle     ; $91 CIRCLE
                FDB     impl_clear      ; $92 CLEAR
                FDB     impl_close      ; $93 CLOSE
                FDB     impl_color_disp ; $94 COLOR (形式1/2/3 振り分け)
                FDB     impl_cont       ; $95 CONT
                FDB     impl_cls        ; $96 CLS
                FDB     impl_stub       ; $97 COM
                FDB     impl_defdbl     ; $98 DEFDBL
                FDB     impl_defint     ; $99 DEFINT
                FDB     impl_defsng     ; $9A DEFSNG
                FDB     impl_defstr     ; $9B DEFSTR
                FDB     impl_delete     ; $9C DELETE
                FDB     impl_data       ; $9D DATA
                FDB     impl_def        ; $9E DEF
                FDB     impl_dim        ; $9F DIM
                FDB     impl_error      ; $A0 ERROR
                FDB     impl_edit       ; $A1 EDIT
                FDB     impl_else       ; $A2 ELSE
                FDB     impl_exec       ; $A3 EXEC
                FDB     impl_end        ; $A4 END
                FDB     ion_err         ; $A5 EQV
                FDB     ion_err         ; $A6 ERL
                FDB     ion_err         ; $A7 ERR
                FDB     impl_files      ; $A8 FILES
                FDB     impl_for        ; $A9 FOR
                FDB     impl_fn         ; $AA FN
                FDB     impl_gcursor    ; $AB GCURSOR
                FDB     impl_get        ; $AC GET
                FDB     impl_go         ; $AD GO
                FDB     impl_stub       ; $AE HARDC
                FDB     impl_interval   ; $AF INTERVAL
                FDB     ion_err         ; $B0 IMP
                FDB     impl_if         ; $B1 IF
                FDB     impl_stub       ; $B2 KILL
                FDB     impl_key        ; $B3 KEY
                FDB     impl_locate3    ; $B4 LOCATE (第3引数 ,S 対応, free 領域)
                FDB     impl_line       ; $B5 LINE
                FDB     impl_list       ; $B6 LIST
                FDB     impl_load       ; $B7 LOAD
                FDB     impl_let        ; $B8 LET
                FDB     impl_merge      ; $B9 MERGE
                FDB     impl_motor      ; $BA MOTOR
                FDB     ion_err         ; $BB MOD
                FDB     impl_mon        ; $BC MON
                FDB     impl_next       ; $BD NEXT
                FDB     impl_new        ; $BE NEW
                FDB     ion_err         ; $BF NOT
                FDB     impl_open       ; $C0 OPEN (KYBD:/SCRN: のみ)
                FDB     ion_err         ; $C1 OFF
                FDB     impl_on_entry   ; $C2 ON (ON INTERVAL GOSUB を先に見る)
                FDB     ion_err         ; $C3 OR
                FDB     impl_preset     ; $C4 PRESET
                FDB     impl_paint      ; $C5 PAINT
                FDB     impl_print_disp ; $C6 PRINT (@座標描画 振り分け, free 領域)
                FDB     impl_poke       ; $C7 POKE
                FDB     impl_pset       ; $C8 PSET
                FDB     impl_put        ; $C9 PUT
                FDB     impl_randomize  ; $CA RANDOMIZE
                FDB     impl_restore    ; $CB RESTORE
                FDB     impl_resume     ; $CC RESUME
                FDB     impl_return_entry ; $CD RETURN (INTERVAL 自動保留の解除入口)
                FDB     impl_renum      ; $CE RENUM
                FDB     impl_read       ; $CF READ
                FDB     impl_rem        ; $D0 REM
                FDB     impl_run        ; $D1 RUN
                FDB     impl_symbol     ; $D2 SYMBOL
                FDB     impl_skipf      ; $D3 SKIPF
                FDB     impl_save       ; $D4 SAVE
                FDB     ion_err         ; $D5 SPC(
                FDB     ion_err         ; $D6 STEP
                FDB     impl_stop       ; $D7 STOP
                FDB     impl_swap       ; $D8 SWAP
                FDB     ion_err         ; $D9 SUB
                FDB     impl_troff      ; $DA TROFF
                FDB     ion_err         ; $DB TAB(
                FDB     impl_term       ; $DC TERM
                FDB     ion_err         ; $DD THEN
                FDB     impl_tron       ; $DE TRON
                FDB     ion_err         ; $DF TO
                FDB     impl_unlist     ; $E0 UNLIST
                FDB     ion_err         ; $E1 USING
                FDB     ion_err         ; $E2 USR
                FDB     impl_while      ; $E3 WHILE
                FDB     impl_width      ; $E4 WIDTH
                FDB     impl_wend       ; $E5 WEND
                FDB     ion_err         ; $E6 XOR
                FDB     ion_err         ; $E7 \
                FDB     ion_err         ; $E8 ^
cmd_jt_end:

;==============================================================================
; 予約語テーブル (命令語 105 語 → 関数語 43 語の順に連続して置く)
;   綴りは参考書籍に基づき、語末の 1 文字だけ bit7 を立てて区切る。
;   並びは本プロジェクトが独自に定めた規則 (先頭 1 文字でまとめ、同一先頭字の
;   中は綴り長の降順 → 同長は綴り順) で、トークン値は表の位置から機械的に
;   決まる。表とジャンプ表・トークン定数は scripts/gen_7t_keywords.py が同期
;   生成する (手書き禁止)。「同一表内で、ある語が別の語の先頭部分になる組は
;   長い綴りが先」という最長一致の成立条件は生成器が機械検査する。
;   表の置き場所は本実装の都合で決めた (参照はラベル経由のみ)。命令語表を
;   先に置くのは、照合本体が「一致位置が関数語表の先頭より後か」の 1 回の
;   比較で $FF 前置の要否を決められるようにするため (二次関数語表はさらに
;   後方にあるので同じ比較で前置つきになる)。
;==============================================================================
kw_tbl:
                FCB     $27+$80                         ; $80 '
                FCB     '*'+$80                         ; $81 *
                FCB     '+'+$80                         ; $82 +
                FCB     '-'+$80                         ; $83 -
                FCB     '/'+$80                         ; $84 /
                FCB     '<'+$80                         ; $85 <
                FCB     '='+$80                         ; $86 =
                FCB     '>'+$80                         ; $87 >
                FCB     '?'+$80                         ; $88 ?
                FCB     'A','U','T','O'+$80             ; $89 AUTO
                FCB     'A','N','D'+$80                 ; $8A AND
                FCB     'B','U','B','I','N','I'+$80     ; $8B BUBINI
                FCB     'B','E','E','P'+$80             ; $8C BEEP
                FCB     'B','U','B','R'+$80             ; $8D BUBR
                FCB     'B','U','B','W'+$80             ; $8E BUBW
                FCB     'C','O','N','N','E','C','T'+$80 ; $8F CONNECT
                FCB     'C','O','N','S','O','L','E'+$80 ; $90 CONSOLE
                FCB     'C','I','R','C','L','E'+$80     ; $91 CIRCLE
                FCB     'C','L','E','A','R'+$80         ; $92 CLEAR
                FCB     'C','L','O','S','E'+$80         ; $93 CLOSE
                FCB     'C','O','L','O','R'+$80         ; $94 COLOR
                FCB     'C','O','N','T'+$80             ; $95 CONT
                FCB     'C','L','S'+$80                 ; $96 CLS
                FCB     'C','O','M'+$80                 ; $97 COM
                FCB     'D','E','F','D','B','L'+$80     ; $98 DEFDBL
                FCB     'D','E','F','I','N','T'+$80     ; $99 DEFINT
                FCB     'D','E','F','S','N','G'+$80     ; $9A DEFSNG
                FCB     'D','E','F','S','T','R'+$80     ; $9B DEFSTR
                FCB     'D','E','L','E','T','E'+$80     ; $9C DELETE
                FCB     'D','A','T','A'+$80             ; $9D DATA
                FCB     'D','E','F'+$80                 ; $9E DEF
                FCB     'D','I','M'+$80                 ; $9F DIM
                FCB     'E','R','R','O','R'+$80         ; $A0 ERROR
                FCB     'E','D','I','T'+$80             ; $A1 EDIT
                FCB     'E','L','S','E'+$80             ; $A2 ELSE
                FCB     'E','X','E','C'+$80             ; $A3 EXEC
                FCB     'E','N','D'+$80                 ; $A4 END
                FCB     'E','Q','V'+$80                 ; $A5 EQV
                FCB     'E','R','L'+$80                 ; $A6 ERL
                FCB     'E','R','R'+$80                 ; $A7 ERR
                FCB     'F','I','L','E','S'+$80         ; $A8 FILES
                FCB     'F','O','R'+$80                 ; $A9 FOR
                FCB     'F','N'+$80                     ; $AA FN
                FCB     'G','C','U','R','S','O','R'+$80 ; $AB GCURSOR
                FCB     'G','E','T'+$80                 ; $AC GET
                FCB     'G','O'+$80                     ; $AD GO
                FCB     'H','A','R','D','C'+$80         ; $AE HARDC
                FCB     'I','N','T','E','R','V','A','L'+$80 ; $AF INTERVAL
                FCB     'I','M','P'+$80                 ; $B0 IMP
                FCB     'I','F'+$80                     ; $B1 IF
                FCB     'K','I','L','L'+$80             ; $B2 KILL
                FCB     'K','E','Y'+$80                 ; $B3 KEY
                FCB     'L','O','C','A','T','E'+$80     ; $B4 LOCATE
                FCB     'L','I','N','E'+$80             ; $B5 LINE
                FCB     'L','I','S','T'+$80             ; $B6 LIST
                FCB     'L','O','A','D'+$80             ; $B7 LOAD
                FCB     'L','E','T'+$80                 ; $B8 LET
                FCB     'M','E','R','G','E'+$80         ; $B9 MERGE
                FCB     'M','O','T','O','R'+$80         ; $BA MOTOR
                FCB     'M','O','D'+$80                 ; $BB MOD
                FCB     'M','O','N'+$80                 ; $BC MON
                FCB     'N','E','X','T'+$80             ; $BD NEXT
                FCB     'N','E','W'+$80                 ; $BE NEW
                FCB     'N','O','T'+$80                 ; $BF NOT
                FCB     'O','P','E','N'+$80             ; $C0 OPEN
                FCB     'O','F','F'+$80                 ; $C1 OFF
                FCB     'O','N'+$80                     ; $C2 ON
                FCB     'O','R'+$80                     ; $C3 OR
                FCB     'P','R','E','S','E','T'+$80     ; $C4 PRESET
                FCB     'P','A','I','N','T'+$80         ; $C5 PAINT
                FCB     'P','R','I','N','T'+$80         ; $C6 PRINT
                FCB     'P','O','K','E'+$80             ; $C7 POKE
                FCB     'P','S','E','T'+$80             ; $C8 PSET
                FCB     'P','U','T'+$80                 ; $C9 PUT
                FCB     'R','A','N','D','O','M','I','Z','E'+$80 ; $CA RANDOMIZE
                FCB     'R','E','S','T','O','R','E'+$80 ; $CB RESTORE
                FCB     'R','E','S','U','M','E'+$80     ; $CC RESUME
                FCB     'R','E','T','U','R','N'+$80     ; $CD RETURN
                FCB     'R','E','N','U','M'+$80         ; $CE RENUM
                FCB     'R','E','A','D'+$80             ; $CF READ
                FCB     'R','E','M'+$80                 ; $D0 REM
                FCB     'R','U','N'+$80                 ; $D1 RUN
                FCB     'S','Y','M','B','O','L'+$80     ; $D2 SYMBOL
                FCB     'S','K','I','P','F'+$80         ; $D3 SKIPF
                FCB     'S','A','V','E'+$80             ; $D4 SAVE
                FCB     'S','P','C','('+$80             ; $D5 SPC(
                FCB     'S','T','E','P'+$80             ; $D6 STEP
                FCB     'S','T','O','P'+$80             ; $D7 STOP
                FCB     'S','W','A','P'+$80             ; $D8 SWAP
                FCB     'S','U','B'+$80                 ; $D9 SUB
                FCB     'T','R','O','F','F'+$80         ; $DA TROFF
                FCB     'T','A','B','('+$80             ; $DB TAB(
                FCB     'T','E','R','M'+$80             ; $DC TERM
                FCB     'T','H','E','N'+$80             ; $DD THEN
                FCB     'T','R','O','N'+$80             ; $DE TRON
                FCB     'T','O'+$80                     ; $DF TO
                FCB     'U','N','L','I','S','T'+$80     ; $E0 UNLIST
                FCB     'U','S','I','N','G'+$80         ; $E1 USING
                FCB     'U','S','R'+$80                 ; $E2 USR
                FCB     'W','H','I','L','E'+$80         ; $E3 WHILE
                FCB     'W','I','D','T','H'+$80         ; $E4 WIDTH
                FCB     'W','E','N','D'+$80             ; $E5 WEND
                FCB     'X','O','R'+$80                 ; $E6 XOR
                FCB     $5C+$80                         ; $E7 \
                FCB     '^'+$80                         ; $E8 ^
kw_tbl_end:
kw_fn_tbl:
                FCB     'A','N','P','O','R','T'+$80     ; $80 ANPORT
                FCB     'A','B','S'+$80                 ; $81 ABS
                FCB     'A','S','C'+$80                 ; $82 ASC
                FCB     'A','T','N'+$80                 ; $83 ATN
                FCB     'C','S','R','L','I','N'+$80     ; $84 CSRLIN
                FCB     'C','D','B','L'+$80             ; $85 CDBL
                FCB     'C','H','R','$'+$80             ; $86 CHR$
                FCB     'C','I','N','T'+$80             ; $87 CINT
                FCB     'C','S','N','G'+$80             ; $88 CSNG
                FCB     'C','O','S'+$80                 ; $89 COS
                FCB     'D','A','T','E'+$80             ; $8A DATE
                FCB     'E','O','F'+$80                 ; $8B EOF
                FCB     'E','X','P'+$80                 ; $8C EXP
                FCB     'F','I','X'+$80                 ; $8D FIX
                FCB     'F','R','E'+$80                 ; $8E FRE
                FCB     'H','E','X','$'+$80             ; $8F HEX$
                FCB     'I','N','K','E','Y','$'+$80     ; $90 INKEY$
                FCB     'I','N','P','U','T'+$80         ; $91 INPUT
                FCB     'I','N','S','T','R'+$80         ; $92 INSTR
                FCB     'I','N','T'+$80                 ; $93 INT
                FCB     'L','E','F','T','$'+$80         ; $94 LEFT$
                FCB     'L','E','N'+$80                 ; $95 LEN
                FCB     'L','O','F'+$80                 ; $96 LOF
                FCB     'L','O','G'+$80                 ; $97 LOG
                FCB     'M','I','D','$'+$80             ; $98 MID$
                FCB     'O','C','T','$'+$80             ; $99 OCT$
                FCB     'P','O','I','N','T'+$80         ; $9A POINT
                FCB     'P','E','E','K'+$80             ; $9B PEEK
                FCB     'P','E','N'+$80                 ; $9C PEN
                FCB     'P','O','S'+$80                 ; $9D POS
                FCB     'R','I','G','H','T','$'+$80     ; $9E RIGHT$
                FCB     'R','N','D'+$80                 ; $9F RND
                FCB     'S','T','R','I','N','G','$'+$80 ; $A0 STRING$
                FCB     'S','C','R','E','E','N'+$80     ; $A1 SCREEN
                FCB     'S','P','A','C','E','$'+$80     ; $A2 SPACE$
                FCB     'S','T','R','$'+$80             ; $A3 STR$
                FCB     'S','G','N'+$80                 ; $A4 SGN
                FCB     'S','I','N'+$80                 ; $A5 SIN
                FCB     'S','Q','R'+$80                 ; $A6 SQR
                FCB     'T','I','M','E'+$80             ; $A7 TIME
                FCB     'T','A','N'+$80                 ; $A8 TAN
                FCB     'V','A','R','P','T','R'+$80     ; $A9 VARPTR
                FCB     'V','A','L'+$80                 ; $AA VAL
kw_fn_tbl_end:





; ==============================================================================
; GET/PUT(@) グラフィックブロック転送
; (ジャンプテーブル経由で呼ばれるため配置位置は任意)
; ==============================================================================
; GET / PUT（グラフィック用）
; GET@ (x1,y1)-(x2,y2), 配列名 [,G [,カラー...]]
; PUT@ (x1,y1)-(x2,y2), 配列名 [,機能 [,カラー]]
; subsys $1B (Get Block 1) / $1C (Put Block 1)
;
; 参考書籍の GET/PUT(@) 仕様: 矩形 (x1,y1)-(x2,y2) と整数配列の間で
; ビットイメージを転送する。配列要素には行ごとにパックしたバイト列が入る。
; 最重要: 「文全体 (座標・配列名・機能・カラー) を最後まで消費して復帰する」
; こと。途中で打ち切ると後続トークン (,配列名…) が次文として誤実行される。
;
; igp_common で GET/PUT 共通の「矩形解析 → 配列アドレス取得 → 残引数消費 →
; 矩形を共有 RAM へ格納」を行い、A にコマンドコード ($1B/$1C) を受けて発行する。

; igp_common: 入力 A=コマンド($1B/$1C)。矩形解析・配列・残引数・送信を一括処理。
;   配列要素先頭は gp_arr_ptr、最初のオプション値 (機能/対象色) は gp_func。
igp_common:
                STA     SUBCMD_CMD
                CLR     gp_attr                 ; 既定 = 形式1/3 (@A でない)
                ; GET@/PUT@ の '@' ($40) を読み飛ばす (省略形 GET/PUT も許容)
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                CMPA    #'@'
                BNE     igc_no_at
                LEAX    1,X                     ; '@' 消費
                ; 形式2/4 (GET@A/PUT@A): '@' 直後の 'A' を受理する (省略時は形式1/3)。
                LDA     ,X
                CMPA    #'A'
                BNE     igc_no_at
                LEAX    1,X                     ; 'A' 消費 (形式2/4)
                INC     gp_attr                 ; @A (3プレーン) モード
igc_no_at:
                ; (x1,y1)-(x2,y2)
                JSR     impl_parse_xy_cmd   ; (x1,y1) → tmpX16/tmpY16
                ; 範囲セパレータは ASCII '-' ($2D) またはトークン化 '-' ($DA)
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                CMPA    #'-'
                BEQ     igc_range
                CMPA    #TOK_MINUS
                BNE     igc_rect_done
igc_range:
                LEAX    1,X
                JSR     impl_parse_xy_cmd2  ; (x2,y2) → tmpX16_2/tmpY16_2
                BRA     igc_arr
igc_rect_done:
                LDD     tmpX16
                STD     tmpX16_2
                LDD     tmpY16
                STD     tmpY16_2
igc_arr:
                ; 矩形を共有 RAM 並び ($D383=X0/Y0/X1/Y1, 16bit BE) へ格納
                LDD     tmpX16
                STD     SUBCMD_P1
                JSR     sub_stage_xy        ; Y0 / X1 / Y1 (共通化)
                LDD     ARYTAB              ; 配列ブロックの基準を退避 (再結合用)
                STD     gp_arr_base         ;   X は本文ポインタなので D で退避する
                CLRA                        ; (= LDD #0 より 1 バイト短い)
                CLRB
                STD     gp_arr_ptr
                CLR     gp_func
                CLR     gp_optn
                CLR     gp_kwfn             ; 機能語の指定なし (= 形式1/2 の想定)
                LDA     cur_fg_color
                STA     gp_color            ; 色引数の既定 = 現在の描画色
                CLR     SUBCMD_P9           ; GET@ 形式1 の明示色リストの個数 ($D38B)
                ; 残りの引数 (,配列名 [,機能/対象色 [,カラー...]]) を末尾まで消費
igc_optlp:
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     igc_send_r
                CMPA    #':'
                BEQ     igc_send_r
                CMPA    #','
                BNE     igc_send_r
                JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
                LDA     ,X
                BEQ     igc_send_r
                CMPA    #':'
                BEQ     igc_send
                CMPA    #','
                BEQ     igc_optlp           ; ",," 空引数
                ; 1個目 (配列名: 英字始まり) はアドレス参照、それ以外は式評価。
                LDA     gp_optn
                BNE     igc_opt_expr
                LDA     ,X
                CMPA    #'A'
                BLO     igc_opt_expr
                CMPA    #'Z'
                BHI     igc_opt_expr
                BSR     igc_arrname         ; U = 配列要素アドレス (下記)
                STU     gp_arr_ptr
                INC     gp_optn
                BRA     igc_optlp
; igc_send への近距離中継。引数の走査を打ち切る 4 箇所は igc_send まで短分岐が
;   届かないので、無条件分岐の直後 (落ち込みの無い位置) に 1 本置いて、
;   各判定を 2 byte の短分岐で済ませる。
igc_send_r:
                BRA     igc_send
igc_opt_expr:
                ; GET@ の 2 番目の引数は書式指定子 'G' に限られる (参考書籍)。
                ;   他の綴りを式として評価すると未定義変数が新規に作られ、
                ;   解析済みの配列アドレスが陳腐化する。
                LDA     SUBCMD_CMD
                CMPA    #$1B
                BNE     igc_oe_tok
                LDA     gp_optn
                CMPA    #1
                BNE     igc_oe_tok
                LDA     ,X
                CMPA    #'G'
                LBNE    errc_2              ; 'G' 以外 → Syntax Error
igc_oe_tok:
                ; 機能キーワード (PSET/PRESET/AND/OR/XOR トークン) は直接コード化
                ; する。式評価へ渡すと未消費のまま文が打ち切られ、機能引数と色引数
                ; が渡らない (PUT@ ...,A%,PSET,5 の形)。
                LDA     ,X
                CLRB                    ; FUNC_PSET = 0
                CMPA    #TOK_PSET           ; PSET トークン
                BEQ     igc_fn_tok
                LDB     #FUNC_PRESET
                CMPA    #TOK_PRESET         ; PRESET トークン
                BEQ     igc_fn_tok
                LDB     #FUNC_AND
                CMPA    #TOK_AND            ; AND トークン
                BEQ     igc_fn_tok
                LDB     #FUNC_OR
                CMPA    #TOK_OR             ; OR トークン
                BEQ     igc_fn_tok
                LDB     #FUNC_XOR
                CMPA    #TOK_XOR            ; XOR トークン
                BEQ     igc_fn_tok
                JSR     igc_optnum          ; 書式指定子を除いてから数値式を評価 → D
                PSHS    A                   ; 値の上位バイト (負値の判定に使う) を退避
                LDA     gp_optn
                CMPA    #2                  ; 配列(1)・機能(2) の後の数値は色
                BHS     igc_num_color
                PULS    A
                STB     gp_func             ; 最初の数値 = 機能/対象色
                BRA     igc_opt_next
igc_num_color:
                LDA     SUBCMD_CMD
                CMPA    #$1B
                LBEQ    igc_getcol          ; GET@ は色リストへ積む (プールへ)
                PULS    A
                STB     gp_color            ; PUT@ の 2 個目以降の数値 = 色
                BRA     igc_opt_next
igc_fn_tok:
                STB     gp_func
                INC     gp_kwfn             ; 機能語あり → 形式3/4 (図形)
                LEAX    1,X                 ; キーワードトークンを消費
igc_opt_next:
                INC     gp_optn
                LBRA    igc_optlp
igc_send:
                JMP     igc_fixarr          ; 配列アドレスを再解決してから復帰

; igc_arrname: GET@/PUT@ の配列名引数を解決して U = 実体アドレスを返す。
;   `A%(5)` のような要素参照は impl_get_var_ref 経路。
;   `A%` (添字なしの配列名) は配列表 (arr_find) から要素領域先頭を引く。
;   添字なし配列名を impl_get_var_ref に渡すと同名のスカラ変数を新規作成して
;     しまい、(1) gp_arr_ptr がスカラ値を指し GET/PUT が配列外を読み書きする、
;     (2) 上方確保レイアウトではスカラ挿入で配列ブロックが移動し、直前に
;     LOADM で配列へ載せたデータが数バイトずれる、の 2 つの実害があるため。
igc_arrname:
                JSR     parse_var_name      ; VNPTR/VNLEN/VARTYP 設定, X=名前直後
                JSR     skip_sp_lparen
                BEQ     igc_an_elem
                ; 添字なし配列名: 配列エントリから要素領域先頭を求める
                PSHS    X                   ; 本文ポインタ退避 (arr_find が X を壊す)
                JSR     arr_find
                BEQ     igc_an_scal         ; 未宣言 → スカラ参照 (防御)
                LDX     arr_entry
                JSR     arr_entry_size      ; arr_ndim を設定 (X 保存)
                JSR     arr_hdrskip         ; X = len 欄先頭 (可変長名)
                LEAX    3,X                 ; ヘッダ固定部 (len2+ndim1)
                LDB     arr_ndim
                ASLB
                ABX                         ; X = 要素領域先頭
                TFR     X,U
                PULS    X,PC ; 本文ポインタ復元
igc_an_scal:
                PULS    X
                JMP     gvr_find_scalar     ; U = スカラ値アドレス (tail)
igc_an_elem:
                JMP     gvr_array           ; U = 要素アドレス (tail, 名前解析済)

; impl_get: GET@ / GET@A — 参考書籍の 4 形式を受ける入口。
;   形式1 `GET@ (x1,y1)-(x2,y2),配列名`        … キャラクタ座標。文字コードを読む
;   形式2 `GET@A(x1,y1)-(x2,y2),配列名`        … 同上 + 属性 (1 セル 2 byte)
;   形式3 `GET@ (x1,y1)-(x2,y2),配列名,G[,色]` … グラフィック座標。ドットパターン
;   形式4 `GET@A(x1,y1)-(x2,y2),配列名,G`      … 同上を 3 面ぶん
;   形式の判別と発行は本区画に残量が無いため空きプールの iget_sel が担う。
impl_get:
                LDA     #$1B
                JSR     igp_common
                JMP     iget_sel            ; 形式判別 → 発行 → 受取 (プール)

; impl_put: PUT@ / PUT@A — 配列パターンを矩形へブリット。
;   共有 RAM のパターン領域は 1 コマンドぶんしか無いため、サブシステムの手順の
;   分割転送継続コマンド ($64) で残りを流す。矩形は切らない (切ると面の先頭が
;   コマンドごとにバイト境界へ揃え直され、幅が 8 の倍数でない矩形が崩れる)。
;   本区画に残量が無いため送出本体 (iput_blk) は空き領域に置く。
impl_put:
                LDA     #$1C
                JSR     igp_common
                LDU     gp_arr_ptr
                LBNE    iput_sel            ; 配列あり → 形式判別 (プール)
                ; 配列無し → 矩形のみ 1 コマンド (防御)
                LDA     gp_color
                STA     SUBCMD_P9           ; color ($D38B)
                LDA     gp_func
                STA     SUBCMD_P10          ; function ($D38C)
                CLR     SUBCMD_P11
                JMP     impl_sub_send_cmd

;==============================================================================
; exec_body: EXEC 本体 (impl_exec から JMP される)
;   開始アドレスがあれば評価して EXEC アドレス (EXEC_ADDR $0217) に記し、EXEC アドレス
;   へ JSR する。開始アドレスを省くと、直前の LOADM の入口アドレスか直前の EXEC の
;   開始アドレスから実行する (参考書籍)。RTS で
;   戻る機械語のために、戻り位置 (後続文ポインタ) を iexec_resume へ退避し戻り時に
;   X へ載せる (ディスパッチャは handler から戻った後の X を続行位置として使うため)。
;   未設定の EXEC アドレスは Illegal Function Call の入口 (低位 RAM) を指す。
;   飛び先はそのまま JSR し、ROM/RAM モードは切り換えない (切換えは利用者の
;   機械語が $FD0F で行う。参考書籍)。
;   ※ impl_exec を短く保つため、本体をここ (空き領域) に置く。アドレスは固定しない。
;==============================================================================
impl_exec:
exec_body:
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     exb_bare
                CMPA    #':'
                BEQ     exb_bare
                ; 開始アドレスあり: 評価して EXEC アドレスへ記す。X は式直後 (=後続文位置)。
                JSR     eval_to_addr   ; アドレスなので -65536..65535
                STD     EXEC_ADDR
exb_bare:
                STX     iexec_resume
                LDX     EXEC_ADDR       ; 開始アドレス無しなら直前の LOADM / EXEC のもの
                ; 利用者機械語へ制御を渡す前に周期タイマ割込 (TIME$ tick) を止め、
                ; 復帰したら再開する。復帰しない (呼出先の本体へ移行する) 場合は
                ; 止まったままとなり、機械語側の割込運用と干渉しない。
                JSR     time_disarm
                JSR     ,X
                JSR     time_rearm
                LDX     iexec_resume
                RTS


; ieh_tail / iet_d — エラー表示の後半 (本体は 7t_deffn.inc)。

                include "7t_using_entry.inc"    ; PRINT USING 文入口 (プール)

;------------------------------------------------------------------------------
; ift_num — TIME (数値形) の本体。impl_fn_time から '$' が続かない時に入る。
;   参考書籍の TIME 関数は、内部時計をその日の 0 時からの経過秒
;   (0-86399) で返す。値域が 16 bit 整数を超えるので単精度で返す。
;   総秒 = 時*3600 + 分*60 + 秒 を 24 bit で組み立て、仮数 bit31..8 に置いた
;   前提の指数 151 を与えて impl_int_to_float の正規化ループへ合流する。
;   時*3600 は 3600 = $0E10 の分割 (時*$0E を 256 の位へ、時*$10 を 1 の位へ)
;   で 8x8 の乗算 2 回に収める。
;   TIME$ 側の区間には残余が無いため、本体はこの空きプールへ置く。
;------------------------------------------------------------------------------
ift_num:
                STX     fn_xsave
                LDA     #$3E            ; Read Timer (サブ CPU 常駐 RTC)
                JSR     sub_send_cmd_a          ; A = コマンド番号のまま発行
                LDX     #SUBCMD_P3      ; ,X=時 / 1,X=分 / 2,X=秒
                LDA     1,X
                LDB     #60
                MUL                     ; D = 分*60
                ADDB    2,X
                ADCA    #0              ; D = 分*60+秒 (<=3599)
                PSHS    D
                LDA     ,X
                LDB     #16
                MUL                     ; D = 時*16
                ADDD    ,S++            ; D = 時*16+分*60+秒 (<=3967)
                STD     FAC1+2          ; 24 bit の下位 2 byte
                CLR     FAC1+1          ; 上位 byte
                LDA     ,X
                LDB     #14
                MUL                     ; D = 時*14 (= 時*3584 を 256 の位で)
                ADDD    FAC1+1
                STD     FAC1+1          ; FAC1+1..+3 = 総秒 (24 bit)
                CLRA
                CLRB
                STD     FAC1+4          ; 仮数下位 + 符号 (正)
                STD     FAC1+6          ; ガード
                LDA     #151            ; 24 bit を bit31..8 に置いた前提の指数
                STA     FAC1
                JMP     iitf_norm       ; 正規化 → FACTYPE=1 で復帰
                                        ;   (総秒 0 は正規化ループ側が単精度ゼロへ)


;--- impl_cls_ext — CLS 本体 (置き場所は容量の都合) ----
;   CLS は画面を消し、消した面を現在の背景色にし、以後の文字を現在の文字色で
;   出す (参考書籍)。よって色を伴う画面消去 ($0D) で渡す。
;     $D383 = W  … 0 = 全画面
;     $D384 = BC … 消去後の背景色
;     $D385 = FC … 消去後の文字色
;   色を伴わない $02 で送ると消去面が常に色番号 0 になるため使わない。
impl_cls:
impl_cls_ext:
                LDD     #$0D00                  ; A = $0D (コマンド), B = 0
                STA     SUBCMD_CMD              ; ステージング SH_CMD
                STB     SUBSTG_BASE             ; ステージング SH_ERR
                STB     SUBSTG_BASE+1           ; ステージング SH_CNT
                STB     SUBCMD_P1               ; W = 0 (全画面消去)
                LDA     cur_bg_color
                STA     SUBCMD_P2               ; BC = 消去後の背景色
                JSR     eff_txt_col             ; FC = 消去後の文字色 (単色表示なら緑)
                STA     SUBCMD_P3
                JMP     impl_sub_send_cmd

;------------------------------------------------------------------------------
; iget_colors: GET@ (ドットパターン形式) の色指定を組み立てる。
;   サブシステムの矩形読出は $D38B を色数 n (1-8)、$D38C 以降を色コードの
;   リストとして読み、一致した画素のビットを 1 にする。
;   ドットパターン形式は背景以外のドットが在れば 1 (色は問わない) なので、
;   現在の背景色を除く 7 色をリストに載せる (背景色 0 を決め打ちで除くと、
;   COLOR で背景色を 0 以外にしたプログラムでは背景そのものが「一致」と判定され、
;   取り込んだビットイメージが全面 1 になって図形が潰れる)。
;   破壊: A, B, X。
;------------------------------------------------------------------------------
iget_colors:
                LDB     SUBCMD_P9               ; 書式指定子の後に明示された色数
                BNE     igcl_ret                ; 1 個以上あればそのリストを使う
                LDB     #7                      ; 既定 = 背景色を除く 7 色
                STB     SUBCMD_P9               ; $D38B = 色数
                LDX     #SUBCMD_P10             ; $D38C- = 色コードリスト
                LDA     cur_bg_color
                ANDA    #$07                    ; A = 背景色 (この 1 色だけ載せない)
igcl_lp:
                PSHS    A
                CMPB    ,S+                     ; B == 背景色 ?
                BEQ     igcl_skip
                STB     ,X+                     ; 背景色以外を順に載せる (順序は不問)
igcl_skip:
                DECB
                BPL     igcl_lp                 ; 7 → 0 を走査 (載るのは 7 色)
igcl_ret:
                RTS

;--- igc_getcol — GET@ 形式1 の対象色を 1 個ぶん色リストへ積む ----------------
;   参考書籍の GET@ 形式1 は、書式指定子 G の後に対象色を並べられる。
;   サブシステムの矩形読出は $D38B=色数 / $D38C- =色コードリストを読む。
;   属性形式 (@A) は対象色を採らない。色は 8 個まで。
;   入力: D = 色の値 (16bit 符号付き) / X = 本文ポインタ (保存する)
igc_getcol:
                PULS    A                       ; A = 値の上位バイト
                TSTA
                LBMI    errc_5                  ; 負の色 → Illegal Function Call
                LDA     gp_attr
                BNE     tgc_err                 ; 属性形式は色を採らない → Syntax Error
                LDA     SUBCMD_P9
                CMPA    #8
                BHS     tgc_err                 ; 9 個目 → Syntax Error
                LDU     #SUBCMD_P10             ; $D38C- = 色コードリスト
                LEAU    A,U
                STB     ,U
                INC     SUBCMD_P9
                JMP     igc_opt_next

                include "7t_strheap.inc"        ; 文字列領域の割付

;------------------------------------------------------------------------------
; tok_go_chk — GO の直後に TO / SUB が続くことを入力時に確かめる (プール配置)
;
;   GOTO / GOSUB は GO ($87) と TO ($CD) / SUB ($CE) の 2 トークンに分けて
;   格納する綴りである。よって GO 単独、および GO に TO / SUB 以外が続く綴りは
;   文にならない。そのような行は入力した時点で Syntax Error とし、記憶しない
;   (行番号付き・直接入力のいずれも)。GO と TO の間の空白は何個あってもよい。
;
;   入力: X = GO の直後の入力位置, U = 出力位置 (直前に $87 を書いてある)
;------------------------------------------------------------------------------
tok_go_chk:
                JSR     skip_sp_x       ; GO と TO の間の空白を読み飛ばす
                JSR     tok_match_kw    ; 続く語を命令語表で照合
                BNE     tgc_err         ; 予約語ですらない → 文にならない
                LDA     -1,U            ; 書き込んだトークン
                CMPA    #TOK_TO         ; TO
                BEQ     tgc_ok
                CMPA    #TOK_SUB        ; SUB
                BEQ     tgc_ok
tgc_err:
                JMP     errc_2          ; Syntax Error (行は記憶されない)
tgc_ok:
                JMP     tok_lp          ; 主ループへ戻る

; --- システムスタック上の枠の出し入れ (7t_defs.inc「システムスタック上の枠」) ---
; frame_ins — B = 枠の大きさ。枠列の先頭 (EXEC_SP) に B バイトの枠を開ける。
;   S〜EXEC_SP の間 (呼出元の一時とサブルーチンの復帰アドレス。本ルーチン自身の
;   復帰アドレスを含む) を B だけ下へずらし、S と EXEC_SP を B 下げる。
;   下げた後のシステム S − STK_RESERVE が STREND を割るなら Out Of Memory
;   (戻らない。検査は S も EXEC_SP も動かす前に済ませるので、足りないときは
;    1 バイトも書かない)。
;   出力: U = 枠 (= 新しい EXEC_SP)。破壊: A,B,X,U。
;   検査を通ったら S を先に下げてから写す (割込のフレームが写し先を踏まない)。
frame_ins:
                TFR     S,X             ; X = 写し元 (現在の S)
                NEGB
                SEX                     ; D = −B
                LEAU    D,X             ; U = 新しい S
                TFR     U,D
                SUBD    #STK_RESERVE
                CMPD    STREND
                BLO     fi_oom          ; 未使用領域が足りない (何も書いていない)
                TFR     U,S             ; 先に S を下げる
fi_lp:
                LDA     ,X+
                STA     ,U+
                CMPX    EXEC_SP
                BNE     fi_lp           ; U = 旧 EXEC_SP − B = 枠
                STU     EXEC_SP
                RTS
fi_oom:
                JMP     err_rg          ; Out Of Memory
; frame_del — X = 新しい枠列の先頭 (≥ EXEC_SP)。EXEC_SP から X の手前までの枠を
;   捨てる。S〜EXEC_SP の間をそのぶん上へずらし、S と EXEC_SP を上げる。
;   破壊: A,X,U (crs_oldtop を作業に使う)。
frame_del:
                STS     crs_oldtop      ; 写しの下限 (現在の S)
                LDU     EXEC_SP         ; 写し元の上端 (排他)
                STX     EXEC_SP
fd_lp:
                LDA     ,-U
                STA     ,-X
                CMPU    crs_oldtop
                BNE     fd_lp
                TFR     X,S
                RTS
; stk_find — 枠列 (EXEC_SP〜STRBOT) を先頭から辿り、種別 B の枠を探す。
;   サブルーチンの枠 (TAG_GOSUB) は越えない (B がそれ自身なら見つかった扱い)。
;   出力: Z=1 で X = 枠。Z=0 = 無い。破壊: A,X (B は保存)。
stk_find:
                LDX     EXEC_SP
sf_lp:
                CMPX    STRBOT
                BHS     sf_no
                CMPB    ,X
                BEQ     sf_ret          ; 見つかった (Z=1)
                LDA     ,X
                CMPA    #TAG_GOSUB
                BEQ     sf_no           ; サブルーチンの枠は越えない
                BMI     sf_dir          ; $FF = 直接モードの行
                CMPA    #TAG_FOR
                BNE     sf_5
                LEAX    FRM_FOR-FRM_SMALL,X
sf_5:
                LEAX    FRM_SMALL,X
                BRA     sf_lp
sf_dir:
                LDA     1,X             ; 原文の長さ n
                LEAX    5,X
                EXG     A,B
                ABX
                EXG     A,B
                BRA     sf_lp
sf_no:
                ANDCC   #$FB            ; Z=0
sf_ret:
                RTS
; dr_drop_top — 枠列の先頭が直接モードの行の擬似レコードなら、それを降ろす
;   (EXEC_SP を上げる。S は呼出元が LDS EXEC_SP で揃える)。変換済み行の控えも
;   無効化する (次の直接モードの行が同じ位置に載ると内容が別物になるため)。
;   破壊: A,B,X。
dr_drop_top:
                LDX     EXEC_SP
                CMPX    STRBOT
                BHS     ddt_ret
                LDA     ,X
                BPL     ddt_ret         ; 直接モードの行ではない
                LDB     1,X             ; 原文の長さ n
                ABX
                LEAX    5,X
                STX     EXEC_SP
                JSR     clr_execrec
ddt_ret:
                RTS
; chk_stack — 式の入れ子 (括弧・関数・FN の再帰) の 1 段ごとに、システムスタック
;   の空きを確かめる。S − STK_RESERVE が STREND を割るなら Out Of Memory。
;   破壊: D (CC)。
chk_stack:
                TFR     S,D
                SUBD    #STK_RESERVE
                CMPD    STREND
                BLO     fi_oom
                RTS

;------------------------------------------------------------------------------
; con_init_send — コンソールイニシャライズ ($01) の送出 (WIDTH / CONSOLE 共通)
;   入力: A = スクロール範囲の上端行, B = スクロール範囲の行数
;         con_cols / con_rows = 現在の桁数・行数 (呼出側が最新にしておく)
;   桁数・行数は「現在値」をそのまま添える。桁数はサブシステム側にしか無く
;   メイン CPU から読めないので、控えを持たずに固定値を送ると 40 桁で走行中の
;   範囲指定で桁数まで書き換わってしまう。
;   消去指定は 1 (画面消去 + カーソルは範囲左上へ)。相対 8 (FD) は機能キー行の
;   表示指定、相対 10 (GR) は単色表示の指定で、いずれも CONSOLE の第 3・第 4 引数の
;   現在値を添える (参考書籍)。
;   文字色は送出後に SF オーダ ($11) で改めて送る (ic12_send)。
;   壊すレジスタ: A,B (X,Y,U は送出側が保全する)
;------------------------------------------------------------------------------
;   ステージングは連続アドレスなので、隣り合う 2 つのパラメータは 16 ビットで
;   まとめて置く (この区画の空きが乏しいため)。
con_init_send:
                STD     SUBCMD_P4               ; A=範囲上端 → P4 / B=範囲行数 → P5
                STD     con_wtop                ; 開始行を控える (B は終了行の位置に一旦入る)
                ADDA    con_wend                ; 終了行 = 開始行 + 行数 - 1
                DECA                            ;   (参考書籍)
                STA     con_wend
                LDD     #$0100                  ; A=$01 コマンド / B=0 背景色
                STD     SUBCMD_CMD              ; → SUBCMD_CMD / SUBCMD_P1
                LDA     con_cols                ; 桁数 (現在値)
                LDB     con_rows                ; 行数 (現在値)
                STD     SUBCMD_P2               ; → SUBCMD_P2 / SUBCMD_P3
                LDA     con_pf                  ; A=機能キー行の表示指定 (現在値)
                LDB     #1                      ; B=1 消去指定
                STD     SUBCMD_P6               ; → SUBCMD_P6 / SUBCMD_P7
                LDA     con_mono
                STA     SUBCMD_P8               ; 相対 10 = GR (単色表示。参考書籍)
                JSR     impl_sub_send_cmd
                ; 画面表示の初期化はコンソール制御 ($0C) の指定も伴う。カーソル表示・
                ;   オーダ動作・TAB 動作・ページウェイト・プットウェイトを指定し、
                ;   オート LF は指定しない (参考書籍)。サブシステムのリセット時の
                ;   既定 (CF = $23) とは異なるので、控え disp_state をそのまま送る。
                JSR     cursor_on               ; 控えをそのまま CNSCTL で発行する
                CLR     PRINT_COL
                RTS

;------------------------------------------------------------------------------
; tab_set_init — TAB 停止位置の設定 (コールドスタート。停止位置の表は tab_map ($02A2〜$02AB))
;   表を 8 桁ごと (各バイトの bit7 = 桁 0, 8, 16, …, 72) にし、その 10 バイトを
;   TAB SET ($0B) の相対 3〜12 へ写して送る
;     (参考書籍)。
;   壊すレジスタ: A,B,X (U は送出側が保全する)
;------------------------------------------------------------------------------
tab_set_init:
                LDX     #tab_map
                LDD     #$800A                  ; A: bit7 = 8 桁ごとの先頭の桁 / B: 10 個
tsi_lp:
                STA     ,X+                     ; 表へ置き、同じ値を相対 3〜12 へも写す
                STA     SUBCMD_P1-tab_map-1,X   ;   (表と同じ並び。X は 1 つ進んでいる)
                DECB
                BNE     tsi_lp
                LDA     #$0B                    ; TAB SET
                JMP     sub_send_cmd_a


                ; 範囲をコンソールへ伝える (プール) (ここへ展開)
;------------------------------------------------------------------------------
; icon_apply — CONSOLE の値域検査を通った後の適用 (impl_console_ext から JMP)
;   入口の枠 (impl_console_ext が積んだまま): 0,S=上限 2,S=単色 4,S=機能キー
;                                             6,S=行数 8,S=開始行
;   スクロール範囲 (開始行・行数) と、機能キー行表示・単色表示の指定を
;   コンソールへ伝える (機能キー行表示は控え con_pf を con_init_send が相対 8 として
;   送る。単色表示は控え con_mono をコンソールイニシャライズの相対 10 で伝える)。
;   コンソールイニシャライズは消去を伴い、文字色は既定へ戻るので、送出の後に
;   COLOR で指定した文字色を送り直す (impl_color12 の末尾と共通)。
;------------------------------------------------------------------------------
icon_apply:
                LDA     5,S                     ; 機能キー行表示 (下位 byte。0..1)
                STA     con_pf
                LDA     3,S                     ; 単色表示 (下位 byte。0..255)
                STA     con_mono
                LDA     9,S                     ; 開始行 (下位 byte。値域検査済)
                LDB     7,S                     ; 範囲行数 (同上)
                LEAS    10,S
                BSR     con_init_send           ; 送出は WIDTH と共通
                JMP     ic12_send               ; 文字色を送り直す (単色表示の切替を反映)

;------------------------------------------------------------------------------
; bdf_err — Bad Data In File (58) で読取/併合を中止する共通口。
;   ヘッダのファイル形式・属性が読み取れる形でないファイルの LOAD / MERGE は
;   58 Bad Data In File とする (51 Bad File Mode はオープンした向きと逆の
;   入出力に使い、ファイルの形式の判定には用いない)。
;   テープ/ディスクのどちらのモータも止める (使っていない側の停止は無害。
;   ここへ来る経路は BASIC プログラムの読取・併合のみで、FDC へ触れない
;   決まりは LOADM 系の尾部 (ilm_done) にだけ掛かる)。
;------------------------------------------------------------------------------
bdf_err:
                JSR     cmt_motor_off
                CLR     FDC_DRV
                LDB     #58                     ; Bad Data In File (参考書籍)
                JMP     impl_error_handler

;------------------------------------------------------------------------------
; cmt_name_chk — 指定ファイル名 (cmt_filename) とヘッダ名 (cmt_hdr_buf 先頭 8B)
;   を照合する。指定が空 ($00 先頭) は無条件一致。指定側の終端 $00 以降は
;   前方一致 (LOAD/LOADM/MERGE の 3 か所で共用)。
;   出力: C=0 一致 / C=1 不一致。破壊: A,B,X,U
;------------------------------------------------------------------------------
cmt_name_chk:
                LDX     #cmt_filename
                LDU     #cmt_hdr_buf
                LDB     #8
cnc_lp:
                LDA     ,X+
                BEQ     cnc_ok                  ; 指定側終端 (先頭 $00 = 指定なし含む)
                CMPA    ,U+
                BNE     cnc_no
                DECB
                BNE     cnc_lp
cnc_ok:
                CLRA                            ; C=0 一致 (A は破壊してよい)
                RTS
cnc_no:
                COMA                            ; C=1 不一致 (COM は必ず C=1)
                RTS

;------------------------------------------------------------------------------
; crs_fix_recs — CLEAR n,m のスタック移設後に、直接モードの行の擬似レコード
;   (システムスタック上に在る) を指す常駐ポインタを移動量で補正する。移設の
;   複写範囲 [S..STRBOT) には擬似レコードも含まれるため内容は既に新位置にあり、
;   ポインタの値だけが旧位置のまま残る。プログラム本文の行レコード (VARTAB 未満)
;   と 0 (未設定) は対象外。実行用行バッファ (EXECBUF) は固定なので、その中を
;   指すポインタ (CURIP / 枠の IP / ERRIP / DATPTR) に補正は要らない。
;   対象: CURLINE_PTR / EXECREC / DATLINE と、枠列の各枠の行レコード欄。
;   前提: crs_oldtop = 旧 STRBOT, crs_new = 移動量, EXEC_SP は新値。破壊: A,B,D,X。
;------------------------------------------------------------------------------
crs_fix_recs:
                LDX     #CURLINE_PTR
                BSR     cfr_1
                LDX     #EXECREC
                BSR     cfr_1
                LDX     #DATLINE
                BSR     cfr_1
                LDX     EXEC_SP
cfr_lp:
                CMPX    STRBOT
                BHS     cfr_ret
                LDA     ,X
                BMI     cfr_dir
                CMPA    #TAG_FOR
                BNE     cfr_5
                LEAX    FRM_FOR-FRM_SMALL,X
cfr_5:
                LEAX    3,X             ; 行レコード欄 (GOSUB / WHILE +3、FOR +13)
                BSR     cfr_1
                LEAX    2,X
                BRA     cfr_lp
cfr_dir:
                LDB     1,X
                ABX
                LEAX    5,X
                BRA     cfr_lp
;   cfr_1 — [X] の 16bit 値が VARTAB 以上 旧 STRBOT 未満 (= 移設した範囲) なら
;   移動量を足して書き戻す。破壊: D。
cfr_1:
                LDD     ,X
                CMPD    VARTAB
                BLO     cfr_ret
                CMPD    crs_oldtop
                BHS     cfr_ret
                ADDD    crs_new
                STD     ,X
cfr_ret:
                RTS



;------------------------------------------------------------------------------
; pool_fn_point_rd — POINT(x,y) の読み取りと判定。
;
;   POINT 関数の返り値はその点が表示されているかどうかで
;   あり、**現在の背景色と異なる色で描かれていれば -1、背景色と同じなら 0**
;   である (「色 0 でないか」ではないので、背景色を 0 以外にしたプログラムでも
;   真偽は反転しない)。
;
;   判定はサブシステム $1B (Get Block 1) に色リスト = 現在の背景色 1 個だけを
;   与え、1x1 の矩形を読むことで得る。応答の先頭バイトの bit7 が「その点は
;   指定色 (= 背景色) と一致した」を表すので、bit7=1 なら 0、0 なら -1 を返す。
;   3 プレーンを別々に読む $1D と違い走査は 1 周で済む。
;
;   境界の扱い:
;     垂直位置が 0-255 の外  → Illegal Function Call
;     水平位置                → 上限判定なし (アドレス計算のまま折り返す)
;
;   入力: スタックに Y 座標 (上) と X 座標 (下) が 16bit で積まれている。
;         本文ポインタは呼出側で fn_xsave へ退避済み。
;   直前は fixed_8F39 の JMP なので、この定義へ落ち込む経路は無い。
;------------------------------------------------------------------------------
pool_fn_point_rd:
                LDA     #$1B                    ; Get Block 1 (指定色一致で 1bit)
                STA     SUBCMD_CMD
                PULS    D                       ; Y 座標
                TSTA
                BNE     pia_ifc                 ; 0-255 の外 → Illegal Function Call
                STD     SUBCMD_P3               ; Y1 ($D385-86)
                STD     SUBCMD_P7               ; Y2 ($D389-8A)
                PULS    D                       ; X 座標
                STD     SUBCMD_P1               ; X1 ($D383-84)
                STD     SUBCMD_P5               ; X2 ($D387-88)
                LDA     #1
                STA     SUBCMD_P9               ; 色リストの個数 = 1
                LDA     cur_bg_color
                ANDA    #$07                    ; 色コードは下位 3 bit
                STA     SUBCMD_P10              ; 照合する色 = 現在の背景色
                JSR     impl_sub_send_cmd
                LDA     SUBCMD_P2               ; 応答先頭バイト (bit7 = 背景色一致)
                BMI     pfpr_bg                 ; 一致 → 表示されていない
pfpr_m1:
                JMP     int_to_fac_m1
pfpr_bg:
                JMP     int_to_fac_zero         ; FAC1 = 0

;==============================================================================
; pool_instr_arg1 — INSTR の第1引数を分類し検索開始オフセットを決める
;   (impl_fn_instr から JSR。本体は空きプールに置く。直前は JMP なので
;    落ち込み経路はない)
;
;   入力: 第1引数を評価済み (FACTYPE/STRPTR/STRLEN), X = 第1引数の直後
;   出力: B = 検索開始オフセット (0 起点), STRPTR/STRLEN = haystack$,
;         X = haystack$ の直後 (呼出側が ',' を消費する)
;   第1引数が文字列 → 2 引数形 (開始位置 1)。数値 → 3 引数形として
;   ',' と haystack$ を続けて評価する。開始位置 0 以下・256 以上は
;   Illegal Function Call (参考書籍)。
;==============================================================================
pool_instr_arg1:
                JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
                BEQ     pia_2arg
                JSR     impl_fac_to_int         ; D = 開始位置 (1 起点)
                TSTB
                BEQ     pia_ifc                 ; 0 → Illegal Function Call
                TSTA
                BNE     pia_ifc                 ; 負 / 256 以上 → Illegal Function Call
                PSHS    B                       ; 開始位置を退避
                JSR     eval_x_str_ac
                PULS    B
                DECB                            ; 0 起点オフセットへ
                RTS
pia_2arg:
                CLRB                            ; 開始位置 1 → オフセット 0
                RTS
pia_ifc:
                JMP     errc_5         ; Illegal Function Call

;==============================================================================
; pool_fn_sgn — SGN(x) の本体
;   (impl_fn_sgn から JMP。本体は空きプールに置く。直前は JMP なので
;    落ち込み経路はない)
;
;   入力: FAC1 = 評価済みの引数。出力: FAC1 = 1 / 0 / -1 (整数型)。
;   浮動小数のときは引数を 16bit 整数へ切り捨てず、パック形式の指数バイト
;     (0 = 値ゼロ) と符号ビットで直接判定する (切り捨てると |x|<1 の小数が
;     値ゼロと判定されるため)。
;==============================================================================
pool_fn_sgn:
                JSR     fx_ensure       ; 整数/倍精度も単精度へ揃える
                LDA     FAC1            ; 指数 0 = 値ゼロ
                BEQ     psg_zero
                LDA     FAC1+5          ; 符号バイト (bit7)
                BMI     pfpr_m1
                LDD     #1
                JMP     impl_int_to_fac

psg_zero:
                CLRA
                CLRB
                JMP     impl_int_to_fac

;------------------------------------------------------------------------------
; 連携サービス入口 — 数字文字の検査/搬送 (行取得サブの分類と対の入口)
;   入力 A=文字。復帰: C=0 なら '0'-'9' (A は保存)、C=1 なら数字でない。
;------------------------------------------------------------------------------
                include "7t_using_exp1.inc"     ; PRINT USING 指数形式 書式検出 (プール)
                include "7t_arr_ndim.inc"       ; 配列の多次元機構 (プール)

;------------------------------------------------------------------------------
; cir_angle — CIRCLE の開始/終了位置引数 1 個を頂点番号 B (0-63) へ変換する
;   (impl_circle から JSR。置き場所は容量の都合。
;    参照はラベル経由のみで位置依存なし)
;   省略・空・行末は 0 (参考書籍)。値は ×64 (指数 +6) して最近接丸め
;   (0.5 はゼロから遠い側)。値域 0〜1 は単精度変換前に検査する。
;   検査後の ×64 は 0〜64 なので、指数あふれと整数上位の再検査は不要。
;   直前は JMP で終わるため、この定義へ落ち込む経路は無い。
;------------------------------------------------------------------------------
cir_angle:
                JSR     skip_comma
                BCC     can_zero
                JSR     skip_sp_comma
                BEQ     can_zero            ; 空 → 0
                CMPA    #':'
                BEQ     can_zero
                TSTA
                BEQ     can_zero            ; 行末 → 0
                JSR     impl_eval_expr
                JSR     circle_unit_ensure
                LDA     FAC1                ; 指数 0 = 値 0
                BEQ     can_zero
                ADDA    #6                  ; ×64 (指数へ +6)
                STA     FAC1
                JSR     impl_fac_to_int_round   ; D = round(値×64)
                ANDB    #63                 ; 頂点リングへ折返し (動作観察)
                RTS
can_zero:
                CLRB
                RTS

; (impl_fn_pos は 7t_core.s の末尾プールに置く。参照はラベル経由のみ)

;------------------------------------------------------------------------------
; ssc_waitb — 非 $03 コマンドの完了待ち前段。impl_sub_send_cmd から。
;   「64 歩空回り → HALT → SH_CMD 読み → RUN」を完了まで反復すると、HALT の
;   たびにサブ CPU 側の処理そのものが止まり、Put Block の継続 ($64) や GET の分割
;   応答のような長いコマンドほど待ちが自乗的に膨らむ。そこで次の手順を採る:
;     1. BUSY ($FD05 bit7) の立ち上がりを有界 (256 回) で待つ。サブはコマンド
;        受理時に BUSY をセットするので、通常はすぐ観測できる。観測前に上限へ
;        達した場合 (サブが受理前 / 既に処理完了済) は HALT 検証へ委ねる
;     2. 立ち上がりを見たら、降りるまで $FD05 のみで待つ。HALT を掛けないので
;        サブは全速で処理を進める。サブは応答完了 (SH_CMD クリア) の後に待機
;        ループ先頭で BUSY を降ろすため、BUSY=0 なら SH_CMD=0 でもある
;     3. いずれの経路も最後は ssc_poll (HALT 下で SH_CMD=0 を確認) へ
;        合流する。完了判定の根拠・読み戻し・ATN 手順はこの 1 本に集約
;   利用者の機械語がサブ CPU を占有して BUSY が立ったままのときは 2 で待ち続ける。
;   B (完了ポーリング残回数 = 0, 上限なし) は温存する。壊す: A, X。
;   直前は gc_fin の PULS ..,PC なので、この定義へ落ち込む経路は無い。
;------------------------------------------------------------------------------
ssc_waitb:
                ; 立ち上がり待ちの上限 256 周は B で数える (入口の B は 0 で、
                ; 256 回減じると 0 に戻るので、合流先 ssc_poll が求める
                ; 「B=0 = 完了待ちの上限なし」をどちらの出口でも満たす)。
sscw_rise:
                LDA     PORT_SUBCPU         ; $FD05 bit7 = BUSY
                BMI     sscw_fall           ; 立った → 降り待ちへ
                DECB
                BNE     sscw_rise
                BRA     sscw_go             ; 未観測 (受理前 or 完了済) → HALT 検証
sscw_fall:
                ; BUSY の間サブは全速走行 (HALT なし)。行入力 ($04) はここで
                ; 1 行が確定するまで待ち続けるので、中断操作はこの周回で
                ; 受け付ける (押下かつ走行中ならキャンセル要求をサブ CPU 側へ送る)。
                JSR     ssc_brk_cancel
                LDA     PORT_SUBCPU
                BMI     sscw_fall
sscw_go:
                JMP     ssc_poll            ; 降りた → HALT 下で SH_CMD=0 を確認

;------------------------------------------------------------------------------
; fmul_bmul — 32bit×32bit → 64bit 積を MUL 命令の部分積で作る。
;   fmul_core から JSR。乗数バイトごとに被乗数×バイトの 40bit 部分積を加算する
;   教科書式 (最大 16 回の MUL) で 64bit 積を作る。
;
;   入力:  fm_plo = 乗数 M1 (4B 上位から)、fm_m2 = 被乗数 M2 (4B 上位から)。
;   出力:  [fm_phi:fm_plo] = M1×M2 の 64bit 厳密値
;          (丸め・正規化は fmul_core 側で行う)。
;   壊す:  A, B, fm_cnt (乗数バイトの一時)。X/Y/U は退避・復元する。
;
;   バイト配置: P0..P7 = fm_phi+0..fm_phi+3, fm_plo+0..fm_plo+3 (連続 8B)。
;   乗数バイト v_j (j=0..3, 上位から) の部分積 M2×v_j は P[j]..P[j+4] に載る。
;   P[j+4] は v_j 自身の格納庫で、v_j を読み終えた後に部分積の最下位バイトを
;   新規格納する (読み順を上位 j=0 から下位へ取ると、消費済みバイトの枠を
;   ちょうど次の格納庫として使い回せる)。v_j=0 の部分積は丸ごと省略できる:
;   加算値が全零で、P[j+4] に残る v_j=0 がそのまま正しい最下位バイトになる。
;   桁上げは各段で上位へ INC 連鎖 (積全体は 2^64 未満なので P0 で必ず止まる)。
;
;   4 段 × 4 バイトの 16 通りを展開せず、段を X (格納庫の位置)、被乗数の
;   バイトを U で回す二重ループにしてある。展開形は同じ並びを 16 回書き写す
;   ぶん ROM を大きく使うので、参考書籍が定める言語機能を削らずに容量を作るため
;   ループ形を採る。
;------------------------------------------------------------------------------
fmp_p           EQU     fm_phi          ; P0..P7 = fm_phi:fm_plo の連続 8B

fmul_bmul:
                PSHS    X,Y,U
                CLR     fmp_p+0
                CLR     fmp_p+1
                CLR     fmp_p+2
                CLR     fmp_p+3
                LDX     #fmp_p+4        ; X → v_j の格納庫 (= 部分積の最下位バイト)
fbm_stage:
                LDB     ,X              ; v_j
                BEQ     fbm_next        ; 0 の段は丸ごと省略 (P[j+4] は 0 のまま正しい)
                STB     fm_cnt
                LDU     #fm_m2+3        ; 被乗数は最下位バイトから
                LDA     ,U
                MUL
                STB     ,X              ; 部分積の最下位バイトを新規格納
                LEAY    -1,X            ; Y → 8bit 加算先 P[j+3]
                ADDA    ,Y
                STA     ,Y
                BCC     fbm_wlp
                BSR     fbm_carry       ; 桁上げ連鎖 (Y-1 から上位へ)
fbm_wlp:
                ; 残る 3 バイトは 16bit 加算 (加算先を 1 バイトずつ上位へ送る)
                LEAU    -1,U
                LDA     ,U
                LDB     fm_cnt
                MUL
                LEAY    -1,Y
                ADDD    ,Y
                STD     ,Y
                BCC     fbm_wnc
                BSR     fbm_carry
fbm_wnc:
                CMPU    #fm_m2          ; 被乗数の最上位バイトまで済んだか
                BNE     fbm_wlp
fbm_next:
                LEAX    1,X
                CMPX    #fmp_p+8        ; 乗数 4 バイトぶん回したか
                BNE     fbm_stage
                PULS    X,Y,U,PC

; fbm_carry — 桁上げを Y-1 から上位へ伝える。積は 2^64 未満なので P0 で必ず
;   止まる (P0 が繰り上がることはない)。Y は呼出時の値のまま返す。
fbm_carry:
                PSHS    Y
                LEAY    -1,Y
fbm_clp:
                INC     ,Y
                BNE     fbm_cdone
                LEAY    -1,Y
                BRA     fbm_clp
fbm_cdone:
                PULS    Y,PC

; (clear_body は容量の都合で 7t_cmt_io.inc の空きプールに置く。参照はラベル
;  経由のみで位置依存なし)

;------------------------------------------------------------------------------
; ic_setbot — D = 文字列領域の大きさ。新しい下限 (MEMTOP − 大きさ) を求め、
;   変わっていればシステムスタック (枠列を含む) を移設してから領域を初期化する。
;   破壊: A,B,X,U (S は張り替わり得る)。
;------------------------------------------------------------------------------
ic_setbot:
                STD     crs_new                 ; 大きさ (移設前なので crs_new は空き)
                LDD     MEMTOP
                SUBD    crs_new
                CMPD    STRBOT
                BEQ     ic_sb_same              ; 変わらないなら移設不要
                LDX     STRBOT
                STX     crs_oldtop              ; 旧スタック上端
                STD     STRBOT                  ; 新しい文字列領域の下限 (= システム
                                                ;   スタックの上端)
                JSR     clear_relocate_stack
                JSR     crs_fix_recs            ; 擬似レコードを指すポインタの補正
ic_sb_same:
                JMP     str_heap_reset          ; 文字列領域の初期化


;==============================================================================
; impl_console_ext — CONSOLE [開始行][,行数][,機能キー表示][,単色表示]
;   (impl_console と同じ入口。置き場所は容量の都合。参照は当該 JMP だけで
;    位置依存はない)
;
;   CONSOLE 文はテキスト描画のスクロール領域 (開始行・
;   行数)、機能キー行の表示/非表示、単色表示の指定を行う。省略したパラメータは
;   現在の設定を保つ。
;
;   実装方針: スクロール領域 (第1・第2引数) を実装する。値域検査を通した
;     開始行と行数を、桁数・行数の現在値を添えてコンソールイニシャライズ
;     ($01) で伝える (icon_apply → con_init_send)。以後の文字出力は
;     その範囲の中だけに出て、範囲の最下行を越えると範囲の中だけがスクロール
;     する。桁数の現在値はサブシステム側の作業域にしか無くメイン CPU から読めない
;     ため、BASIC 側の控え (con_cols) を WIDTH と共に保守して添える
;     (固定値を送ると、範囲指定で桁数まで変わってしまうため)。
;     範囲指定は画面消去を伴い、カーソルは範囲の左上へ移る (動作観察)。
;     機能キー行の表示指定 (第3引数) は控え con_pf に置き、コンソールイニシャライズの
;       相対 8 で伝える。単色表示 (第4引数) は控え con_mono に置き、同じコマンドの
;       相対 10 = GR で伝える (緑だけで描くのはサブシステム側が行う)。
;     引数は式として評価するだけでなく整数化と値域検査を通す。整数の範囲を
;       外れた値 (32768 以上 / -32769 以下) も誤りにする。
;
;   値域 (いずれも整数化の後に検査し、外れたら Illegal Function Call):
;     上限 L = 現在のテキスト行数 con_rows (機能キー表示 = 1 のときは L-2)。
;     con_rows は起動時 CON_ROWS で、WIDTH の行数指定に追随する
;     (行数を省いた WIDTH では上限は動かない。動作観察)
;     開始行 0..L-1 / 行数 0..L / 開始行+行数 <= L / 機能キー表示 0..1 /
;     単色表示 0..255。引数が 5 個以上あれば Syntax Error。
;   整数化は「整数を要求する文脈」の規約 (最近接丸め、整数の範囲外は Overflow)
;   で、4 個すべてを評価し終えてから値域を検査する
;   (値域誤りより Overflow が先に出る。動作観察)。
;==============================================================================
;------------------------------------------------------------------------------
; icon_arg — 引数を 1 個読む。入力 D = 省略時の既定値 / X = 本文ポインタ。
;   出力 D = 値、X = 次の引数の先頭 (区切りの ',' は消費済)。
;   (BSR が届くよう本体の直前に置く。末尾は PULS D,PC で復帰する)
;------------------------------------------------------------------------------
icon_arg:
                PSHS    D                       ; 既定値
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     icon_ag_end             ; 行末 → 既定のまま
                CMPA    #':'
                BEQ     icon_ag_end             ; 文区切り → 既定のまま
                CMPA    #','
                BEQ     icon_ag_com             ; 空の位置指定 → 既定のまま
                JSR     eval_to_int             ; 整数の規約 (範囲外は Overflow)
                STD     ,S                      ; 既定値を評価値で置き換える
                JSR     skip_sp_comma
                BNE     icon_ag_end
icon_ag_com:
                LEAX    1,X                     ; 区切りの ',' を消費
icon_ag_end:
                PULS    D,PC

impl_console_ext:
                CLRA
                LDB     con_wtop
                BSR     icon_arg                ; 開始行 (既定 = 現在の上端行)
                PSHS    D
                CLRA
                LDB     con_wend                ; 行数 = 終了行 + 1 - 開始行
                INCB
                SUBB    con_wtop
                BSR     icon_arg                ; 行数 (既定 = 現在の行数)
                PSHS    D
                CLRA
                CLRB
                BSR     icon_arg                ; 機能キー表示 (既定 = 出さない)
                PSHS    D
                CLRA
                CLRB
                BSR     icon_arg                ; 単色表示 (既定 = 0)
                PSHS    D
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     icon_chk
                CMPA    #':'
                BEQ     icon_chk
                JMP     errc_2                  ; 引数が 5 個以上 → Syntax Error
icon_chk:
                ; 枠: 0,S=単色 2,S=機能キー 4,S=行数 6,S=開始行
                LDD     2,S                     ; 機能キー表示
                LDA     con_rows                ; 上限 = 現在のテキスト行数
                TSTB
                BEQ     icon_lim
                SUBA    #2                      ; 機能キー行を出すと表示範囲が 2 行狭まる
icon_lim:
                TFR     A,B
                CLRA
                PSHS    D                       ; 0,S = 上限 L (以降 枠が 2 ずれる)
                LDD     4,S                     ; 機能キー表示
                CMPD    #1
                BHI     icon_r5                 ; 0..1
                LDD     2,S                     ; 単色表示
                CMPD    #255
                BHI     icon_r5                 ; 0..255
                LDD     8,S                     ; 開始行
                CMPD    ,S
                BHS     icon_r5                 ; 0..L-1 (負値も符号なしで捕まる)
                LDD     6,S                     ; 行数
                CMPD    ,S
                BHI     icon_r5                 ; 0..L
                ADDD    8,S                     ; 開始行 + 行数 (どちらも L 以下ゆえ桁あふれなし)
                CMPD    ,S
                BHI     icon_r5                 ; 合計が表示範囲に収まること
                JMP     icon_apply
icon_r5:
                JMP     errc_5                  ; Illegal Function Call


;------------------------------------------------------------------------------
; iput_sel / icput_blk — PUT@ / PUT@A の形式判別と、形式1/2 (キャラクタ矩形の
;   書込) の送出。impl_put の区画には残量が無いため本プールへ置く。
;
;   参考書籍の PUT@ は、2 番目の引数が機能語 (PSET/PRESET/AND/OR/XOR)
;   なら形式3/4 (図形の書込)、機能語でなければ形式1/2 (キャラクタの書込) である。
;   形式1 の 2 番目の引数は表示色 (パレットコード) で、省略時は直前の COLOR の
;   文字色を採る。配列にはキャラクタコード (形式1) / 属性+キャラクタコード
;   (形式2) が 1 セルあたり 1 byte / 2 byte で並ぶ。
;
;   サブ CPU 側のキャラクタ矩形書込は $07 (形式1) / $09 (形式2) で、
;     $D383-$D386 = 矩形 (桁,行)  /  $D387 = 形式1 の固定属性 (表示色)
;     $D388       = 当該チャンクのデータ計数、データ本体は $D389 から
;   データ領域に入りきらないぶんは図形形式と同じ継続コマンド ($64、$D383=計数 /
;   $D384〜=データ) で流すので、送出そのものは ipb_xfer / ipb_more と共通にできる。
;------------------------------------------------------------------------------
ICP_FIRST       EQU     119                 ; $07/$09 のデータ領域 ($D389-$D3FF)
iput_sel:
                LDA     gp_kwfn             ; 2 番目の引数が機能語だったか
                BEQ     icput_blk           ; 機能語なし → 形式1/2 (キャラクタ)
                JMP     iput_blk            ; 機能語あり → 形式3/4 (図形)
icput_blk:
                PSHS    X                   ; 本文ポインタ保護 (送出で X を使う)
                LDA     #$07                ; 形式1 = キャラクタのみ
                LDB     gp_attr
                BEQ     icp_cmd
                LDA     #$09                ; 形式2 = 属性 + キャラクタ
icp_cmd:
                JSR     sub_stage_x0        ; SH_CMD=A, SH_ERR/SH_CNT=0
                JSR     igc_chr_rect        ; 矩形をキャラクタ座標の並びへ組み直す
                ; --- セル数 = 桁数 × 行数 (逆順の指定も採るので絶対値で数える) ---
                LDA     SUBCMD_P4
                SUBA    SUBCMD_P2
                BCC     icp_h
                NEGA
icp_h:
                INCA                        ; 行数
                TFR     A,B
                LDA     SUBCMD_P3
                SUBA    SUBCMD_P1
                BCC     icp_w
                NEGA
icp_w:
                INCA                        ; 桁数
                MUL                         ; D = セル数 (最大 80x25)
                TST     gp_attr
                BEQ     icp_n1
                ASLB                        ; 形式2 は 1 セル 2 byte
                ROLA
icp_n1:
                STD     gp_bits             ; 送出総バイト数
                LDA     gp_color            ; 既定 = 現在の文字色 (COLOR の指定)
                STA     SUBCMD_P5           ; $D387 = 形式1 の固定属性
                LDX     #SUBCMD_P6          ; 計数欄 $D388、データは $D389 から
                LDB     #ICP_FIRST
                JSR     ipb_xfer
                JMP     ipb_more            ; 残りは図形形式と共通の継続送出へ

;------------------------------------------------------------------------------
; dsv_wsec — 1 セクタ書込み + 失敗判定 (ディスク SAVE 用。本体は 7t_diskboot.inc、
;   置き場所は容量の都合)。
;   入力: A = セクタ番号, Y = 転送元。失敗は Device I/O Error で中止 (戻らない)。
;------------------------------------------------------------------------------
dsv_wsec:
                JSR     disk_write_sector
                BNE     dsv_ioerr       ; 完了状態のエラービット ≠ 0
                RTS
dsv_ioerr:
                LDB     #53             ; Device I/O Error (参考書籍)
dsv_errb:
                CLR     FDC_DRV         ; モータ OFF
                JMP     impl_error_handler
