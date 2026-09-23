; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
str_date_stub:  FCC     "00/00/00"      ; DATE$ の代入前に返す値 (8 文字。0 埋め)
                FCB     0

;=== データ変数 ===
; (rnd_seed / trace_flag / cur_fg_color / cur_bg_color / gcur_x / gcur_y /
;  mml_* / sym_buf は RAM ($0180- / $0380-) に EQU で置く。ROM オーバレイ
;  有効中は $8000-$FBFF への書込が保持されず、ROM 側に置くと「常に初期値」に
;  化けるため。初期値設定は init_ram_vars が行う)
; CMT 作業変数: RAM に配置 (EQU のみ。ZMB/FCB は ROM に置けないので定義を持たない)
; cmt_filename  EQU $0084 (定義は EQU セクションにある)
; cmt_blk_type  EQU $0095
; cmt_blk_len   EQU $0096
; cmt_load_ptr  EQU $0097
; cmt_chk_sum   EQU $0099

;=== 二次コマンド予約語テーブル ===
kw_2nd_tbl:
                FCB     'C','H','A','I','N'+$80         ; $EE CHAIN
                FCB     'E','R','A','S','E'+$80         ; $EF ERASE
                FCB     'L','P','R','I','N','T'+$80     ; $F0 LPRINT
                FCB     'L','L','I','S','T'+$80         ; $F1 LLIST
                FCB     'P','L','A','Y'+$80             ; $F2 PLAY
                FCB     'S','O','U','N','D'+$80         ; $F3 SOUND
kw_2nd_fn_tbl:
                FCB     'L','P','O','S'+$80
kw_2nd_fn_tbl_end:

;==============================================================================
; サブシステム連携
;
; BUSY ポーリングプロトコル:
;   1. パラメータを $FC83+ に設定
;   2. コマンドコードを $FC82 に書く
;   3. $FD05 の bit7 (BUSY) が 0 になるまでポーリング
;      (サブ CPU がコマンドを処理し $D40A を読むと BUSY クリア)
;==============================================================================

; impl_sub_send_cmd: ステージング ($00DD-) に組み立てたコマンド/パラメータを
;   サブシステムへ発行する。FM-7 のデュアルポート共有 RAM はサブ CPU を HALT
;   ($FD05 bit7) している間だけメイン CPU から有効にアクセスできるため、
;   次の手順で安全に受け渡す:
;     1. サブ CPU を HALT し、HALT 確立 ($FD05 bit7=1) を待つ
;     2. ステージング 16B → 共有 RAM ($FC80-) へ複写
;     3. サブ CPU を RUN ($FD05=0) させてコマンドを処理させる
;     4. 完了判定: 共有 RAM の SH_CMD ($FC82) がサブにより 0 へクリアされるまで
;        待つ。読み取りは必ず HALT 下で行い、未完なら RUN して進行させる。
;     5. 完了したら HALT 下で 共有 RAM 16B → ステージングへ読み戻し、RUN して復帰
;   壊すレジスタ: なし (A/B/X/U を退避・復元)
; impl_sub_send_fast: 事前待ちなしの即送入口。コンソール出力 ($03) 専用。
;   利用者の機械語がサブ CPU を占有していると BUSY が立ったままになるので、
;   $03 まで事前待ちさせると PRINT が 1 文字ごとに上限待ちになる。
;   $03 は即送 + 後段の 255 回上限打切りとする。
impl_sub_send_fast:
                PSHS    A,B,X,U
                BRA     issc_send
sub_send_cmd_a:
                STA     SUBCMD_CMD      ; A = 発行するコマンド番号 (呼出側の STA を畳んだ入口)
impl_sub_send_cmd:
                PSHS    A,B,X,U
                ; --- 0. 先行コマンドの完了待ち ---
                ;   サブが前コマンドを処理中 (BUSY = $FD05 bit7) のまま共有 RAM 128B を
                ;   上書きすると、利用者の機械語が $3F で共有 RAM 領域内に置いて実行中の
                ;   ルーチンを踏み潰し、サブ CPU が暴走する。BUSY が解けてから書く。
                ;   待ちは無制限とする: 非 $03 の完了待ち (ssc_poll, B=0) も無制限である。
                ;   有界にすると、同領域内で長く走るルーチンを打ち切って上書きしてしまう。
issc_busy_wait:
                LDA     PORT_SUBCPU             ; $FD05 bit7 = BUSY
                BMI     issc_busy_wait          ; 1 の間は待つ (0 → 送信可)
