; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 7t_time.inc — TIME$ 実時計 (サブ CPU 常駐 RTC を Read/Set Timer 命令で参照)
;
; TIME$ の読出しと代入:
;   読出し: TIME$        → "HH:MM:SS" (24時間制) の 8 文字列
;   代入  : TIME$="HH:MM:SS" → 内部時計を設定 (形式不正は Illegal Function Call)
;
; 実装方式:
;   TIME$ の計時はサブ CPU 側の常駐 RTC (SET TIMER / READ TIMER) を用いる。
;   - 読出し: Read Timer 命令 ($3E) を impl_sub_send_cmd 経由で発行し、返却された
;     時/分/秒 (SUBCMD_P3/P4/P5) を "HH:MM:SS" へ整形する。
;   - 代入  : Set Timer 命令 ($3D) を発行し、検証済みの時/分/秒を RTC へ設定する。
;   メイン CPU の周期タイマ割込 (IRQ) は、PLAY 文の割込演奏 (7t_play.inc) の tick と
;     INTERVAL の計数に使う。時刻計数はここでは行わない。
;   - タイマ割込ハンドラは低位 RAM (TICK_ENTRY) に常駐させ、IRQ 入口 IRQ_HOOK
;     ($01DD) から JMP する。位置独立 (相対分岐 + 絶対参照のみ) を厳守。
;   - タイマ割込の許可は $FD02 bit2。$FD02 は書込専用のため設定値を fd02_shadow
;     に保持し、一時停止/再開/停止/再有効化はシャドウ経由で行う (PLAY tick 保護)。
;==============================================================================

;------------------------------------------------------------------------------
; tick_irq_src: タイマ割込ハンドラの複写元 (time_init が TICK_ENTRY へ複写)
;   6809 の IRQ は全レジスタを退避し RTI で復元するため、レジスタは自由に
;   使える。$FD03 の読出しがタイマ要因フラグのクリアを兼ねる。
;   位置独立 (相対分岐 + 絶対 RAM/I-O 参照のみ) を厳守すること。
;   TIME$ の計時はサブ CPU の RTC が担う。本ハンドラは PLAY 割込演奏
;     (7t_play.inc) への tick 供給と INTERVAL の計数を担う。
;------------------------------------------------------------------------------
tick_irq_src:
                LDA     >PORT_IRQSTAT   ; $FD03 読出 = 要因確認 + タイマ要因クリア
                BITA    #$04
                BNE     tis_done        ; bit2=1 → タイマ要因でない → 何もしない
                ; 機械語へ委譲中は ROM へ飛ばない (EXEC は time_disarm 済み = シャドウ bit2 = 0)。
                LDA     >fd02_shadow    ; BASIC 側が tick を使っているか (bit2)
                BITA    #$04
                BEQ     tis_done        ; 0 = 機械語へ委譲中 → 何もしない
                JMP     >tick_rom       ; ROM 側の処理 (INTERVAL の計数 / 演奏)
tis_done:
                RTI
tick_irq_src_end:

; ビルド時検査: 常駐の関門は TICK_ENTRY の 18B に収める。予算超過をアセンブル時に
;   検出する。
                IFGT    (tick_irq_src_end-tick_irq_src)-18
                ERROR   "tick_irq_src overruns TICK_ENTRY"
                ENDC

;------------------------------------------------------------------------------
; tick_rom — タイマ割込の ROM 側の処理 (低位 RAM の関門 tick_irq_src から JMP)。
;   INTERVAL の周期計数と、バックグラウンド演奏 (7t_play.inc) への分岐。RTI で戻る。
;------------------------------------------------------------------------------
tick_rom:
                ; --- INTERVAL 周期計数 ---
                ; intv_sub (秒未満の残り tick) が 0 の間は計時停止。動いている間は
                ; tick 毎に減算し、1 秒分を数え切ったら intv_sec (残り秒数) を 1
                ; 減らす。周期満了 (intv_sec=0) で発生保留 intv_pend を立て、次の
                ; 周期 intv_n を装填する。分岐そのものは実行ループの文の切れ目
                ; (iep_stmt_gate) が行うため、ここは数えるだけ。
                LDX     >intv_sub
                BEQ     trm_play        ; 0 = 計時停止中
                LEAX    -1,X
                STX     >intv_sub
                BNE     trm_play
                LDX     #INTV_TPS       ; 1 秒経過 → 秒未満計数を刻み直す
                STX     >intv_sub
                LDX     >intv_sec
                LEAX    -1,X
                STX     >intv_sec
                BNE     trm_play
                INC     >intv_pend      ; 周期満了 → 発生を保留に積む
                LDX     >intv_n         ; 次の周期を装填 (有効中は常に 1 以上)
                STX     >intv_sec