issc_send:
                ; --- 1. HALT 要求 → 確立待ち ---
                BSR     ssc_halt
                ; --- 1a. 共有 RAM 相対 1 (STATUS) の発生通知を先に回収する ---
                ;   相対 1 はサブ→メインの通知欄 (参考書籍) で、機能キーの
                ;   PF 割込コードもここに載る。次の一括複写がこの欄ごと上書きして
                ;   しまうので、潰す前に取り込む (HALT 中なので直接読める)。
                JSR     kap_take
                ; --- 2. ステージング → 共有 RAM (128B) ---
                LDX     #SUBSTG_BASE
                LDU     #SUBCMD_BASE
                BSR     ssc_copy_blk            ; 復帰時 B=0 (複写カウンタが 0 まで減算済)
                ; --- 3. RUN (サブにコマンドを処理させる) ---
                CLR     PORT_SUBCPU             ; $FD05 = 0 (RUN)
                ; --- 4. SH_CMD が 0 になるまで完了待ち ---
                ;   B = コンソール出力 ($03) の完了ポーリング残回数。B=0 は「上限なし」。
                ;   サブシステムの手順では、コンソール文字送出は HALT で共有 RAM へ積んで
                ;   RUN すれば足り、完了フラグの待ち合わせを要しない (次コマンド発行時に
                ;   $FD05 bit7 で同期する)。利用者の機械語がサブ CPU を占有
                ;   すると SH_CMD が非ゼロのままになり PRINT/putchar が無限待ちに陥るため、$03 は
                ;   上限で打ち切る。上限 (255) は通常の出力の完了に要する回数を十分に覆う値。
                ;   キー入力 ($04) 等の本来ブロックする命令は B=0 のまま無制限に待つ
                ;   (打ち切ると入力を取りこぼす)。
                LDA     SUBCMD_CMD              ; 発行コマンド ($0102, = $FC82 相当)
                CMPA    #$03                    ; コンソール出力?
                LBNE    ssc_waitb               ; 非 $03 → BUSY 先行待ち (プール側)。
                                                ;   BUSY ($FD05 bit7) が降りるまで HALT を
                                                ;   一切掛けずに待ち、降りてから ssc_poll の
                                                ;   HALT 検証 (SH_CMD=0 確認) へ合流する
                COMB                            ; $03 → B=$FF (255 回上限)
ssc_poll:
                ; サブを少し走らせる (HALT 連打でサブが進めなくなるのを防ぐ)
                LDX     #$0040
ssc_run_dly:
                LEAX    -1,X
                BNE     ssc_run_dly
                BSR     ssc_halt                ; HALT 確立 (共有 RAM を読むため)
                LDA     SUBCMD_BASE+2           ; 共有 RAM の SH_CMD ($FC82) を読む
                BEQ     ssc_done                ; 0 → コマンド処理完了
                TSTB                            ; B=0 (上限なし) なら減算せず継続
                BEQ     ssc_repoll
                DECB                            ; $03 の残回数を消費
                BEQ     ssc_ff                  ; 上限枯渇 → fire-and-forget 打ち切り
ssc_repoll:
                CLR     PORT_SUBCPU             ; まだ → RUN して再試行
                BRA     ssc_poll
ssc_done:
                ; --- 5. 共有 RAM → ステージング (結果読み戻し, HALT 中) ---
                LDX     #SUBCMD_BASE
                LDU     #SUBSTG_BASE
                BSR     ssc_copy_blk
                ; --- 6. サブを待機ループ先頭へ戻し BUSY を解放させる (参考書籍準拠) ---
                ;   コマンド処理後、サブをそのまま RUN するだけでは、サブはコマンド
                ;   待ちループ内の任意位置から再開し待機ループ先頭の BUSY 解放を
                ;   通らないことがある。その結果 $FD05 bit7 (BUSY) が立ったまま残り、
                ;   機械語が $FD05 bit7=0 を待つ際に永久待ちに
                ;   なる。SH_CMD ($D382) を 0 にし ATN ($D380 bit7) を立ててから RUN
                ;   すると、サブは待機ループ先頭へ戻り ATN と BUSY をクリアして再待機
                ;   する。続けて $FD05 bit7=0 を確認してから復帰する。
                ;   (HALT 中なので共有 RAM $FC80- へ直接書ける)
                BSR     ssc_atn_run             ; ATN を立てて RUN → BUSY 解放待ち
                JSR     ssc_chk_err             ; 応答のエラーコードを写す (プール)
                PULS    A,B,X,U,PC
ssc_ff:
                BSR     ssc_run_wait            ; 打ち切り時は ATN を立てずに RUN
                PULS    A,B,X,U,PC