trm_play:
                ; バックグラウンド演奏中なら ROM 側プレイヤへ (7t_play.inc)。PLAY 文の
                ; 割込演奏 (参考書籍) は本 tick 基盤へ相乗りする。
                TST     >play_active
                BEQ     trm_rti
                JMP     >play_irq_tick  ; ROM 側で処理し RTI
trm_rti:
                RTI

; 低位 RAM 常駐ハンドラ内の RTI 単独入口 (割込フックスロットの既定ハンドラ)
TICK_RTI        EQU     TICK_ENTRY+(tis_done-tick_irq_src)

;------------------------------------------------------------------------------
; time_init: 時計の初期化 (コールドスタートから、IRQ マスク中に呼ぶ)
;   ハンドラ複写、割込フックスロット設置、$FD02 シャドウ設定 (時刻は設定しない)。
;   割込フックスロット ($01D1-$01E2, SWI3/SWI2/SWI/NMI/IRQ/FIRQ の 6 スロット
;   × JMP 3B) は RAM ベクタ表 ($FFF0-$FFFD) が指す低位 RAM の所定位置。全スロットへ安全な
;   既定ハンドラ (RTI) を設置した上で、IRQ スロットを tick ハンドラへ、FIRQ スロットを
;   FIRQ の受け口 (firq_isr) へ向ける。
;------------------------------------------------------------------------------
time_init:
                ; TIME$ の計時はサブ CPU の RTC ($3E/$3D) が担う。以下は tick
                ; 供給用の IRQ ハンドラ常駐・フックスロット・RAM ベクタ・$FD02
                ; シャドウの設置。
                ; ハンドラの複写元を低位 RAM へ複写
                LDX     #tick_irq_src
                LDU     #TICK_ENTRY
                LDB     #(tick_irq_src_end-tick_irq_src)
ti_cp:
                LDA     ,X+
                STA     ,U+
                DECB
                BNE     ti_cp
                ; 6 スロット全部へ JMP TICK_RTI (安全な既定ハンドラ) を設置
                IFDEF   STAGE_ALT_RAM
                LDU     #STAGE_IRQ+0          ; スロット先頭 (SWI3)
                ELSE
                LDU     #$01D1          ; スロット先頭 (SWI3)
                ENDC
                LDB     #6
ti_hook:
                LDA     #$7E            ; JMP extended
                STA     ,U+
                LDX     #TICK_RTI
                STX     ,U++
                DECB
                BNE     ti_hook
                ; IRQ スロット ($01DD) のみ tick ハンドラへ向け直す
                LDX     #TICK_ENTRY
                STX     >IRQ_HOOK+1
                ; FIRQ スロット (IRQ スロットの次 = $01E0) は当方の受け口へ。
                ;   ATTENTION と中断キーは $FD04 を読むまで要求線が下りないため、
                ;   既定の RTI のままでは要求が立ち続ける。
                LDX     #firq_isr
                STX     >IRQ_HOOK+4
                ; --- RAM 割込みベクタ表 ($FFF0-$FFFD) の実行時設置 ---
                ; FM77AV 系では 6809 割込みベクタは RAM にあり起動直後は不定。
                ; 本 ROM は $8000-$FBFF でベクタ領域を封入できないため、上で
                ; 設置したフックスロットを指すベクタ値をここで書き込む。
                ; $FFFE (RESET) はブート側管理のため触らない。
                LDX     #vec_ram_tbl
                LDU     #$FFF0
ti_vec:
                LDD     ,X++
                STD     ,U++
                CMPX    #vec_ram_tbl_end
                BLO     ti_vec
                ; 所有の要否を最初に判定する前に、判定材料をすべて確定させる。
                ; 演奏中フラグは PLAY ワークの初期化 (play_stop) で消えるが、
                ; それはコールドスタートの本ルーチンより後なので、ここで先に
                ; 倒しておく (未初期化の値で所有判定が走らないようにする)。
                CLR     >play_active
                ; 控えの初期値 = $04 (bit2: ROM 側 tick 処理を許可 / bit7: 未所有)。
                ; 平常時はハードの周期タイマ許可を持たない。刻みを要る場面
                ; (PLAY の割込演奏・INTERVAL の計時) で取りに行く (所有規約)。
                LDA     #$04
                STA     >fd02_shadow
                ; INTERVAL 割込ワークの初期化 (IRQ マスク中なので競合なし)

;------------------------------------------------------------------------------
; intv_reset — INTERVAL 割込の全状態を初期化する。破壊: A, B, X。
;   コールドスタート (time_init 経由) と RUN 毎 (iep_start_x) に呼ぶ。
;   INTERVAL・タイマの欄を個別に消す。分岐条件 (intv_st) を先に、計時停止 (intv_sub) を最後に消す。
;------------------------------------------------------------------------------
intv_reset:
                LDX     #0
                STX     >intv_st        ; 分岐条件を先に消す (+0 = ON 状態 / +1 = STOP)
                STX     >intv_pend      ; +2 = 保留 (隣の intv_line の上位も消える)
                STX     >intv_line
                STX     >timer_st       ; タイマ割込の欄も同じ機会に落とす (+0 / +1)
                STX     >timer_pend     ; (隣の timer_line の上位も消える)
                STX     >timer_line
                STX     >intv_n
                STX     >intv_sec
                STX     >intv_sub       ; 計時停止が最後
                JSR     key_reset       ; 機能キー割込も同じ機会に全 OFF へ戻す
                                        ;   (RUN で機能キーは全て OFF)
                JMP     time_hw_sync    ; 末尾呼び (要らなくなった所有は返す)

; RAM ベクタ表: $FFF0-$FFFD へ設置する 7 ワード (6809 ベクタ順)。
; 値は time_init が設置する割込フックスロットのアドレス。
vec_ram_tbl:
                FDB     $0000           ; $FFF0 (reserved)
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+0           ; $FFF2 SWI3
                ELSE
                FDB     $01D1           ; $FFF2 SWI3
                ENDC
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+3           ; $FFF4 SWI2
                ELSE
                FDB     $01D4           ; $FFF4 SWI2
                ENDC
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+15           ; $FFF6 FIRQ
                ELSE
                FDB     $01E0           ; $FFF6 FIRQ
                ENDC
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+12           ; $FFF8 IRQ (= IRQ_HOOK, tick ハンドラ入口)
                ELSE
                FDB     $01DD           ; $FFF8 IRQ (= IRQ_HOOK, tick ハンドラ入口)
                ENDC
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+6           ; $FFFA SWI
                ELSE
                FDB     $01D7           ; $FFFA SWI
                ENDC
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+9           ; $FFFC NMI
                ELSE
                FDB     $01DA           ; $FFFC NMI
                ENDC
vec_ram_tbl_end:

;------------------------------------------------------------------------------
; 周期タイマ割込の所有規約 ($FD02 は書込専用のため控えで状態を持つ)
;
;   $FD02 の bit2 (周期タイマ割込の許可) は 1 つしかない資源で、しかも読み
;   戻せない。BASIC はそれを所有しているかを控え (fd02_shadow) で持ち、所有して
;   いる間だけ $FD02 を書く (止める・戻す)。所有していない間は書かない。
;
;   控え fd02_shadow が持つ 2 つの意味 (控え自体は $FD02 へ書かない):
;     bit7 = BASIC がハードの許可を所有中 (= BASIC が $FD02 へ $04 を書いた)
;     bit2 = ROM 側 tick 処理の許可 (= 機械語へ委譲していない)
;
;   BASIC が刻みを要るのは次の 2 つだけで、要る間だけ所有する:
;     - PLAY の割込演奏 (play_active)
;     - INTERVAL の計時 (intv_sub が 0 でない)
;
;   公開ルーチン (いずれも A のみ使用。X/Y/U/B は保存。CC は変化する):
;     time_pause / time_resume — 所有中だけハードを止める/戻す (テープ読み書きの
;         パルス幅計測をタイマ割込のジッタから守る)。所有していなければ何も
;         しない。
;     time_disarm    — 機械語へ委譲: ROM 側 tick 処理の許可 (bit2) だけ倒す。
;         ハードは触らない。倒さないと、機械語が ROM オーバレイを解除して
;         いる最中に ROM アドレスへ絶対 JMP して暴走する。
;     time_rearm     — 委譲から復帰: 許可 (bit2) を戻し、要否に応じて所有を直す。
;     time_hw_claim  — 刻みを要り始める側 (割込演奏の投入・INTERVAL 計時開始) の
;         所有取得。許可 (bit2) を戻してから所有を取る。
;   所有の取得・返却の実体 (time_hw_arm / time_hw_free / time_hw_sync /
;   time_hw_claim / time_perm_on) と INTERVAL 計時開始 (intv_arm_tick) は 7t_statements.inc。
;------------------------------------------------------------------------------
time_pause:
                PSHS    A
                CLRA                    ; 止める = $FD02 へ $00
                BRA     tpr_go
time_resume:
                PSHS    A
                LDA     #$04            ; 戻す = $FD02 へ $04 (bit2 のみ)
tpr_go:
                TST     >fd02_shadow    ; bit7 = BASIC が所有中か
                BPL     tpr_ret         ; 未所有 → 1 バイトも書かない
                STA     >PORT_CMT
tpr_ret:
                PULS    A,PC
time_disarm:
                PSHS    D
                LDB     >fd02_shadow
                ANDB    #$FB            ; bit2 (ROM 側 tick 処理の許可) を倒す
                STB     >fd02_shadow
                PULS    D,PC