; >>FOLD ssc_atn_run
;------------------------------------------------------------------------------
; ssc_atn_run / ssc_run_wait — サブ CPU 側を待機ループ先頭へ戻す定型。
;   ssc_atn_run  : SH_CMD を 0 にし SH_ERR の ATN を立ててから ssc_run_wait へ。
;   ssc_run_wait : RUN させ、BUSY ($FD05 bit7) の解放を上限つきで待つ。
;   コンソール出力 / 図形コマンド発行 / テープ側の戻りの 3 か所で使う共通の定型。
;   直前は PULS ..,PC なので、この定義へ落ち込む経路は無い。
;   破壊: A, X。
;------------------------------------------------------------------------------
ssc_atn_run:
                CLR     SUBCMD_BASE+2           ; $FC82 = $D382 = SH_CMD ← 0
                LDA     #$80                    ; bit7 = ATN
                ORA     SUBCMD_BASE             ; $FC80 = $D380 = SH_ERR と合成 = ATN セット
                STA     SUBCMD_BASE
ssc_run_wait:
                CLR     PORT_SUBCPU             ; RUN (サブを通常走行へ戻す)
                LDX     #$4000                  ; BUSY 解放待ち上限 (異常時の保険)
sarw_lp:
                LDA     PORT_SUBCPU             ; $FD05 (bit7 = BUSY)
                BPL     sarw_done               ; bit7=0 → BUSY 解放完了
                LEAX    -1,X
                BNE     sarw_lp
sarw_done:
                RTS
; <<FOLD ssc_atn_run

; ssc_halt: サブ CPU を HALT し、HALT 確立 ($FD05 bit7=1) を待つ
ssc_halt:
                LDA     #$80
                STA     PORT_SUBCPU             ; $FD05 = HALT 要求
ssc_halt_wait:
                LDA     PORT_SUBCPU             ; $FD05 読み (bit7 = BUSY/HALT)
                BPL     ssc_halt_wait           ; bit7=0 ならまだ → 待つ
                RTS

; ssc_copy_blk: X→U へ SUBSTG_LEN バイト複写。壊す: B,X,U (呼出側で退避済)
;   複写は 2 バイト単位 (Y 経由)。HALT 中の複写はサブ CPU 側の走行をそのまま
;   止めるので、複写を速くすることが発行のたびに直に効く。
;   B は「残り対数」(呼出側は半分を渡す)。
;   Y は退避して復元する (impl_sub_send_cmd は Y を保存しない約束のため)。
ssc_copy_blk:
                LDB     #SUBSTG_LEN/2
ssc_cp_lp:
                PSHS    Y
scl_lp:
                LDY     ,X++
                STY     ,U++
                DECB
                BNE     scl_lp
                PULS    Y,PC

impl_sub_init:
                ; サブ CPU の起動完了待ち (リセット処理終了 = BUSY 解消まで) は
                ;   impl_sub_send_cmd の事前待ち ($03 以外は BUSY 解消を
                ;   待ってから送る) が受け持つ。
                ; cmd_01 コンソールイニシャライズ: 40 桁 × 20 行 (起動時の既定)
                ; 送出は WIDTH / CONSOLE と同じ con_init_send に任せる。全パラメータ
                ;   (背景色・消去指定・アトリビュート・機能キー行) を明示し、
                ;   桁数・行数・スクロール範囲の控え (con_cols / con_rows /
                ;   con_wtop / con_wrows) も同時に確定する。控えを別々に置くと、
                ;   起動直後の CONSOLE が「現在の設定」を取り違える。
                ; アトリビュートは現在の文字色 (cur_fg_color)。本ルーチンより先に
                ;   init_ram_vars が 7 (白) を入れてある。
                LDD     #40*256+CON_ROWS ; 40 桁 × 20 行 (起動時の既定)
                STA     con_cols
                STB     con_rows
                CLRA                    ; 範囲上端 0 / B = 範囲行数 = 行数 (全画面)
                STA     con_pf          ; 機能キー行表示 0 / 単色表示 0 (起動時の既定)
                STA     con_mono
                JSR     con_init_send
                ; TAB 停止位置を 8 桁ごとに設定する (参考書籍)
                JMP     tab_set_init    ; 末尾呼出 (プール。RTS は送出側)

put_zero:                               ; "0" を 1 文字出す
                LDA     #'0'
                BRA     impl_putchar
put_dot:                                ; "." を 1 文字出す
                LDA     #'.'
                BRA     impl_putchar
put_x_inc:                              ; X の指す 1 文字を出して X を進める
                LDA     ,X+
                BRA     impl_putchar