;------------------------------------------------------------------------------
; impl_fn_time: TIME$ / TIME 読出し (fn_jt の TOK2_TIME から)
;   入力: X = 関数トークン直後
;   読出し形は 2 つ: 直後に ASCII '$' が続く TIME$ は "HH:MM:SS" の文字列、続かない TIME は
;     その日の 0 時からの経過秒 (数値)。
;   出力 (TIME$): STRPTR/STRLEN/FACTYPE = "HH:MM:SS", fn_xsave = 読了位置
;   出力 (TIME) : FAC1 = 経過秒 (0-86399, 単精度)。本体は ift_num (7t_startup.inc)
;------------------------------------------------------------------------------
impl_fn_time:
                LDA     ,X
                CMPA    #'$'
                LBNE    ift_num         ; '$' なし → 数値形 TIME
                LEAX    1,X
                STX     fn_xsave
                ; サブ CPU RTC を Read Timer ($3E) で取得。返却は
                ;   SUBCMD_P3=時 / SUBCMD_P4=分 / SUBCMD_P5=秒。
                LDA     #$3E
                JSR     sub_send_cmd_a          ; A = コマンド番号のまま発行
                LDX     #time_buf
                LDA     SUBCMD_P3
                BSR     time_fmt2
                LDA     #':'
                STA     ,X+
                LDA     SUBCMD_P4
                BSR     time_fmt2
                LDA     #':'
                STA     ,X+
                LDA     SUBCMD_P5
                BSR     time_fmt2
                LDX     #time_buf       ; タイマ参照時刻 ($02F4-$02F9) を控える
                LDU     #TMR_REF
                JSR     sixdig_save
                LDD     #time_buf
                ; 文字列 8 文字を返す後始末は DATE$ (ifd_ret) と共用する。
                BRA     ifd_ret

; time_fmt2: A (0-99) を 10進2桁 ASCII で ,X+ へ格納。壊す: A,B
time_fmt2:
                LDB     #'0'
tf2_lp:
                CMPA    #10
                BLO     tf2_done
                SUBA    #10
                INCB
                BRA     tf2_lp
tf2_done:
                STB     ,X+
                ADDA    #'0'
                STA     ,X+
                RTS

;------------------------------------------------------------------------------
; impl_fn_date: DATE$ 読出し (fn_jt の TOK2_DATE から)。DATE$ 代入で格納した値
;   (date_buf) を返す。未設定時 (date_buf 先頭 = 0。コールドスタートで低位 RAM
;   が 0 クリアされる) は既定スタブ str_date_stub を返す。代入があれば先頭は
;   必ず非 0 になる。コールドスタートで date_buf へ書き込まない。TIME$ と同じく後続の
;   ASCII '$' を受け、fn_xsave を確定して返す。
;------------------------------------------------------------------------------
impl_fn_date:
                LDA     ,X
                CMPA    #'$'
                BNE     ifd_nd
                LEAX    1,X
ifd_nd:
                STX     fn_xsave
                LDA     date_buf        ; 先頭バイト (未設定=0)
                BNE     ifd_setbuf      ; 非 0 → 代入済みの date_buf を返す
                LDD     #str_date_stub  ; 0 → 代入前 → "00/00/00"
                BRA     ifd_ret
ifd_setbuf:
                LDD     #date_buf
ifd_ret:
                STD     STRPTR
                LDA     #8              ; "YY/MM/DD" (2 けたの年) の 8 文字
                STA     STRLEN
                JMP     fret_str_own    ; FACTYPE=3 を立てて復帰 (共通尾部)

;------------------------------------------------------------------------------
; fn_stmt_dispatch: 文頭の関数トークン ($FF xx) の文としての取り扱い
;   入力: X = 第2バイト位置 (第2バイトを消費して返す)
;   出力: C=1 … 文として処理済 (X = 文の直後)
;         C=0 … 文としての意味を持たない (X = 第2バイト直後。第2バイトは -1,X で
;               参照可。TIME ON / OFF / STOP は制御語を消費した位置)
;   TIME$="HH:MM:SS" (トークンイメージでは '$' 無しの TIME="…" 形も同義)・TIME ON / OFF /
;   STOP・DATE$=・SCREEN・INPUT・MID$・PEN を振り分ける。
;------------------------------------------------------------------------------
fn_stmt_dispatch:
                LDA     ,X+             ; 第2バイトを消費
                CMPA    #TOK2_TIME
                BEQ     fsd_time
                CMPA    #TOK2_DATE
                BEQ     fsd_date
                BRA     fsd_chkscr