put_space:
                LDA     #' '            ; 空白 1 個 (呼出側の LDA を畳んだ入口)
impl_putchar:
                ; cmd_03_console: $D382=03, $D383=文字数(1), $D384=文字コード
                ; PRINT_COL を保守する (CR=$0D で 0 リセット, LF=$0A は不変,
                ; それ以外は +1。桁数 con_cols に達したら 0 = 次の行の先頭)。
                ; PRINT カンマのゾーン tab と POS(0) に使用。
                ; pc_capt 非0 の間はコンソールへ送らず RAM へ捕捉 (STR$)
                TST     pc_capt
                LBNE    ipc_capture
                CMPA    #$0D
                BNE     ipc_notcr
                CLR     PRINT_COL
                BRA     impl_putraw
ipc_notcr:
                CMPA    #$0A
                BEQ     ipc_lf
                INC     PRINT_COL
                ; 行の長さが画面の幅に達すると次の行に続く (参考書籍) ので、
                ; 桁の記録も次の行の桁 0 へ戻す (POS(0) は画面上の桁。参考書籍)。
                ; B は呼出側の計数 (pv_str 等) なので A だけを借りる。
                PSHS    A
                LDA     PRINT_COL
                CMPA    con_cols
                BNE     ipc_nowrap
                CLR     PRINT_COL
ipc_nowrap:
                PULS    A
                ; impl_putraw: A のバイトを PRINT_COL 更新なしでコンソール
                ; ($03) へ直接送る生出力。LOCATE/COLOR がコンソール制御コード
                ; ($12 位置指定 / $11 文字色指定) を送るのに使う。A/B/X/U 保存。
impl_putraw:
                STA     SUBCMD_P2           ; $FC84 = 文字コード (A を先に退避)
                LDA     #$03
                STA     SUBCMD_CMD          ; $FC82 = $03
                LDA     #1
                STA     SUBCMD_P1           ; $FC83 = 1 (文字数=1)
                JMP     impl_sub_send_fast  ; 末尾呼出 ($03 は即送入口, RTS は send 側で)
                ; ipc_lf: LF の後に SF オーダ ($11 + 現在の文字色) を送り、新しい行を
                ; 現在の文字色のフィールドにする (参考書籍。
                ; 1 行入力 $04 は変更フィールドを行の単位で返すため)。A 保存。
ipc_lf:
                BSR     impl_putraw         ; LF
                PSHS    A
                JSR     ic12_send           ; SF
                PULS    A,PC

; impl_print_str は容量の都合で 7t_deffn.inc のプールに置く。

; impl_sub_inkey: 非ブロッキング INKEY
;   cmd_29_inkey: $D383 = CF (バッファクリアフラグ), 出力: $D383 = キーコード, $D384 = 有無
;   出力: A = キーコード (0 = キー無し)
impl_sub_inkey:
                LDA     #$29
                STA     SUBCMD_CMD      ; $FC82 = $29
                CLR     SUBCMD_P1       ; $FC83 = CF=0 (バッファクリア不要)
                JSR     impl_sub_send_cmd
                LDA     SUBCMD_P2       ; $FC84 = 有無フラグ (0=無, 1=有)
                BEQ     isi_ret         ; フラグ=0 → キー無し (A=0 のまま返す)
                LDA     SUBCMD_P1       ; $FC83 = キーコード
isi_ret:
                RTS

; --- 空きプール ($C174-$C17A, 7 byte) ---
;   ここにはブロッキング INKEY を置かない (入力待ちは impl_input_line が
;   subsys $04 へ委譲するため、呼出元が無い)。
; 「JSR impl_eval_expr / JSR impl_fac_to_int」の共通ヘルパ (呼出側は 3B の JSR 1 本)。
eval_to_int:
                JSR     impl_eval_expr
                JMP     impl_fac_to_int