fsd_time:
                ; TIME ON / TIME OFF / TIME STOP — タイマ割込の許可・禁止・一時停止
                ;   (参考書籍)。制御語はここで読み捨てる。
                JSR     skip_sp_x       ; 制御語の前の空白を読み飛ばす
                CMPA    #TOK_ON         ; ON
                BEQ     fsd_ton
                CMPA    #TOK_OFF        ; OFF
                BEQ     fsd_toff
                CMPA    #TOK_STOP       ; STOP
                BEQ     fsd_tstop
                ; TIME$="HH:MM:SS" (計時の設定) か TIME "hh:mm:ss" (割込予約時刻の
                ;   設定。参考書籍 TIME$) かを弁別する。代入形の印は [$] '='。
                LDU     #time_set_str
                CMPA    #'$'
                BEQ     fsd_setstr
                CMPA    #TOK_EQ
                BEQ     fsd_setstr
                LDU     #time_alarm_str ; 割込予約時刻の設定
                BRA     fsd_evalstr
fsd_ton:
                ; TIME ON — 予約時刻での割込動作を可能にする。STOP 中に来ていた
                ;   発生分は、この後の文の切れ目で割込ルーチンを呼ぶ。
                LDA     #1
                STA     timer_st
                LDA     timer_stp
                DECA
                BNE     fsd_tctl        ; 2 (割込処理ルーチン実行中) は倒さない
                STA     timer_stp       ; A=0
                BRA     fsd_tctl
fsd_toff:
                ; TIME OFF — タイマ割込を禁止する (保留していた発生分も捨てる)。
                CLR     timer_st
                CLR     timer_pend
                BRA     fsd_tctl
fsd_tstop:
                ; TIME STOP — 割込動作を一時停止する (発生は受け付ける)。
                LDA     #1
                STA     timer_stp
fsd_tctl:
                LEAX    1,X             ; 制御語を消費
                BRA     fsd_no          ; C=0 (代入形ではない)
fsd_date:
                ; DATE$="YY/MM/DD" 代入形か判定。
                LDU     #date_set_str
fsd_setstr:
                ; TIME$ / DATE$ 代入形の共通処理。
                ; U = 検証済み文字列を受け取る設定ルーチン。
                BSR     fsd_chkassign
                BCC     fsd_no
fsd_evalstr:
                ; 右辺の評価は U を壊すので、評価の前に U を退避する。
                PSHS    U               ; 設定ルーチンのアドレスを退避
                JSR     eval_chk_str    ; Z=1 なら文字列型 (共通化)
                PULS    U               ; (PULS は CC を変えないので直後の分岐は有効)
                BNE     fsd_tmerr
                ; 設定ルーチンは X を壊すので、本文ポインタを退避する。
                PSHS    X
                JSR     ,U
                PULS    X
                ORCC    #$01            ; C=1 (処理済)
                RTS
fsd_no:
                ANDCC   #$FE            ; C=0
                RTS
;------------------------------------------------------------------------------
; fsd_chkassign: 代入形 (関数トークン第2バイト直後に [$] '=') かを判定。
;   入力: X = 第2バイト直後。出力: C=1 … 代入形 (X = '=' の直後)、
;         C=0 … 代入形でない (X は入力値のまま。呼出側は値としての読出しへ)。壊す: A
;------------------------------------------------------------------------------
fsd_chkassign:
                PSHS    X               ; 非代入形なら巻き戻す
                LDA     ,X
                CMPA    #'$'
                BNE     fca_nd
                LEAX    1,X
fca_nd:
                JSR     skip_sp_x       ; A = 非空白文字
                CMPA    #TOK_EQ         ; '=' はトークナイザが変換済み
                BEQ     fca_yes
                CMPA    #'='            ; 念のため生 '=' も受ける (ASCII 由来本文)
                BEQ     fca_yes
                PULS    X               ; '=' が続かない → 非代入形
                ANDCC   #$FE            ; C=0
                RTS
fca_yes:
                LEAS    2,S             ; 退避 X は破棄 (代入形を確定)
                LEAX    1,X             ; '=' を消費
                ORCC    #$01            ; C=1
                RTS
fsd_chkscr:
                ; SCREEN 文 (関数トークン $FF TOK2_SCREEN が文位置に出現)。
                ; A = 第2バイト / X = 第2バイト直後 (引数先頭)。本体は scr_stmt。
                CMPA    #TOK2_SCREEN    ; SCREEN?
                BEQ     scr_stmt
fsd_chkinp:
                LBRA    mid_chk         ; 7t_midstmt.inc
err_c63:
                LDB     #63             ; File Not Found
                BRA     err_go2
err_rg:
                LDB     #7              ; Out of Memory
err_go2:
                JMP     impl_error_handler
fsd_tmerr:
                JMP     err_tmm         ; Type Mismatch