;==============================================================================
; dl_fat_next — ディスク割当表 (FAT) を読み dl_cluster の次クラスタを求める
;   割当表は物理シリンダ1/サイド0 セクタ1。先頭 5 バイトはヘッダで、
;   エントリは +5 からクラスタ番号順に並ぶ (動作観察):
;     値 < $98 (152) … 次クラスタ番号
;     $C0-$C7        … このクラスタが最終 (下位 = 使用セクタ数)
;     $FD/$FF ほか   … 空き/未使用 (鎖の続き無し)
;   断片化したファイルでも隣の別ファイルの領域を取り込まないよう、クラスタの
;   連番を仮定せず割当鎖を辿る。
;   入力: dl_cluster = 現クラスタ
;   出力: A = 次クラスタ値 ($98 以上は終端扱いとして呼出し元が判定)
;         FDC はシリンダ1/サイド0 へ移動済 (呼出し元が次クラスタで再 SEEK する)
;   注意: DA_SECBUF を割当表の読取に流用する (呼出し時点でバッファ読了済が前提)
;   破壊: A,B,X,Y
;==============================================================================
dl_fat_next:
                JSR     dl_seek_cyl1    ; サイド0・シリンダ1 へシーク (共通化)
                LDY     #DA_SECBUF
                LDA     #1              ; 割当表 = セクタ1
                JSR     disk_read_sector
                TSTA
                BNE     dfn_end         ; RNF → 終端扱い
                LDX     #DA_SECBUF+5    ; 先頭 5B はヘッダ
                LDB     dl_cluster
                ABX                     ; X += クラスタ番号 (符号無し)
                LDA     ,X              ; A = 次クラスタ値
                RTS
dfn_end:
                LDA     #$FF            ; 読取失敗 → 終端扱い
                RTS

;==============================================================================
; 入力
;==============================================================================

; impl_input_line — 1 行入力 (サブシステムの $04 GET、参考書籍)。
;   入力の直前に SF オーダ ($11 + アトリビュート) でカーソル位置からフィールドを
;   定め、$04 (N=0) でオペレータの入力を受ける。応答の相対 3 = K (終了キー)、
;   4 = N、5 = $12、6 = X、7 = Y、8〜 = TEXT。TEXT を INPBUF へ写す (120 バイトを
;   越える分は継続コマンド $64 で受ける)。他に変更されたフィールドが
;   あれば $05 GETC で N=0 になるまで読み捨てる (参考書籍)。
;   K=1 (CTRL+C / CTRL+X) は参考書籍の INPUT のとおり走行中なら
;   中断 ("Break In <行>")、直接モードなら入力し直し。K=2 (CLS) は入力し直し。
;   ただし AUTO の機能を選択中 (auto_flag が 0 以外) の直接モードでは、K=1 は
;   AUTO の終了である (参考書籍 AUTO)。auto_flag を 0 に戻して呼出元へ返す。
;   エコーはサブシステムが行う。改行は行の確定後にここで送る。
impl_input_line:
                JSR     ic12_send               ; SF: カーソル位置からフィールドを定める
iil_issue:
                LDY     #INPBUF
                LDA     #$04
                STA     SUBCMD_CMD
                CLR     SUBCMD_P1               ; N = 0 (表示する文字列なし)
                JSR     impl_sub_send_cmd
                LDA     >SUBSTG_BASE+3          ; K
                BEQ     iil_k0
                DECA
                BNE     iil_issue               ; K=2 (CLS) → 入力し直し
                LDX     CURLIN                  ; K=1: 走行中なら中断
                LEAX    1,X
                BNE     iil_brk
                TST     auto_flag               ; $FFFF = 直接モード
                BEQ     iil_issue               ;   AUTO 中でなければ入力し直し
                CLR     auto_flag               ;   AUTO 中は AUTO の機能を終了して
                RTS                             ;   呼出元 (impl_auto) へ返す
iil_brk:
                JMP     brk_stop                ; "Break In <行>" (戻らない)
iil_k0:
                LDB     >SUBSTG_BASE+4          ; N (0 = 変更フィールドなし)
                BEQ     iil_drain
                SUBB    #3                      ; SBA, X, Y を除く
                LDX     #SUBSTG_BASE+8
iil_more:
                BSR     iil_copy                ; B バイトを INPBUF へ
                TST     >SUBSTG_BASE+1          ; S′ (継続フラグビット)
                BPL     iil_drain
                LDA     #$64
                JSR     sub_send_cmd_a          ; A = コマンド番号のまま発行
                LDB     >SUBSTG_BASE+4          ; N
                LDX     #SUBSTG_BASE+5
                BRA     iil_more
iil_drain:                                      ; 他の変更フィールドを読み捨てる
                LDA     #$05
                JSR     sub_send_cmd_a          ; A = コマンド番号のまま発行
                TST     >SUBSTG_BASE+4          ; N = 0 で終り
                BEQ     iil_end
iil_dr64:
                TST     >SUBSTG_BASE+1
                BPL     iil_drain
                LDA     #$64
                JSR     sub_send_cmd_a          ; A = コマンド番号のまま発行
                BRA     iil_dr64
iil_end:
                JMP     ice_trim                ; 行末の空白を捨て、null 終端し、改行を送る