;------------------------------------------------------------------------------
; scr_stmt — SCREEN [<書込面マスク>][,<表示面マスク>]
;
;   SCREEN 文は図形を書き込む面と、画面に映す面を
;   それぞれ 3 ビットのマスク (0-7) で指定する。ビットは重ね合わせ表示の
;   3 面に 1 対 1 で対応し、1 = 選択 / 0 = 非選択。省略した側は現在の指定を
;   保つ。指定は CLS・RUN・NEW を跨いで保持される。
;
;   マスクはハードウェアの重ね合わせ表示制御レジスタ ($FD37) で効く。
;   同レジスタは「1 = その面を止める」極性で、下位 3 ビットが書込面、
;   上位ニブル (bit4-6) が表示面である。よって書き込む値は
;       ((7 - 表示面マスク) << 4) | (7 - 書込面マスク)
;   となる。レジスタは書込専用のため現在値を RAM (mpage_cur) に控える。
;
;   誤りの扱い :
;     マスクが 0-7 の外   → Illegal Function Call
;     マスクが文字列      → Type Mismatch
;     引数が 3 個以上     → Syntax Error (面の指定を反映した後で、残りの文字を調べて出す)
;   第 1・第 2 引数の誤りでは面の指定を変えない (第 3 引数の誤りは反映の後に出る)。
;   小数は最近接丸めで整数化する (0.5→1, 6.5→7)。
;
;   入力: X = 引数先頭。出力: C=1 (SCREEN 文として処理済), X = 文の直後。
;------------------------------------------------------------------------------
scr_stmt:
                LDA     mpage_cur
                EORA    #$77            ; A = (表示面<<4)|書込面 (選択=1 の側へ)
                PSHS    A               ; ,S = 構築中の指定
                BSR     scr_arg         ; 第1引数 (書込面)
                BCC     scr_disp        ; 省略 → 現在の指定を保つ
                ; 作業値の該当ビットだけを B の値で置換する
                ;   新 = 旧 ^ ((旧 ^ 新) & マスク)
                EORB    ,S
                ANDB    #$07            ; 書込面のビット
                EORB    ,S
                STB     ,S
scr_disp:
                JSR     skip_comma
                BCC     scr_fin
                BSR     scr_arg         ; 第2引数 (表示面)
                BCC     scr_fin         ; 省略 → 現在の指定を保つ
                LDA     #16
                MUL                     ; B = 表示面マスク << 4 (0-7 なので桁溢れなし)
                EORB    ,S
                ANDB    #$70            ; 表示面のビット
                EORB    ,S
                STB     ,S
scr_fin:
                PULS    A
                EORA    #$77            ; 表示制御レジスタの極性へ戻す
                STA     mpage_cur       ; 書込専用レジスタの控え
                STA     MULTIPAGE       ; $FD37 = 重ね合わせ表示制御
                ; 3 個目以降の引数は Syntax Error。ただし面の指定は上で反映済み
                ; のまま誤りにする。
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     scr_ok          ; 行末
                CMPA    #':'            ; 文区切り
                BEQ     scr_ok
                CMPA    #TOK_REM_APOS   ; ' (注記の短縮形) も文の終わりとして受ける
                LBNE    errc_2         ; それ以外が残る → Syntax Error
scr_ok:
                ORCC    #$01            ; C=1 (SCREEN 文として処理済)
                RTS

;------------------------------------------------------------------------------
; scr_arg — 省略可能な面マスク引数 (0-7) を 1 個読む
;   入力: X = 引数位置。
;   出力: C=1 … B = マスク値 (0-7), X = 引数の直後
;         C=0 … 省略 (',' / ':' / 行末), X は区切りの位置のまま
;------------------------------------------------------------------------------
scr_arg:
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     scr_omit        ;   行末 ($00) も省略として扱う
                CMPA    #':'            ; 文区切り
                BEQ     scr_omit
                CMPA    #','            ; 位置指定の省略
                BEQ     scr_omit
                JSR     eval_chk_str    ; Z=1 なら文字列型 (共通化)
                BEQ     fsd_tmerr
                JSR     impl_fac_to_int_round   ; D = マスク値 (最近接丸め)
                CMPD    #7
                BHI     tss_err         ; 0-7 の外 (負値も符号なし比較で捕まる)
                ORCC    #$01
                RTS
scr_omit:
                ANDCC   #$FE
                RTS

;------------------------------------------------------------------------------
; time_set_str: STRPTR/STRLEN の "HH:MM:SS" を解釈して内部時計を設定する
;   検証を全て通してから一括コミットする (途中エラーで時刻が半端に変わらない)。
;   形式不正・範囲外は Illegal Function Call。壊す: A,B,X
;------------------------------------------------------------------------------
time_set_str:
                BSR     tss_parse       ; 検証込みで SUBCMD_P3..P6 = 時/分/秒/0
                LDA     #$02
                STA     SUBCMD_P1       ; Set Timer マスク bit1 = 24 時間時計
                LDA     #$3D
                JMP     sub_send_cmd_a

;------------------------------------------------------------------------------
; time_alarm_str: TIME "hh:mm:ss" (参考書籍 TIME$)。STRPTR/STRLEN の
;   "hh:mm:ss" を割込予約時刻レジスタ (T1-I。参考書籍の相対 9〜12)
;   へ設定し、制御レジスタ bit0 (タイマ割込イネイブル) を立てる。設定は
;   1 レベルのみで、2 つの時刻は同時に持てない。壊す: A,B,X,U
;------------------------------------------------------------------------------
time_alarm_str:
                BSR     tss_parse       ; 検証込みで SUBCMD_P3..P6 = 時/分/秒/0
                LDX     #SUBCMD_P3
                LDU     #SUBCMD_P7      ; T1-I (相対 9〜12)
                LDB     #4
tal_cp:
                LDA     ,X+
                STA     ,U+
                DECB
                BNE     tal_cp
                LDD     #$0501          ; マスク bit0 (TC) + bit2 (T1-I) /
                STD     SUBCMD_P1       ;   TC bit0 = タイマ割込イネイブル
                LDA     #$3D
                JMP     sub_send_cmd_a

;------------------------------------------------------------------------------
; tss_parse: "HH:MM:SS" を検証して SUBCMD_P3..P6 (時/分/秒/フレーム 0) へ置く。
;   時・分・秒は検査を通るたびに置く。RTC へ送るのは全部を検査した後なので、途中の
;   誤りで時刻は変わらない。
;------------------------------------------------------------------------------
tss_parse:
                LDA     STRLEN
                CMPA    #8
                BNE     tss_err
                LDX     STRPTR
                BSR     tss_2dig        ; B = 時
                CMPB    #24
                BHS     tss_err
                STB     SUBCMD_P3       ; 時 → $D385
                BSR     tss_sep
                BSR     tss_2dig        ; B = 分
                CMPB    #60
                BHS     tss_err
                STB     SUBCMD_P4       ; 分 → $D386 ($D00D)
                BSR     tss_sep
                BSR     tss_2dig        ; B = 秒
                CMPB    #60
                BHS     tss_err
                STB     SUBCMD_P5       ; 秒 → $D387 ($D00E)
                ; 検証完了 → Set Timer ($3D) でサブ CPU RTC を設定
                ;   (途中エラーは送出前ゆえ RTC は不変)。マスク bit1 = RTC 4byte。
                CLR     SUBCMD_P6       ; フレーム = 0 (秒境界から刻み直す)
                RTS
tss_sep:
                LDA     ,X+
                CMPA    #':'
                BNE     tss_err
                RTS
tss_2dig:                               ; X から 10進2桁 → B (0-99)
                BSR     tss_digit       ; A = 十の位
                LDB     #10
                MUL                     ; D = A×10 (B = 値×10)
                PSHS    B
                BSR     tss_digit       ; A = 一の位
                ADDA    ,S+
                TFR     A,B
                RTS
tss_digit:
                LDA     ,X+
                SUBA    #'0'
                BCS     tss_err
                CMPA    #9
                BHI     tss_err
                RTS
tss_err:
                ; エラーハンドラへ直行する。
                JMP     errc_5         ; Illegal Function Call

; date_set_str (DATE$ 代入の本体) は 7t_datestr.inc

;------------------------------------------------------------------------------
; fsd_input: INPUT 文の本体 (fn_stmt_dispatch から)
;   書式: INPUT [;] ["プロンプト" {;|,}] 変数 [,変数...]
;     "プロンプト"; → プロンプト+"? " / "プロンプト", → プロンプトのみ /
;     プロンプト無し → "? " (参考書籍)。
;   入力: X = 第2バイト直後 (引数先頭)
;   出力: C=1 (INPUT 文として処理済), X = 文の直後
;   処理: プロンプト表示 → subsys 行エディタで 1 行取得 (INPBUF, 0 終端) →
;         カンマ区切りフィールドを変数リストへ順に代入する。
;         文字列変数は格納時に恒久領域へ複写される。
;         数値変数はフィールドを数値解析して格納する。
;------------------------------------------------------------------------------
; in_prompt — INPUT / LINE INPUT の前置き部を処理する (両者で共用)。
;   INPUT と LINE INPUT は引数の先頭に画面制御指定の ';'
;   を書け、続けて "文字列" のプロンプトを書ける。ここは両文で同じ綴りなので
;   1 本にまとめてある。
;   入力: X = 引数の先頭。
;   出力: C=1 … プロンプトを表示した (X = 閉じ引用符の直後 = 区切りの位置。閉じ引用符が
;               無いときは行末)
;         C=0 … プロンプトが無かった (X = 非空白文字の位置)
;   破壊: A。
;------------------------------------------------------------------------------
in_prompt:
                JSR     skip_sp_x
                CMPA    #';'            ; 先頭の ';' (画面制御指定) は読み飛ばす
                BNE     inp_np
                LEAX    1,X