; iil_copy — X から B バイトを Y (INPBUF) へ。INPBUF+255 で止める (行は最大 255 文字)
iil_copy:
                TSTB
                BEQ     iic_ret
iic_lp:
                LDA     ,X+
                CMPY    #INPBUF+255
                BCC     iic_skip
                STA     ,Y+
iic_skip:
                DECB
                BNE     iic_lp
iic_ret:
                RTS

                ; 実行ループへ再入 (戻らない) (ここへ展開)
;==============================================================================
; ic_go — CONT の実行ループ再入 (impl_cont から JSR。本体はこのプールに置く)
;   再開位置は CONTREC (行レコード先頭) + CONTPTR (作業列内の位置) の組。
;   CONTREC の行を作業列へ復元 (再変換は決定的なので CONTPTR は同値で有効) し、
;   行文脈 (CURLINE_PTR / CURLIN) を戻して iep_stmt_new へ入る。
;   走り切った後 (CONTREC = 終端レコード) の CONT は何もしない。
;   EXEC_SP を張り直すのは、実行ループ終端 (iep_end) の RTS が呼出元
;   impl_cont へ戻れるようにするためと、ON ERROR トラップ / CHAIN の巻き戻し
;   基準を CONT 後の実行に合わせるため。
;   FOR / GOSUB のスタックと変数は触らないので、停止前の文脈がそのまま続く。
;==============================================================================
ic_go:
                ; CONT を打った直接モードの行の擬似レコードは要らない (この行へは
                ; 戻らない) ので、枠列の先頭にあれば降ろしてから再開する。
                JSR     dr_drop_top
                LDS     EXEC_SP                 ; 停止前の枠列の先頭 = 実行ループの S
                LDX     CONTREC                 ; X = 再開位置を含む行レコード先頭
                LDD     ,X                      ; 次行ポインタ
                BEQ     icg_end                 ; 終端レコード → 何もしない
                STX     CURLINE_PTR
                LDD     2,X                     ; 行レコード +2 = 行番号
                JSR     exec_ensure_curlin
                LDX     CONTPTR                 ; X = 再開位置 (作業列内)
                BRA    iep_stmt_new            ; 実行ループへ

icg_end:
                JMP     warm_start

;==============================================================================
; エラー処理
;==============================================================================

impl_error_handler:
                CLR     pc_capt         ; 捕捉モード中の異常時もコンソール復帰 (堅牢化)
                STB     ERRNUM
                LDD     CURLIN
                STD     ERRLIN
                ; ieh_report: 捕捉済みのエラー (ERRNUM / ERRLIN は設定済み) を
                ; 改めて表示するための入口。ON ERROR GOTO 0 をエラー処理ルーチンの
                ; 中で実行した時に ioe_off から入る。ERRNUM / ERRLIN を上書きしない
                ; よう、記録が済んだこの地点から始める。
ieh_report:
                JSR     ieh_trap_check  ; ON ERROR GOTO 有効化済ならハンドラ行へ (戻らない)
                ; 通常エラーは演奏中断の対象外 (参考書籍)。
                ; 表示前の行送りだけ行い、Break の中断経路は維持する。
                ; エラー名は必ず行頭から始まり、その 1 行前を空ける。本区間は
                ; 残余が 0 なので、その処理は呼出先 (プール) に置く。
                JSR     nl_blank
                ; --- エラー名の表示 ---
                ; 表示は <エラー名> In <発生行番号> の形で、番号は出さない。
                ; 文言表 (err_msg_tbl) は語末の bit7 で区切る 50 件。索引は 1 起算で、
                ; 外部へ出すエラーコードとは別体系である。
                ;   コード  1..26 → 索引  1..26
                ;   コード 50..64 → 索引 27..41 (コード − 23)
                ;   コード 65..73 → 索引 42..50 (コード − 23)
                ;   0 / 27..49 / 74 以上 → 索引 21 Unprintable Error
                ;                   (欠番のコードの表示)
                ; 番号や行番号の出力に使う print_uint16 は out_vec 経由で出力する。
                ; 出力先のコンソールへの張り替えは ieh_trap_check の itc_no
                ; (= エラーを表示する経路に確定した地点) で行っている。
                ; 索引は「先に 0 起算へ寄せてから 2 つの帯に収まるかを見る」順で
                ;   求める (閾値を並べて比べるのではなく、引いた後の値が帯に入るかで
                ;   決める)。コード 0 は DECB で $FF に落ち、どちらの帯にも入らない。
                LDB     ERRNUM
                DECB                    ; 0 起算へ
                CMPB    #26             ; 0..25 = コード 1..26
                BLO     ieh_idx_ok
                SUBB    #23             ; コード 50..73 → 26..49
                CMPB    #26
                BLO     ieh_idx_bad     ; 引いても帯に届かない = コード 27..49 (欠番)
                CMPB    #50             ; コード 50..73 → 26..49
                BHS     ieh_idx_bad     ; 0 と 74 以上 → Unprintable Error