inp_np:
                JSR     skip_sp_x
                CMPA    #'"'
                BNE     inp_none        ; プロンプト無し
                LEAX    1,X             ; 開き引用符消費
inp_lp:
                LDA     ,X+
                BEQ     inp_e0          ; 行末 (閉じ引用符欠落) → 打ち切り
                CMPA    #'"'
                BEQ     inp_e1
                JSR     impl_putchar    ; プロンプト表示 (カナ等もそのまま送出)
                BRA     inp_lp
inp_e0:
                LEAX    -1,X            ; 行末 $00 は消費しない
inp_e1:
                ORCC    #$01
                RTS
inp_none:
                ANDCC   #$FE
                RTS

fsd_input:
                PSHS    X               ; 引数の先頭を控える (打ち直しでここへ戻る)
fin_again:
                LDX     ,S
                BSR     in_prompt       ; ';' 読み飛ばし + プロンプト表示 (共用)
                BCC     fin_qmark       ; プロンプト無し → "? " を出す
                ; 区切り: ';' → "? " を出す / ',' → 出さない / 無し → そのまま
                JSR     skip_sp_comma
                BEQ     fin_sep_comma
                CMPA    #';'
                BNE     fin_read
                LEAX    1,X             ; ';' 消費 → "? " 表示へ
fin_qmark:
                LDA     #'?'
                JSR     impl_putchar
                JSR     put_space               ; 空白 1 個を出す
                BRA     fin_read
fin_sep_comma:
                LEAX    1,X             ; ',' 消費 (プロンプトのみ)
fin_read:
                ; 1 行入力 → INPBUF (0 終端)。X (本文ポインタ) を退避して呼ぶ
                PSHS    X
                JSR     impl_input_line
                PULS    X
                LDD     #INPBUF
                STD     inp_fld         ; フィールド走査位置 = 行頭
fin_var_lp:
                JSR     get_var_ref_chk ; U = 値アドレス, VARTYP 設定, X = 名前直後
                PSHS    X               ; 本文ポインタ退避
                LDX     inp_fld
                ; --- 1 項目を取り出す ---
                ; 打ち込まれた行の項目は DATA 文の値と同じ書式 (カンマ区切り、
                ; 引用符付きは引用符を外す、引用符なしは前後の空白を詰める) なので、
                ; 取り出しは共通ルーチンへ委ねる。
                JSR     inp_readval     ; VARTYP に従い値を取り出し X = 値の直後
                BCS     fin_bad         ; 数値として読めない項目 → 打ち直し
                STX     inp_fld         ; 走査位置 = 項目直後 (','/':'/行末)
fin_assign:
                PULS    X               ; 本文ポインタ復帰
                JSR     impl_assign_var ; U/VARTYP に従い格納
                ; --- 次の変数 (',') があれば続ける ---
                JSR     skip_comma
                BCC     fin_tail
                ; 打ち込まれた行の側も区切りを 1 つ消費する。区切りは ',' と ':'
                ; の 2 つで、どちらも同じ働きをする (参考書籍)。区切りが無いのは、
                ; 打ち込まれた項目が変数の並び
                ; より少ないということなので打ち直しを促す。
                PSHS    X
                LDX     inp_fld
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BEQ     fin_fld_adv
                CMPA    #':'
                BNE     fin_short
fin_fld_adv:
                LEAX    1,X
                STX     inp_fld
                PULS    X
                BRA     fin_var_lp
fin_short:
                PULS    X               ; 本文ポインタを捨てる
                BRA     fin_redo
fin_bad:
                LEAS    2,S             ; 本文ポインタの控えを捨てる
                BRA     fin_redo
fin_tail:
                ; 変数の並びは尽きた。打ち込まれた行がまだ残っていれば項目が多い。
                PSHS    X
                LDX     inp_fld
                LDA     ,X
                PULS    X
                BEQ     fin_done
fin_redo:
                ; 項目の数や型が変数の並びと合わないとき、打ち直しを促して
                ; 同じ INPUT を最初からやり直す。誤りではないので ON ERROR GOTO では捕まらない。
;------------------------------------------------------------------------------
; fin_redo_msg — INPUT の打ち直しを促す表示 (fsd_input から)。
;   打ち込まれた行は復帰で閉じているので行頭から書き始め、書いたあと行を送る。
;------------------------------------------------------------------------------
fin_redo_msg:
                JSR     print_inline
                FCC     "?Redo From Start"
                FCB     $0D,$0A,0
                BRA     fin_again      ; 引数の先頭から丸ごとやり直す
fin_done:
                LEAS    2,S             ; 引数先頭の控えを捨てる
                ORCC    #$01            ; C=1 (INPUT 文として処理済)
                RTS