ieh_idx_ok:
                LDX     #err_msg_tbl
ieh_skip:
                DECB
                BMI     ieh_show
ieh_scan:
                LDA     ,X+             ; 語末は bit7 が立っている
                BPL     ieh_scan
                BRA     ieh_skip
ieh_show:
                JSR     print_str7
                JMP     ieh_tail        ; 行番号付加と改行 (7t_startup.inc のプール)
ieh_idx_bad:
                LDB     #20             ; Unprintable Error (0 起算)
                BRA     ieh_idx_ok

;==============================================================================
; 実行エンジン
;==============================================================================

impl_exec_prog:
                ; RUN は入れ子の枠と直接モードの行を全て捨てる: システムスタックを
                ; 上端 (STRBOT) へ戻し、枠列を空にする。誤り捕捉も RUN 毎に解除。
                ; 本ルーチンは呼出元へ戻らない (走り切れば warm_start へ)。
                LDS     STRBOT
                STS     EXEC_SP
                CLR     ONERROR_EN
                ; 変数/配列テーブルを初期化 (ARYTAB = STREND = VARTAB = プログラム
                ; 末尾の次)。配列はスカラ変数の直後 (ARYTAB) から上方へ伸長し、
                ; STREND が配列領域末尾を指す (上方確保レイアウト)。
                ; 文字列領域の開放も伴うので共通ルーチンが行う。
                JSR     vars_clear_all
                ; READ/DATA ポインタ初期化 (DATLINE=先頭行, DATPTR=未探索)
                LDD     TXTTAB
                STD     DATLINE
                CLRA                    ; D = 0 (LDD #0 より 1 バイト短い)
                CLRB
                STD     DATPTR
                JSR     iep_start_x     ; X = 開始行 (RUN 行番号指定があれば該当行)
iep_loop:
                LDD     ,X
                BEQ    iep_end
                ; 行到達: 本文 (ASCII 原文) を実行用行バッファへ 1 行だけ変換する。
                ; CURLIN/CURLINE_PTR の更新も変換器が行い、X は変換済み
                ; 本文の先頭 (作業列) を指して返る。
                JSR     exec_conv_line
                ; --- 文の先頭 (RESUME の再開位置) の記録について ---
                ; CURIP は「今実行している文の先頭」を保持する。文の切れ目は
                ; 行頭と ':' だけであり、IF ... THEN / ELSE で節へ落ちる所は
                ; 切れ目ではない (参考書籍の RESUME の定義。RESUME は
                ; エラーが起きた文をやり直すが、IF の節は IF 文の一部なので
                ; 条件判定からやり直す)。よって CURIP を書くのは iep_stmt_new
                ; = 行頭と ':' 直後だけに限り、ハンドラ復帰後は書かない。
                ; 行を跨ぐ命令 (GOTO/GOSUB/RETURN/NEXT/CONT 等) は各ハンドラが
                ; 自分で CURIP を設定するので、この方針で辻褄が合う。
iep_stmt_new:
                STX     CURIP
                STS     EXEC_SP         ; 文の切れ目の S = 枠列の先頭 (誤り捕捉の巻戻し先)
                ; 文の切れ目で一時文字列を一括開放する。文をまたいで
                ;   生きる一時は存在しない (代入は必ず恒久側へ複写するため)。
                ; 開放は iep_stmt_gate (プール) 経由。INTERVAL の発生保留が
                ;   あれば指定行への GOSUB をここ (文の切れ目) で分岐するため。
                ;   直接モードの切れ目 (cmd_direct_stmt) は
                ;   str_tmp_clear 直呼びで、分岐しない。
                JSR     iep_stmt_gate
                IFNDEF  STAGE_ALT_RAM
                JSR     >HOOK_STMT      ; ステートメント実行フック
                ENDC
                                        ;   (参考書籍。通常は RTS)
iep_stmt:
                LDA     ,X+
                BEQ     iep_next_line
                CMPA    #':'
                BEQ     iep_stmt_new
                ; $80 未満の生バイトはプールの ies_alpha が一手に裁く:
                ;   空白 → 読み飛ばし / 英字 → 代入なら暗黙 LET、さもなくば
                ;   Syntax Error / その他の生の記号で始まる文 → Syntax Error
                ;   (動作観察)。
                ; 当区間は残余が無いため、判定の本体はプールに置く。
                CMPA    #TOK_FIRST
                BHS     ies_tok
                JSR     ies_alpha
                BRA     iep_stmt        ; CURIP は文の先頭のまま
ies_tok:
                ; 関数トークン ($FF xx) が文頭に来た場合は文としては no-op。
                ; 第2バイトまで読み飛ばす。$FF は TOK_2ND_FIRST 以上なので
                ; 下の BHS iep_2nd より先に判定する。さもなくば iep_2nd が $FF 1バイト
                ; だけ消費し、第2バイトが別の文トークンとして再解釈され誤動作する。
                CMPA    #TOK_FUNC_PFX
                BEQ     iep_fn_stmt
                CMPA    #TOK_2ND_FIRST
                BHS     iep_2nd
                ; 命令ディスパッチ
                ; A = トークン値, X = トークン後のポインタ (入力データの先頭)
                ; シンプルなディスパッチ: Y でハンドラ、X で入力ポインタ
                PSHS    Y               ; Y を保存
                ; テーブル索引は ABX で 16bit 加算する。B,X の符号付き
                ; インデックス (-128..+127) では $C0 以上のトークンで
                ; オフセットが負に化けて誤アドレスを読むため使わない。
                SUBA    #TOK_FIRST      ; A = トークンインデックス (0..)
                TFR     A,B             ; B = インデックス (符号無 0..255)
                LDY     #cmd_jt         ; X 退避前にハンドラを Y へ求める
                STX     ,--S            ; 入力ポインタを保存 (X はまだ使う)
                TFR     Y,X             ; X = cmd_jt
                ABX                     ; X += インデックス
                ABX                     ; X += インデックス → cmd_jt + 2*idx
                LDY     ,X              ; Y = ハンドラアドレス
                PULS    X               ; X = 入力ポインタ (データ用)
                JSR     ,Y              ; ハンドラ呼び出し
                PULS    Y               ; Y を復元
                BRA     iep_stmt        ; CURIP は文の先頭のまま
iep_fn_stmt:
                ; TIME$= 代入は fn_stmt_dispatch が処理。他は第2バイト消費のみ (no-op)
                JSR     fn_stmt_dispatch
                BRA     iep_stmt
iep_2nd:
                ; ここへは直前の CMPA #TOK_2ND_FIRST / BHS でしか来ないので
                ; A < TOK_2ND_FIRST は起こり得ないので、ここに再判定
                ; (CMPA #TOK_2ND_FIRST / BLO iep_stmt) は置かない。
                CMPA    #TOK_2ND_LAST
                BHI     iep_stmt
                JSR     call_cmd2       ; 二次コマンドのハンドラ呼出 (共通化)
                BRA     iep_stmt
iep_next_line:
                ; 次行レコードへ進む。終端マーカ (リンクのワードが $0000) の検査は
                ; iep_loop が行うので、そこへ戻して一元化する (リンク先を無検査で
                ; 行レコード扱いすると、END 無しプログラムの終端で変数領域を
                ; トークンとして誤実行する)。
                LDX     CURLINE_PTR
                LDX     ,X              ; 次行ポインタ
                ; --- 直接モードの行の終端 ---
                ; 直接モードの行の擬似レコードはリンク欄が $FFnn (負) なので、
                ; ここで見分ける。1 行の実行完了 = Ready へ戻る (warm_start が
                ; 枠列の先頭の擬似レコードを降ろす)。
                LBMI    warm_start
                BEQ     iep_end         ; リンク=0 (防御的)
                BRA    iep_loop        ; リンク=0 (終端マーカ) は iep_loop が検出
; pool_gr_opts (続き 4/4): 既定機能の補填と復帰 (本体は 7t_startup.inc)
pgo_nofn:
                CLRB                                ; 機能の既定値 (FUNC_PSET = 0)
pgo_ret:
                PULS    A,PC ; A = パレットコード
iep_end:
                ; 末尾まで走り切ったら再開位置を「末尾」に更新する。
                ; X は終端マーカ (リンク = $0000) のレコードを指しているので、
                ; この後の CONT は CONTREC の終端を見て直ちに実行を終え Ready に
                ; 戻る (= 走り切った後の CONT はエラーにせず何もしない)。
                STX     CONTPTR         ; 非 0 = 再開可の印
                STX     CONTREC         ; 再開行 = 終端レコード (ic_go が判別)
                LDD     #$FFFF
                STD     CURLIN
                JMP     warm_start
