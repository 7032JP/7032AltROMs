                IFDEF   STAGE_ALT_RAM
                INCLUDE "7t_stage_ram.inc"
                ENDC
; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 7T-BASIC 3.1 — F-BASIC V3.0 互換 BASIC インタプリタ
;
; ROM サイズ : 31744 byte ($7C00 bytes)  $8000–$FBFF
;   (アセンブル後のイメージは 32768 byte ($8000–$FFFF) で、配布する ROM はその先頭 31744 byte を切り出したもの)
;
; 設計方針: アドレスを固定するのは、参考書籍が位置を規定している所だけに限る。
;   すなわち ROM 領域と RAM 領域の境界 (参考書籍、
;   参考書籍)、I/O 領域の各ポート (参考書籍)、
;   サブ CPU との共有領域の窓と相対構造 (参考書籍)、
;   6809 のシステムベクトル (参考書籍)、BIOS の間接ベクタ語 $FBFA (参考書籍)、
;   参考書籍が定める機械語入口とワークエリア。
;   それ以外の入口・表・ワークは通常のラベル / EQU として自然に配置し、
;   参照は全て記号経由で行う (アドレスに意味を持たせない)。
;   目印語と起動ベクタ語 (BASVEC_*) の位置だけは、本プロジェクトの起動 ROM と
;   本 ROM の間の内部の取り決めとして保つ。
;   当方が選んだアドレスである (語に入る値は自然に決まったアドレス)。
;   予約語の綴りと言語仕様は参考書籍に基づく。表の内部構造・欄の
;   並び・実装コードの配置・各表の置き場所は本実装が独自に決めた。
;   ワーク RAM のうち参考書籍がアドレスを定めるものはそのアドレスに置く。
;   それ以外のワークは参考書籍が
;   示す BASIC 領域 ($0000-$05FF) の内側へ、直接ページの参照効率を優先して
;   割り付ける (アドレスは内部都合)。
;==============================================================================

;--- 当方の起動 ROM との内部の取り決め (アドレスは当方が選んだもの) ------------
BASVEC_SIG      EQU     $FBF4           ; 目印語 "7T" (直後に起動ベクタ 2 つ = 2 バイトの値 2 つ)

                include "7t_tokens.inc"  ; 予約語トークン値のシンボル定義 (EQU のみ)

;==============================================================================
; ゼロページワーク（RAM アドレス定義のみ）
;==============================================================================
                IFDEF   STAGE_ALT_RAM
TXTTAB          EQU     STAGE_PTR+1
                ELSE
TXTTAB          EQU     $0033   ; プログラム本文先頭 (参考書籍「BASIC テキスト先頭」)
                ENDC
                IFDEF   STAGE_ALT_RAM
VARTAB          EQU     STAGE_PTR+3
                ELSE
VARTAB          EQU     $0035   ; スカラ変数域先頭 = 本文終端の直後 (参考書籍「BASIC テキスト終端」)
                ENDC
                IFDEF   STAGE_ALT_RAM
ARYTAB          EQU     STAGE_PTR+9
                ELSE
ARYTAB          EQU     $003B   ; 配列域先頭 (参考書籍「配列変数先頭」)
                ENDC
                IFDEF   STAGE_ALT_RAM
STREND          EQU     STAGE_PTR+11
                ELSE
STREND          EQU     $003D   ; 変数域末尾 (参考書籍「未使用領域先頭」)
                ENDC
; 配列レイアウトは F-BASIC 3.0 のワーク規約（参考書籍の記載に基づく）に合わせた「プログラム直後・上方確保」:
;   TXTTAB → プログラム → VARTAB → スカラ変数 → ARYTAB → 配列 → STREND → 空き ← S
;   (上方確保にするのは、VARPTR(A%(0)) が返す実体アドレスを相互運用要件どおりに
;    するため。下方確保では VARPTR に依存する配置敏感なローダが動かない)
                IFDEF   STAGE_ALT_RAM
ARR_NEWEND      EQU     STAGE_PTR+17
                ELSE
ARR_NEWEND      EQU     $0043   ; 配列確保/変数挿入の新末尾一時 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
CURLIN          EQU     STAGE_PTR+21
                ELSE
CURLIN          EQU     $0047   ; 現在行番号 (参考書籍「実行中の行番号」)
                ENDC
                IFDEF   STAGE_ALT_RAM
DATPTR          EQU     STAGE_PTR+33
                ELSE
DATPTR          EQU     $0053   ; データ読取位置 (参考書籍「DATA アドレス」)
                ENDC
                IFDEF   STAGE_ALT_RAM
ERRLIN          EQU     STAGE_IO+34
                ELSE
ERRLIN          EQU     $00AD   ; 誤りの起きた行番号 (参考書籍「ERL」)
                ENDC
                IFDEF   STAGE_ALT_RAM
ERRNUM          EQU     STAGE_IO+33
                ELSE
ERRNUM          EQU     $00AC   ; 誤り番号 (参考書籍「ERR」)
                ENDC
                IFDEF   STAGE_ALT_RAM
CONTPTR         EQU     STAGE_PTR+27
                ELSE
CONTPTR         EQU     $004D   ; CONT 再開位置 (停止した文の次の位置)。0 = 再開不可 2B
                ENDC
                                ;   (参考書籍「CONT 再開ステートメント」の位置)
                IFDEF   STAGE_ALT_RAM
CURLINE_PTR     EQU     STAGE_PTR+25
                ELSE
CURLINE_PTR     EQU     $004B
                ENDC
                IFDEF   STAGE_ALT_RAM
CURIP           EQU     STAGE_PTR+29
                ELSE
CURIP           EQU     $004F   ; 実行 IP (2B。参考書籍「実行中ステートメント」の位置)
                ENDC
                IFDEF   STAGE_ALT_RAM
in_cont_flag    EQU     STAGE_RUNTIME+100
                ELSE
in_cont_flag    EQU     $01E4   ; NEXT: 1=ループ継続, 0=終了 (impl_next が参照)
                ENDC
; --- AUTO (参考書籍「AUTO フラグ / AUTO 次行番号 / AUTO 増分」) ---
                IFDEF   STAGE_ALT_RAM
auto_flag       EQU     STAGE_IO+18
                ELSE
auto_flag       EQU     $009D   ; AUTO の機能を選択中か (0 = 選択しない / 0 以外 = 選択中)
                ENDC
                IFDEF   STAGE_ALT_RAM
auto_next_ln    EQU     STAGE_IO+19
                ELSE
auto_next_ln    EQU     $009E   ; AUTO が次に発生する行番号 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
auto_inc        EQU     STAGE_IO+21
                ELSE
auto_inc        EQU     $00A0   ; AUTO の行番号の増分値 (2B)
                ENDC
; --- UNLIST (参考書籍「UNLIST 行番号」) ---
;   LIST はこの値以上の行番号を持つ行を表示しない。$FFFF = 制限なし。
                IFDEF   STAGE_ALT_RAM
unlist_ln       EQU     STAGE_RUNTIME+103
                ELSE
unlist_ln       EQU     $01E7   ; UNLIST で指定した非表示行番号 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
fn_xsave        EQU     STAGE_PTR+35
                ELSE
fn_xsave        EQU     $0055   ; 関数引数評価後の本文ポインタ退避 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
VARTYP          EQU     STAGE_PTR+37
                ELSE
VARTYP          EQU     $0057
                ENDC
;  行入力バッファ。参考書籍は論理行を 255 文字と定めるので、
;  256B 全域を使う (255 文字 + $00 終端)。置き場所は参考書籍の
;  「1 行入力バッファ」($043D-$053C) に合わせる。直接モードの行はこの上で実行する
;  ため、他のワークを一切重ねない (行の実行中に踏まれて本文が壊れる)。
;    生存期間が行入力を跨いでいた CONTLPTR は廃止し、CONT 時に導出する。
                IFDEF   STAGE_ALT_RAM
INPBUF          EQU     STAGE_INPUT+0
                ELSE
INPBUF          EQU     $043D
                ENDC
;  行入力の受け皿はコマンド待ち (cmd_loop) と INPUT / LINE INPUT だけが使う。
;  直接モードの行は、入力後にシステムスタック上の擬似行レコードへ写してから
;  実行用行バッファ (EXECBUF) で実行するので、実行中の本文はここには無い。
;  そのため、文の実行中だけ生きる作業域 (str_work_buf の束) と時間排他で
;  共用する (受け付けた行は写し終えるまで壊さない)。
; EXEC アドレス (2B)。参考書籍「EXEC アドレス」。
;   参考書籍の EXEC は「開始アドレスの指定がないときは、直前に実行した LOADM の
;   入口アドレス、または直前の EXEC の開始アドレスから実行する」と定める。LOADM の完了と
;   開始アドレス付きの EXEC がここへ記し、開始アドレス無しの EXEC がここを読んで飛ぶ。
;   未設定のときは Illegal Function Call の入口 (ramstub_ifc) を指す (コールド
;   スタートで入れる)。USR0〜USR9 のアドレス (usr_slot_tbl) が直後に続く。
                IFDEF   STAGE_ALT_RAM
EXEC_ADDR       EQU     STAGE_RUNTIME+151
                ELSE
EXEC_ADDR       EQU     $0217
                ENDC
                IFDEF   STAGE_ALT_RAM
str_conv_buf    EQU     STAGE_BUFFER+44
                ELSE
str_conv_buf    EQU     $0569   ; 数値→文字列 変換用 RAM バッファ (9B)
                ENDC
                IFDEF   STAGE_ALT_RAM
str_sign        EQU     STAGE_RUNTIME+79
                ELSE
str_sign        EQU     $01CF   ; int_to_str 符号フラグ 1B
                ENDC
; 文の実行中だけ生きる作業域の束 (256B)。1 行入力バッファ (INPBUF) と時間排他で
;   共用する (上の INPBUF の注記)。参考書籍の「中間言語バッファ」
;   ($033A) は実行用行バッファ (EXECBUF) に充てる。
str_work_buf    EQU     INPBUF  ; 文字列連結作業バッファ (256B)
                IFDEF   STAGE_ALT_RAM
escat_save_x    EQU     STAGE_RUNTIME+17
                ELSE
escat_save_x    EQU     $0191   ; 文字列連結中のプログラム本文ポインタ退避 (2B)
                ENDC
                                ;
; 文字列関数の出力バッファは必ず RAM に置く (ROM 上では書込が無効化されるため)。
                IFDEF   STAGE_ALT_RAM
hex_buf         EQU     STAGE_FILE+94
                ELSE
hex_buf         EQU     $0300   ; HEX$ 出力 (4B)
                ENDC
                IFDEF   STAGE_ALT_RAM
hex_ptr         EQU     STAGE_FILE+98
                ELSE
hex_ptr         EQU     $0304   ; HEX$ 書込ポインタ (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
hex_cnt         EQU     STAGE_FILE+100
                ELSE
hex_cnt         EQU     $0306   ; HEX$ 桁カウンタ (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
oct_buf         EQU     STAGE_FILE+101
                ELSE
oct_buf         EQU     $0307   ; OCT$ 出力 (6B)
                ENDC
                IFDEF   STAGE_ALT_RAM
oct_ptr         EQU     STAGE_RUNTIME+76
                ELSE
oct_ptr         EQU     $01CC   ; OCT$ 書込ポインタ (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
oct_cnt         EQU     STAGE_RUNTIME+78
                ELSE
oct_cnt         EQU     $01CE   ; OCT$ 桁カウンタ (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
DA_SECBUF       EQU     STAGE_BUFFER+175
                ELSE
DA_SECBUF       EQU     $05EC   ; ASCII ディスク LOAD: セクタ読取バッファ (256B)
                ENDC
                                ;   テープの機械語ブロック緩衝 (MLBUF) / SAVE のブロック
                                ;   組立バッファ (IIL_BUF) / 演奏の音符キュー (PLAYQ) と
                                ;   アドレス共有 (参考書籍「CAS0 入出力
                                ;   バッファ」)。テープ・ディスクの入出力と PLAY 演奏は
                                ;   互いに入口で相手を止めるため時間排他で共用できる。
                IFDEF   STAGE_ALT_RAM
DA_SECBUF_END   EQU     STAGE_BUFFER+431
                ELSE
DA_SECBUF_END   EQU     $06EC   ; セクタバッファ排他上限
                ENDC
space_buf       EQU     str_work_buf+$00   ; SPACE$ 出力 (80B)
; STRING$ は結果を文字列一時領域へ直接書くので固定緩衝を持たない。
                IFDEF   STAGE_ALT_RAM
STRBOT          EQU     STAGE_PTR+13
                ELSE
STRBOT          EQU     $003F   ; 文字列領域の下限 (2B)。このアドレス自体は使わない。
                ENDC
                                ;   システムスタックの上端でもある (標準構成と同じ配置。
                                ;   参考書籍「文字領域先頭」の位置)。
                IFDEF   STAGE_ALT_RAM
TMPPTR          EQU     STAGE_IO+77
                ELSE
TMPPTR          EQU     $00D8   ; 一時文字列で最後に使ったアドレス (2B)。初期値 = STRBOT。
                ENDC
                                ;   文の切れ目ごとに STRBOT へ戻して一括開放する。
; RND の系列状態 (4B = 32bit)。上位 24bit が返す値の仮数、最下位 8bit が
;   正規化のガードになる。
;   置き場所は参考書籍の「RND ワークエリア」($0318-$031E)。
                IFDEF   STAGE_ALT_RAM
rnd_state       EQU     STAGE_FILE+118
                ELSE
rnd_state       EQU     $0318   ; RND 状態 (4B: 32bit の系列状態)
                ENDC
STRSPC_DEF      EQU     300     ; 文字列領域の既定の大きさ (byte)。CLEAR n で変えられる。
STR_GC_TH       EQU     64      ; 文の切れ目で詰め直しに入る空き容量のしきい値 (byte)
                IFDEF   STAGE_ALT_RAM
str_gcflg       EQU     STAGE_RUNTIME+106
                ELSE
str_gcflg       EQU     $01EA   ; ごみ (どの記述子も指さない実体) がある印 (1B)。
                ENDC
                                ;   文字列変数を上書きして旧実体を捨てたときに立て、
                                ;   詰め直しで倒す。これが 0 の間は詰め直しても
                                ;   1 byte も回収できないので走らせない。
; --- ON〜GOSUB 割込テーブル ($071E-$0777) ---
;   参考書籍「ON〜GOSUB 割り込み用テーブル」。1 件 5 バイトで、
;   PEN ($071E) / COM(n) ($0723) / KEY(n) ($073C) / TIMER ($076E) /
;   INTERVAL ($0773) の順に並ぶ。各件の意味は
;     +0 … 0 以外のとき ON 状態
;     +1 … 0 以外のとき STOP 状態であるか、割込処理ルーチンを実行中
;     +2 … 0 以外のとき割込があった
;     +3,+4 … ON〜GOSUB で指定された行
;   本 ROM は +1 に 1 = STOP、2 = 割込処理ルーチン実行中 を入れ、RETURN
;   (impl_return_entry) が 2 の欄だけを倒す (割込の入れ子を作らないため)。
                IFDEF   STAGE_ALT_RAM
EVTBL           EQU     STAGE_BUFFER+481
                ELSE
EVTBL           EQU     $071E   ; ON〜GOSUB 割込テーブルの先頭 (PEN の欄)
                ENDC
; --- タイマ割込ワーク (参考書籍 ON TIME GOSUB / TIME "hh:mm:ss" / TIME ON・OFF・STOP) ---
;   上の表の TIMER の欄。割込予約時刻そのものはサブシステムの割込予約時刻
;   レジスタ (T1-I。参考書籍) が持ち、発生は STATUS の
;   bit4 TIMER (参考書籍) で受け取る。設定は 1 レベルのみ。
                IFDEF   STAGE_ALT_RAM
timer_st        EQU     STAGE_BUFFER+561
                ELSE
timer_st        EQU     $076E   ; +0 ON 状態 (0=OFF)
                ENDC
                IFDEF   STAGE_ALT_RAM
timer_stp       EQU     STAGE_BUFFER+562
                ELSE
timer_stp       EQU     $076F   ; +1 1=STOP / 2=割込処理ルーチン実行中
                ENDC
                IFDEF   STAGE_ALT_RAM
timer_pend      EQU     STAGE_BUFFER+563
                ELSE
timer_pend      EQU     $0770   ; +2 発生保留 (STATUS bit4 を受けて立てる)
                ENDC
                IFDEF   STAGE_ALT_RAM
timer_line      EQU     STAGE_BUFFER+564
                ELSE
timer_line      EQU     $0771   ; +3,+4 ON TIME GOSUB の飛び先行番号 (0=未登録)
                ENDC
; --- INTERVAL 割込ワーク ---
;   intv_st / intv_stp / intv_pend / intv_line は上の表の INTERVAL の欄。
;   intv_n はその欄の外 ($0576) に置く。
;   intv_pend / intv_sec / intv_sub はタイマ割込ハンドラ (7t_time.inc:
;   tick_irq_src) が書き替える。16bit の読み書きは 1 命令 (LDD/STD/LDX/STX)
;   で行うこと (6809 の割込は命令境界でしか入らないので 1 命令なら千切れない)。
;   一括初期化は intv_reset (コールドスタートと RUN 毎)。
                IFDEF   STAGE_ALT_RAM
intv_line       EQU     STAGE_BUFFER+569
                ELSE
intv_line       EQU     $0776   ; +3,+4 ON INTERVAL GOSUB の飛び先行番号 (0=未登録)
                ENDC
                IFDEF   STAGE_ALT_RAM
intv_st         EQU     STAGE_BUFFER+566
                ELSE
intv_st         EQU     $0773   ; +0 ON 状態 (0=OFF)
                ENDC
                IFDEF   STAGE_ALT_RAM
intv_stp        EQU     STAGE_BUFFER+567
                ELSE
intv_stp        EQU     $0774   ; +1 1=STOP / 2=割込処理ルーチン実行中
                ENDC
                IFDEF   STAGE_ALT_RAM
intv_pend       EQU     STAGE_BUFFER+568
                ELSE
intv_pend       EQU     $0775   ; +2 発生保留。周期満了でタイマ割込が +1、
                ENDC
                                ;   分岐時に 0 へ戻す (溜まった分は 1 件に畳む)
                IFDEF   STAGE_ALT_RAM
intv_n          EQU     STAGE_BUFFER+57
                ELSE
intv_n          EQU     $0576   ; 割込間隔 (2B, 秒。0=未設定)
                ENDC
                IFDEF   STAGE_ALT_RAM
intv_sec        EQU     STAGE_BUFFER+118
                ELSE
intv_sec        EQU     $05B3   ; 残り秒数 (2B)。タイマ割込が 1 秒毎に減算
                ENDC
                IFDEF   STAGE_ALT_RAM
intv_sub        EQU     STAGE_BUFFER+120
                ELSE
intv_sub        EQU     $05B5   ; 秒未満の残り tick 数 (2B)。0=計時停止
                ENDC
INTV_TPS        EQU     492     ; 1 秒あたりのタイマ tick 数 (周期 2.03ms 換算)
; --- 機能キー割込ワーク ---
;   参考書籍の KEY(n) ON/OFF/STOP と ON KEY(n) GOSUB を受ける。
;   割り込みマスクの値は参考書籍の
;   Interrupt Control の書式そのもの (下位 10 ビットが PF1〜PF10、1 で許可)。
;   飛び先行番号の表 (key_line) は参考書籍「ON〜GOSUB
;   割り込み用テーブル」の KEY(n) 欄に置く (10 スロット × 2B の「番号で引く
;   常駐アドレス表」)。一括初期化は key_reset (コールドスタートと RUN 毎)。
                IFDEF   STAGE_ALT_RAM
key_line        EQU     STAGE_BUFFER+511
                ELSE
key_line        EQU     $073C   ; KEY(n) の欄 10 件×5B。ON 状態 (+0) / STOP・実行中
                ENDC
                                ;   (+1) / 発生 (+2) / 飛び先行番号 (+3,+4)
                IFDEF   STAGE_ALT_RAM
key_arm         EQU     STAGE_BUFFER+114
                ELSE
key_arm         EQU     $05AF   ; 割り込みマスクの値 (2B)。bit0=PF1 … bit9=PF10 (ON か STOP で 1)
                ENDC
                IFDEF   STAGE_ALT_RAM
key_attn        EQU     STAGE_RUNTIME+105
                ELSE
key_attn        EQU     $01E9   ; ATTENTION の受領控え (1B, 0=未受領)。firq_read が積む
                ENDC
; BREAK フラグ (1B)。参考書籍「BREAK フラグ: この値が 0 でない場合、
;   BREAK キーが押されたことを示します」。$FD04 を読む唯一の窓口 firq_read が中断キーの
;   押下を見たとき非 0 にし、中断の判定 (brk_gate / brk_active) はこの控えを見る。
;   中断を受け付けた brk_stop と、直接モードの行の実行開始 (cmd_direct) で 0 に戻す。
                IFDEF   STAGE_ALT_RAM
BRK_FLAG        EQU     STAGE_FILE+113
                ELSE
BRK_FLAG        EQU     $0313
                ENDC
; 配列作業領域 (RAM)
; 次元数を固定しないため、添字表と次元表は低位 RAM の固定枠ではなく
;   CPU スタックに置き、その先頭アドレスだけをここに持つ。表をスタックへ
;   移したことで、次元数の上限は ARR_MAXDIM まで広げられる。
                IFDEF   STAGE_ALT_RAM
arr_sptr        EQU     STAGE_RUNTIME+21
                ELSE
arr_sptr        EQU     $0195   ; 添字表 (CPU スタック上) の先頭アドレス (2B)。
                ENDC
                                ;   低位アドレスから 最後の添字 → 最初の添字 の順
                IFDEF   STAGE_ALT_RAM
arr_dptr        EQU     STAGE_RUNTIME+23
                ELSE
arr_dptr        EQU     $0197   ; 次元表 (CPU スタック上) の先頭アドレス (2B)。
                ENDC
                                ;   並びはエントリの次元欄と同じ
                IFDEF   STAGE_ALT_RAM
arr_nsub        EQU     STAGE_EXPR+14
                ELSE
arr_nsub        EQU     $0010   ; 添字数 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
arr_xsave       EQU     STAGE_RUNTIME+26
                ELSE
arr_xsave       EQU     $019A   ; 配列解決中の本文ポインタ退避 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
arr_entry       EQU     STAGE_RUNTIME+28
                ELSE
arr_entry       EQU     $019C   ; 見つかった配列エントリ先頭 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
arr_elsz        EQU     STAGE_RUNTIME+30
                ELSE
arr_elsz        EQU     $019E   ; 要素サイズ (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
let_tgt         EQU     STAGE_IO+71
                ELSE
let_tgt         EQU     $00D2   ; 代入先アドレス保持 (2B)。配列要素を指す
                ENDC
                                ;   場合は vars_make_room が配列ブロックを動かす
                                ;   ときに同量ぶん追従させる
                IFDEF   STAGE_ALT_RAM
arr_ndim        EQU     STAGE_IO+75
                ELSE
arr_ndim        EQU     $00D6   ; 次元数 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
arr_idx         EQU     STAGE_IO+73
                ELSE
arr_idx         EQU     $00D4   ; 要素の通し番号 / 要素数の積 の累算 (2B)
                ENDC
                                ;   (ホーナー法の途中結果。D だけでは足りない)
; 図形文の座標格納先ポインタ。「図形文の座標解析の間だけ生きる一時ワーク」
;   なので、配列添字ワーク (arr_*) の並びの直後に置く。
                IFDEF   STAGE_ALT_RAM
ipxy_dst        EQU     STAGE_RUNTIME+36
                ELSE
ipxy_dst        EQU     $01A4   ; 図形座標の格納先ポインタ (2B)
                ENDC
; 次元数の上限。ホーナー法の反復回数と、CPU スタックに積む表の大きさ (2 byte/次元) の
;   両方を縛る。表の畳み込みに使う LEAS/LEAU の符号付 8bit オフセット
;   (2*次元数+4 ≦ 127) に収まる値であること。
;   44 は動作観察で確かめた境界。標準は 44 次元までなら DIM・代入・読出しが
;   成立し、45 次元以上は Subscript Out Of Range になる (このとき標準は
;   ON ERROR で捕まえられず、行番号も報告しない)。よって受理する範囲を 44 に
;   揃え、45 次元以上は Subscript Out Of Range (9) として捕まえられる形で返す。
ARR_MAXDIM      EQU     44
; --- 文字列領域の詰め直し作業変数: 単精度除算/表示のスクラッチと時分割共用 ---
;   詰め直しが走るのは (1) 文の切れ目 (2) 文字列を変数へ格納する瞬間 (3) FRE("")
;   の 3 箇所だけで、いずれも浮動小数の演算は走っていない ($005E-$006A は
;   1 演算の内部でのみ使うスクラッチで、演算をまたいで生きる値は置かれない)。
;   直接ページに置くことで 1 参照あたり 1 byte 短くなる (容量捻出)。
                IFDEF   STAGE_ALT_RAM
gc_dst          EQU     STAGE_NUMBER+0
                ELSE
gc_dst          EQU     $005E   ; 次に詰める先 (2B。このアドレスの 1 つ上が実体末尾+2)
                ENDC
                IFDEF   STAGE_ALT_RAM
gc_bptr         EQU     STAGE_NUMBER+2
                ELSE
gc_bptr         EQU     $0060   ; 今回選んだブロックの実体先頭 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gc_bdsc         EQU     STAGE_NUMBER+4
                ELSE
gc_bdsc         EQU     $0062   ; 同 記述子のアドレス (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gc_blen         EQU     STAGE_NUMBER+6
                ELSE
gc_blen         EQU     $0064   ; 同 長さ (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gc_ent          EQU     STAGE_NUMBER+7
                ELSE
gc_ent          EQU     $0065   ; 配列走査: 次エントリ先頭 (2B)
                ENDC
; $01D1-$01E2 は割込フックスロット (SWI3/SWI2/SWI/NMI/IRQ/FIRQ 各 3B の
;   JMP 置場。time_init が既定 JMP を全 6 スロットへ設置し、RAM ベクタが
;   ここを指す)。作業変数を置くと JMP コードを破壊し、該当割込みで暴走する
;   ほか、常駐機械語がスロットへ植えたハンドラとも相互破壊になる (動作観察: 実在
;   の常駐 ML が $01E0-$01E2 の FIRQ スロットへ JMP を植える)。よって本域には
;   作業変数を一切置かない。乗算・配列名の作業変数は下の ZP 安全域
;   ($0002-$001A) に置く。
; --- ZP 安全域 ($0002-$001A, 25B) ---
;   ディスク起動とテープ読取の全行程を書込み監視で確かめ、外来コード
;   (boot ROM / 常駐側 / 低位 RAM 常駐コード) からの書込ゼロを確認した空き。
;   コールドスタートの clr_zp ($00-$7F) で初期化される。ここに置くのは
;   「文の実行中だけ生きる一時ワーク」に限る ($0000-$0001 は誤った
;   ヌルポインタ書込の着弾点になりやすいため空けておく)。
                IFDEF   STAGE_ALT_RAM
arr_mul_a       EQU     STAGE_EXPR+0
                ELSE
arr_mul_a       EQU     $0002   ; 乗算 被乗数 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
arr_mul_b       EQU     STAGE_EXPR+2
                ELSE
arr_mul_b       EQU     $0004   ; 乗算 乗数 (2B)
                ENDC
; 参照回数の多い VNLEN/arr_nlen を直接ページに置き (1 命令 1 バイト節約)、
;   参照 2 回だけの arr_nptr を低位 RAM 側 ($01CA) へ回す。
                IFDEF   STAGE_ALT_RAM
VNLEN           EQU     STAGE_EXPR+4
                ELSE
VNLEN           EQU     $0006   ; 解析中の名前の有効長 1-16 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
arr_nlen        EQU     STAGE_EXPR+5
                ELSE
arr_nlen        EQU     $0007   ; 配列名退避 有効長 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
arr_vty         EQU     STAGE_EXPR+6
                ELSE
arr_vty         EQU     $0008   ; 配列名退避 type (1B)
                ENDC
; RENUM の作業 (行番号の付け直しの間だけ生きる。図形・配列の一時と時間排他)
                IFDEF   STAGE_ALT_RAM
rn_new          EQU     STAGE_EXPR+0
                ELSE
rn_new          EQU     $0002   ; 新行番号 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
rn_old          EQU     STAGE_EXPR+2
                ELSE
rn_old          EQU     $0004   ; 旧行番号 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
rn_inc          EQU     STAGE_EXPR+4
                ELSE
rn_inc          EQU     $0006   ; 増分値 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
rn_ptr          EQU     STAGE_EXPR+6
                ELSE
rn_ptr          EQU     $0008   ; 10 進の綴りを組み立てる書込位置 (2B)
                ENDC
; GET/PUT(@) ブロック転送 & EXEC 作業変数 (書込可能な低位 RAM に置く。
;  $8000-$FBFF の ROM オーバレイ域は読出時に ROM が見えるため不可)
                IFDEF   STAGE_ALT_RAM
gp_arr_ptr      EQU     STAGE_EXPR+12
                ELSE
gp_arr_ptr      EQU     $000E   ; 配列要素先頭アドレス (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gp_kwfn         EQU     STAGE_RUNTIME+72
                ELSE
gp_kwfn         EQU     $01C8   ; 機能キーワード指定フラグ (1B)。PUT@ の 2 番目の
                ENDC
                                ;   引数が機能語 (PSET/PRESET/AND/OR/XOR) だったとき
                                ;   だけ非 0 になる。参考書籍の PUT@ は
                                ;   形式1/2 が「機能を伴わないキャラクタ矩形の書込」、
                                ;   形式3/4 が「機能を伴う図形の書込」で、2 番目の
                                ;   引数の種別 (機能語かパレットコードか) だけが
                                ;   両者を分ける。引数の個数では分けられない
                                ;   (形式1 も 2 番目にパレットコードを採れる)。
                IFDEF   STAGE_ALT_RAM
gp_func         EQU     STAGE_IO+81
                ELSE
gp_func         EQU     $00DC   ; 機能/対象色 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gp_optn         EQU     STAGE_IO+32
                ELSE
gp_optn         EQU     $00AB   ; オプション引数カウンタ (1B)
                ENDC
gp_pl           EQU     gp_optn ; PUT@ 残プレーン数 (1B)。引数解析後の gp_optn を転用
                IFDEF   STAGE_ALT_RAM
gp_color        EQU     STAGE_BUFFER+174
                ELSE
gp_color        EQU     $05EB   ; PUT@ 色引数 (既定 = cur_fg_color) (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gp_attr         EQU     STAGE_IO+76
                ELSE
gp_attr         EQU     $00D7   ; GET@A/PUT@A (形式2/4) フラグ (0=形式1/3, 非0=@A 3プレーン)
                ENDC
; PUT@ の大型矩形分割転送 (チャンク) 用ワーク
; 残行数/行数の 3 バイトは ZP 安全域から FAC2 の内側 ($0084-$0086)
;   へ移した。チャンク走行 (引数解析が終わった後の転送ループ) は式評価も浮動
;   小数演算も一切行わないため FAC2 は死んでおり、時分割共用して安全である。
;   空いた $0009-$000A / $000D は文字列レジスタ (STRPTR/STRLEN) が使う。
;   gp_cury は $000B-$000C に残す。引数解析中は gp_arr_base として生きており
;     (= 式評価を跨ぐ)、FAC2 の内側へは置けないため。
                IFDEF   STAGE_ALT_RAM
gp_rows_rem     EQU     STAGE_NUMBER+38
                ELSE
gp_rows_rem     EQU     $0084   ; 残行数 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gp_rows_per     EQU     STAGE_NUMBER+39
                ELSE
gp_rows_per     EQU     $0085   ; 1 コマンドで送れる行数 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gp_cury         EQU     STAGE_EXPR+9
                ELSE
gp_cury         EQU     $000B   ; 現在チャンクの Y0 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gp_rows_this    EQU     STAGE_NUMBER+40
                ELSE
gp_rows_this    EQU     $0086   ; 現在チャンクの行数 (1B)
                ENDC
; 引数解析開始時点の ARYTAB を退避する枠 (2B)。オプション引数の式評価が
;   未定義変数を新規作成するとスカラ域が伸び、配列ブロック全体が後方へ移動する。
;   解析中に確定させた gp_arr_ptr はその瞬間に陳腐化するため、送信直前に
;   「記録時 ARYTAB からのオフセット」を現在の ARYTAB へ再結合して復元する。
;   gp_cury は PUT@ のチャンク走行専用で、引数解析中は未使用のため借用する
;   (再結合は igp_common の末尾で完了し、チャンク走行はその後に始まる)。
gp_arr_base     EQU     gp_cury
; GET@A/PUT@A (3 プレーン) 分割転送ワーク。
;   重要: $01D1-$01E2 (割込フックスロット) は使わない。例えば IRQ_HOOK
;     ($01DD、メイン IRQ 入口の JMP 3B) で、長時間走る @A ブリット中にタイマ
;     IRQ が発生すると、ここへ書いた値を飛び先ベクタとして実行し暴走する。
;     @A ブリット中は未使用の浮動小数点スクラッチ (fsc_*、str_work_buf+$A0
;     = $03DA) を借用する。ブリットは式評価 (float) を一切行わず、
;     IRQ/送信ハンドラもこの域を触らないため安全。
gpa_psz         EQU     str_work_buf+$A0   ; planeSize = fullBpr*fullRows (2B)
gpa_srcB        EQU     str_work_buf+$A2   ; 現ストリップの B プレーン源ポインタ (走行) (2B)
gpa_curY        EQU     str_work_buf+$A4   ; 現在の絶対 Y 座標 (2B)
gpa_rleft       EQU     str_work_buf+$A6   ; 残り行数 (1B)
gpa_cleft       EQU     str_work_buf+$A7   ; 現在行の残りバイト列数 (1B)
gpa_sx0         EQU     str_work_buf+$A8   ; 現ストリップの X0 (2B)
gpa_cthis       EQU     str_work_buf+$AA   ; 現ストリップのバイト列数 C (1B)
; PUT@ / PUT@A 共通チャンク送出 (ビット位相つき) の作業枠。
;   配列上のパターンは行末でバイト境界へ切上げないビット連続詰めなので、幅が
;   8 の倍数でない矩形はチャンク先頭がバイト境界に載らない。位相を持ち回り、
;   複写時に左詰めしてから共有 RAM のデータ領域へ積む。@A は 3 プレーンを同じ位相で積む。
;   配置は ZP 安全域に限る。$01A0-$01FF はテープ起動ソフトが自前の作業域と
;     して使う可能性がある区画であり、形式1/3 の転送で新たに踏むと常駐側を壊す。
;     配列名/乗算スクラッチ ($0002-$0008) は引数解析が終わった時点で死んでおり、
;     チャンク走行中は誰も参照しないため転用する。
gp_w            EQU     arr_mul_a   ; 矩形の幅 W (2B)
gp_bits         EQU     arr_mul_b   ; チャンクのビット数 (2B)
gp_bitofs       EQU     VNLEN       ; パターン読出のビット位相 0-7 (2B)
                                        ;   ($0006 VNLEN / $0007 arr_nlen は名前解析用で、
                                        ;    引数解析が終わったチャンク走行中は誰も参照しない)
gp_nb           EQU     arr_vty     ; 1 プレーンぶんの送出バイト数 (1B)
; FILES (ディレクトリ一覧) 用 作業 RAM (1B, $01C9)。走査中セクタ番号を保持する。
;   gp_attr ($01C8) 直後の空き領域。FILES 実行時のみ使用 (他機能と非同時)。
                IFDEF   STAGE_ALT_RAM
files_blk       EQU     STAGE_RUNTIME+73
                ELSE
files_blk       EQU     $01C9
                ENDC
; EXEC 中にディスク入出力を行う利用形態では、実行中の文であってもディスク
;   起動と同種の緩衝域が踏まれ得る。よって EXEC 系の作業変数はそうした
;   緩衝域に置かない (実行開始アドレスは EXEC_ADDR $0217 が持つ)。
                IFDEF   STAGE_ALT_RAM
iexec_resume    EQU     STAGE_EXEC+256
                ELSE
iexec_resume    EQU     $043A   ; EXEC 復帰後の BASIC 本文位置 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
vnw_size        EQU     STAGE_EXPR+15
                ELSE
vnw_size        EQU     $0011   ; vars_make_room: 新スカラエントリサイズ (1B)
                ENDC
                                ;   ここは「式評価の最中に書かれる」ワークである
                                ;   (未定義変数への参照が新エントリを作るため)。文字列
                                ;   レジスタの置き場としては使えない。
; 生存期間が文をまたぐ変数を、ディスク起動やテープ入出力が丸ごと書き潰す
;   緩衝域には置いてはならない。この域へ RUNLIN / pc_capt / pc_capptr を
;   置くと踏まれて pc_capt が非 0 になり、全コンソール出力が捕捉モードへ
;   落ちてディスク起動が対話ループから抜けられなくなる。よって現在は
;   RUNLIN ($0311) / pc_capt ($01EC) / pc_capptr ($01ED) / iexec_resume
;   ($043A) / mpage_cur ($05DD) をそうした緩衝域と重ならない位置に置く。
;   $00F7-$00FF も空きに見えるが、boot ROM と常駐低位 RAM コードが多数
;   書き込む常用作業域のため使用禁止。
                IFDEF   STAGE_ALT_RAM
mpage_cur       EQU     STAGE_BUFFER+160
                ELSE
mpage_cur       EQU     $05DD   ; SCREEN 文の面指定の控え (1B)。重ね合わせ表示制御
                ENDC
                                ;   レジスタ ($FD37) は書込専用で読めないため、
                                ;   最後に書いた値をここに保持する。片側だけを
                                ;   指定した SCREEN が他方を保つために要る。
                                ;   コールドスタートの init_ram_vars が 0 に初期化。
                IFDEF   STAGE_ALT_RAM
RUNLIN          EQU     STAGE_FILE+111
                ELSE
RUNLIN          EQU     $0311   ; RUN 開始行番号 (2B)。$FFFF=指定なし(先頭から)。
                ENDC
                                ;   impl_run が設定し iep_start_x が消費時に $FFFF へ戻す。
                IFDEF   STAGE_ALT_RAM
pc_capt         EQU     STAGE_NUMBER+13
                ELSE
pc_capt         EQU     $006B   ; impl_putchar 捕捉モード (1B)。非0=コンソールへ送らず
                ENDC
                                ;   pc_capptr の指す RAM へ追記 (STR$ の単精度文字列化用)。
                IFDEF   STAGE_ALT_RAM
pc_capptr       EQU     STAGE_RUNTIME+109
                ELSE
pc_capptr       EQU     $01ED   ; putchar 捕捉書込ポインタ (2B)
                ENDC
strf_buf        EQU     str_work_buf+$C0   ; STR$ 単精度用 捕捉バッファ (16B = sym_buf+66)。
                                ;   sym_buf 末尾と排他共用 (SYMBOL 実行中に STR$ 結果は
                                ;   生存しないため衝突しない)。
; --- 単精度 超越関数 (SIN/COS/TAN/SQR) 用 FAC 一時 (str_work_buf+$A0) ---
;   float コア (fmul/fadd/fdiv) を跨いで値を保持するための 6B 展開形 FAC 退避。
;   コア群のスクラッチ $43-$4F とは非重複。
;   1 バイトの fsc_flag だけは参照が多いので直接ページへ移した (専有)。
fsc_x           EQU     str_work_buf+$A0   ; 引数/被開平数 保存 (6B)
fsc_r           EQU     str_work_buf+$A6   ; r / ニュートン近似 y (6B)
fsc_s           EQU     str_work_buf+$AC   ; s = r*r (6B)
                IFDEF   STAGE_ALT_RAM
fsc_flag        EQU     STAGE_IO+17
                ELSE
fsc_flag        EQU     $009C   ; 象限パリティフラグ (1B)
                ENDC
fsc_t           EQU     str_work_buf+$B3   ; TAN 用 角度保存 (6B)
fsc_u           EQU     str_work_buf+$B9   ; TAN 用 sin 値保存 (6B)

; --- 倍精度演算作業域: 仮数は fsc_* と時間排他で共用 ---
;   倍精度コア (dfadd/dfmul/dfdiv) は 1 演算の内部でのみ使用し、超越関数は
;   引数入口で単精度へ落ちる (dbl_demote) ため、fsc_* との生存期間は重ならない。
;   仮数レジスタは 8B (56bit 仮数 + 下位 8bit ガード, bit63=先頭 1)。
;   [dx_m|dw1_m] は 16B 連続 (乗算の 128bit 積シフトで前提) なので
;   str_work_buf の中に置いたままにする。指数と符号は参照が多く、連続で
;   ある必要も無いので直接ページへ移した (いずれも専有。共用しない)。
                IFDEF   STAGE_ALT_RAM
dw1_exp         EQU     STAGE_IO+67
                ELSE
dw1_exp         EQU     $00CE   ; 左/結果オペランド 指数 (16bit 符号付き, 0=値零) 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
dw1_sgn         EQU     STAGE_EXPR+16
                ELSE
dw1_sgn         EQU     $0012   ; 同 符号 (bit7) 1B
                ENDC
dx_m            EQU     str_work_buf+$A3   ; 積上位/商/表示 N 作業 8B
dw1_m           EQU     str_work_buf+$AB   ; 左/結果 仮数 8B
                IFDEF   STAGE_ALT_RAM
dw2_exp         EQU     STAGE_EXPR+17
                ELSE
dw2_exp         EQU     $0013   ; 右オペランド 指数 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
dw2_sgn         EQU     STAGE_IO+82
                ELSE
dw2_sgn         EQU     $00DD   ; 同 符号 1B
                ENDC
dw2_m           EQU     str_work_buf+$B6   ; 同 仮数 8B
d_cnt           EQU     str_work_buf+$BE   ; 反復カウンタ 1B

; --- 倍精度リテラル解析スクラッチ: 単精度 pn_*/fdv_* ($43-$4F) と時間排他 ---
                IFDEF   STAGE_ALT_RAM
pd_mant         EQU     STAGE_NUMBER+0
                ELSE
pd_mant         EQU     $005E   ; 十進仮数 64bit 累算 8B ($43-$4A)
                ENDC
                IFDEF   STAGE_ALT_RAM
pd_sat          EQU     STAGE_NUMBER+8
                ELSE
pd_sat          EQU     $0066   ; 仮数飽和フラグ 1B
                ENDC

; --- 倍精度 表示スクラッチ: 文字列連結バッファ後部を借用 (pfe_dig と同流儀) ---
dp_dig          EQU     str_work_buf+$10  ; 有効16桁バッファ (, MSB 先頭)
dp_cnt          EQU     str_work_buf+$20  ; 十進シフトカウンタ 1B (符号付き, $035A)
                IFDEF   STAGE_ALT_RAM
dp_tmp          EQU     STAGE_PTR+38
                ELSE
dp_tmp          EQU     $0058   ; 丸めビット/末尾ゼロ数 一時 1B (参照が多いので
                ENDC
                                ;   直接ページへ移した。専有)
strf_dbuf       EQU     str_work_buf+$30  ; STR$(倍精度) 捕捉バッファ (25B)

                IFDEF   STAGE_ALT_RAM
MLBUF           EQU     STAGE_BUFFER+176
                ELSE
MLBUF           EQU     $05ED   ; テープの機械語ブロック緩衝 (255B。参考書籍
                ENDC
                                ;   「CAS0 入出力バッファ」$05ED-$06EB。LOADM / 常駐ローダ入力)
                IFDEF   STAGE_ALT_RAM
IIL_BUF         EQU     STAGE_BUFFER+176
                ELSE
IIL_BUF         EQU     $05ED   ; SAVE のブロック組立バッファ (128B。同じ CAS0 入出力バッファ)。
                ENDC
                                ;   行挿入 (impl_insert_line) は呼出元バッファ
                                ;   (INPBUF / str_work_buf, 256B) 上で in-place トークン化
                                ;   する方式へ変更したため、本バッファは使わなくなった。
                                ;   ASCII セーブのテキストは行長に依らずここへ 128B ずつ
                                ;   詰めてブロック書出しする (7t_save_list.inc)
; FAC1: 浮動小数点アキュムレータ (内部形式 倍精度 8B): $50=指数,$51-$57=仮数)
;   参考書籍「FAC1」$0074-$007C の位置 (展開形 8B は当方のもの)。
                IFDEF   STAGE_ALT_RAM
FAC1            EQU     STAGE_NUMBER+22
                ELSE
FAC1            EQU     $0074
                ENDC
; FAC2: 第2オペランド (内部形式 倍精度 8B)。参考書籍
;   「FAC2」$0082-$008A の位置。
                IFDEF   STAGE_ALT_RAM
FAC2            EQU     STAGE_NUMBER+36
                ELSE
FAC2            EQU     $0082
                ENDC
; 文字列レジスタ
; 文字列レジスタは第 2 オペランド FAC2 ($0082-$0089, 8B) の外に置く。
;   内側へ置くと次の壊れ方をする。二項演算は左辺を
;   pop_to_fac2 で FAC2 へ復元するため、文字列を保持したまま数式を評価すると
;   (FAC2 域への書込で) 文字列レジスタが必ず壊れる。
;   例: MID$(A$,I+1,1) は A$ を STRPTR へ載せた後に I+1 を評価するので、
;   加算の時点で STRPTR/STRLEN が失われ結果が空になっていた。
;   よって文字列レジスタを ZP 安全域 ($0002-$001A) へ退避し、FAC1 ($0074-$007B)・
;   FAC2 ($0082-$0089) および浮動小数スクラッチ ($005E-$006A) とアドレスを
;   一切共有しない配置にする。
;   ZP 安全域の同居者 (gp_* / escat_save_x / p_*) はいずれも文の実行中だけ
;   生きるワークで、式評価の最中には書かれない。
;   置き場は ZP 安全域のうち「式評価の最中に決して書かれない」3 バイト
;   ($0009-$000A / $000D) とする。ここは PUT@/GET@ のチャンク走行ワーク
;   (gp_rows_*) が居た跡地で、その 3 つは FAC2 の内側 ($0084-$0086) へ移した
;   (チャンク走行は式評価も浮動小数演算も一切行わないため FAC2 は死んでいる)。
;   $000B-$000C (gp_cury) と $0015 (vnw_size) は使えない。前者は引数解析中
;     gp_arr_base として生き、後者は未定義変数の新規作成で書かれるため、
;     どちらも「式評価の最中に書かれる」バイトである。
                IFDEF   STAGE_ALT_RAM
STRPTR          EQU     STAGE_EXPR+7
                ELSE
STRPTR          EQU     $0009   ; 文字列先頭アドレス (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
STRLEN          EQU     STAGE_EXPR+11
                ELSE
STRLEN          EQU     $000D   ; 文字列長 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
div_qsign       EQU     STAGE_NUMBER+42
                ELSE
div_qsign       EQU     $0088   ; div16s: 商の符号作業 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
div_rsign       EQU     STAGE_NUMBER+43
                ELSE
div_rsign       EQU     $0089   ; div16s: 余りの符号作業 1B
                ENDC
; MID$/STRING$ の要求長・埋め文字。div スクラッチと共用 (= FAC2 の内側) なので、
;   引数の式評価を跨いで保持してはならない。跨ぐ値 (開始位置・繰返し数) は
;   ハードウェアスタックに置くこと。
                IFDEF   STAGE_ALT_RAM
mid_len         EQU     STAGE_NUMBER+43
                ELSE
mid_len         EQU     $0089   ; MID$: 要求長 / STRING$: 埋め文字
                ENDC
                IFDEF   STAGE_ALT_RAM
TMPSTR          EQU     STAGE_RUNTIME+35
                ELSE
TMPSTR          EQU     $01A3   ; 一時1文字バッファ
                ENDC
; 型タグ (0=整数,1=単精度,2=倍精度,3=文字列)。参考書籍
;   「FAC1 の型」の位置 (値の符号化は当方のもの)。
                IFDEF   STAGE_ALT_RAM
FACTYPE         EQU     STAGE_EXPR+21
                ELSE
FACTYPE         EQU     $0017
                ENDC
; 左オペランド (FAC2) の型タグ。四則/比較の型二重パスで pop_to_fac2 が設定し、
; 各 impl_fp_* が「両オペランド整数なら整数パス(ビット一致)/片方でも単精度なら
; float 昇格」を判定するのに使う。FAC2 の直前の空き DP バイトを転用。
                IFDEF   STAGE_ALT_RAM
FAC2TYPE        EQU     STAGE_PTR+43
                ELSE
FAC2TYPE        EQU     $005D
                ENDC
; 小数リテラル解析 pn_decimal 用の 10 進スケール指数 (符号付き 8bit)。
;   parse 中のみ使用 (float コアは $43-$4F を使うため、こちらは別バイト $005B)。
                IFDEF   STAGE_ALT_RAM
pn_dexp         EQU     STAGE_PTR+41
                ELSE
pn_dexp         EQU     $005B
                ENDC
; 数値リテラル解析: 10 進仮数 32bit 累算とフラグ。桁累算フェーズ (float
;   コア呼出し前) のみ使用のため、fdv_* スクラッチ $43-$48 と時分割で共用する。
                IFDEF   STAGE_ALT_RAM
pn_mant         EQU     STAGE_NUMBER+0
                ELSE
pn_mant         EQU     $005E   ; 10進仮数 32bit 累算 (4B, fdv_m1 と時分割)
                ENDC
                IFDEF   STAGE_ALT_RAM
pn_msat         EQU     STAGE_NUMBER+4
                ELSE
pn_msat         EQU     $0062   ; 仮数飽和フラグ 1B (fdv_rem と時分割)
                ENDC
                IFDEF   STAGE_ALT_RAM
pn_isflt        EQU     STAGE_NUMBER+5
                ELSE
pn_isflt        EQU     $0063   ; 小数点/指数の出現フラグ 1B (同上)
                ENDC
; print_float 小さい側 (|v|<0.01) の固定/E形式判定用 FAC1 退避 (8B)。
;   print 中のみ使用のため文字列連結作業域の後半を時分割で共用する。
pf_vsave        EQU     str_work_buf+$08   ; str_work_buf+8 (pfe_cnt/pfe_dig の直後)
; 変数参照解決の作業変数
;   名前は本文 (プログラム/入力緩衝) 上に在るものをそのまま指し、複写しない。
;   有効長は先頭 16 文字 (17 文字目以降は読み飛ばすだけで名前に含めない)。
;   これで 16 バイトの名前緩衝を持たずに済み、比較も本文同士の直接比較になる。
                IFDEF   STAGE_ALT_RAM
VNPTR           EQU     STAGE_NUMBER+14
                ELSE
VNPTR           EQU     $006C   ; 名前の本文上の先頭アドレス (2B)
                ENDC
VN_MAXLEN       EQU     16      ; 有効文字数 (標準構成の動作観察による値)
; READ/DATA: 現在走査中の DATA 行レコード先頭 (2B)。参考書籍
;   「DATA 文行番号」の位置 (行番号はレコードの +2)。
                IFDEF   STAGE_ALT_RAM
DATLINE         EQU     STAGE_PTR+31
                ELSE
DATLINE         EQU     $0051
                ENDC
                IFDEF   STAGE_ALT_RAM
div_div         EQU     STAGE_NUMBER+16
                ELSE
div_div         EQU     $006E   ; div16s: |除数| 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
div_num         EQU     STAGE_NUMBER+18
                ELSE
div_num         EQU     $0070   ; div16s: |被除数|/商 作業 2B
                ENDC
; 変数型デフォルト表 (A-Z 各1B: 0=sng,1=int,2=dbl,3=str)。
;   参考書籍の「型宣言テーブル」の位置 (値の符号化は当方のもの)。
                IFDEF   STAGE_ALT_RAM
DEFTYPE         EQU     STAGE_FILE+125
                ELSE
DEFTYPE         EQU     $031F   ; 26B
                ENDC
; 副手続き (GOSUB) / 繰り返し (FOR) / WHILE の枠と直接モードの行は、システム
;   スタックの上に積む (下の「システムスタック上の枠」)。積み上げ量の変数は無い。
                IFDEF   STAGE_ALT_RAM
PRINT_COL       EQU     STAGE_PTR+0
                ELSE
PRINT_COL       EQU     $0032   ; PRINT 現在カラム (0-79)
                ENDC
                IFDEF   STAGE_ALT_RAM
ONERROR_LIN     EQU     STAGE_IO+41
                ELSE
ONERROR_LIN     EQU     $00B4   ; 誤り捕捉の飛び先行番号 (2B。参考書籍
                ENDC
                                ;   「エラートラップ行」の位置。値は行番号)
                IFDEF   STAGE_ALT_RAM
ONERROR_EN      EQU     STAGE_IO+38
                ELSE
ONERROR_EN      EQU     $00B1   ; ON ERROR 状態 (0=無効/1=有効/2=ハンドラ実行中。
                ENDC
                                ;   参考書籍「エラー処理中フラグ」の位置。
                                ;   値の符号化は当方のもの)
;------------------------------------------------------------------------------
; システムスタック上の枠 (参考書籍の「未使用領域」の
;   中で、システムスタックの上端は文字領域の下限 STRBOT)。
;   副手続き (GOSUB) / 繰り返し (FOR) / WHILE の情報と直接モードの行は、
;   システムスタックの上に枠として積む。段数の上限は無く、未使用領域が尽きれば
;   Out Of Memory (参考書籍 WHILE〜WEND「メモリの許される範囲で
;   何重にでもネストできます」)。
;   枠は EXEC_SP (文の切れ目のシステム S = 枠列の先頭) から STRBOT まで並ぶ。
;   各枠の先頭 1 バイトが種別。
;     TAG_GOSUB  5B  [種別][復帰 IP 2][復帰行レコード 2]
;     TAG_WHILE  5B  [種別][WHILE 文の IP 2][行レコード 2]
;     TAG_FOR   15B  [種別][制御変数 2][終値 4][増分 4][本体 IP 2][行レコード 2]
;     $FF        5+n [$FF][原文の長さ n][$FF $FF][原文 n バイト][$00]
;                = 直接モードの行の擬似行レコード (行番号 $FFFF の行レコードと
;                  同じ形なので、実行用行バッファへの変換はプログラム行と共通)
;   枠の出し入れ (frame_ins / frame_del) は S〜EXEC_SP の間にある呼出フレームを
;   そのぶんずらすので、枠を積む文も普通のサブルーチンとして書ける。
;------------------------------------------------------------------------------
TAG_GOSUB       EQU     1
TAG_WHILE       EQU     2
TAG_FOR         EQU     3
FRM_SMALL       EQU     5       ; GOSUB / WHILE の枠の大きさ
FRM_FOR         EQU     15      ; FOR の枠の大きさ
                IFDEF   STAGE_ALT_RAM
gcu_rp          EQU     STAGE_FILE+116
                ELSE
gcu_rp          EQU     $0316   ; GCURSOR: 応答の読出し位置 (2B)。$1F の応答は
                ENDC
                                ;   ステージング先頭から 1 点 4 byte で並ぶ。読出し位置を
                                ;   レジスタで持つと、座標の受け皿が配列要素だったときに
                                ;   添字式の評価で壊れるため RAM に置く。
; 現在の表示桁数 (1B)。起動時 40、WIDTH の桁数指定に追随する。
;   CONSOLE のスクロール範囲指定はコンソールイニシャライズで送るが、その
;   コマンドは桁数と行数も必須の引数に持つ。桁数はサブシステム側の作業域に
;   しか無くメイン CPU から読めないため、範囲指定のたびに現在値を添えられるよう
;   BASIC 側にも控えを持つ (控えが無いと固定値を送ることになり、40 桁で走行中に
;   範囲を指定すると桁数まで書き換わってしまう)。
;   置き場所は参考書籍の「画面桁数」の位置 ($00C3) に合わせる。
                IFDEF   STAGE_ALT_RAM
con_cols        EQU     STAGE_IO+56
                ELSE
con_cols        EQU     $00C3   ; 現在の表示桁数 (1B, 40/80)
                ENDC
; 現在のスクロール範囲 (2B, 隣接。上端行 / 行数)。起動時 0 行目から CON_ROWS 行
;   (= 画面全体)。CONSOLE は省略した引数について「現在の設定を保つ」ので
;   (参考書籍「CONSOLE 文」)、省略時の既定値をここから採る。
;   WIDTH は画面全体へ戻す。con_init_send が送出のたびに書き込む。
                IFDEF   STAGE_ALT_RAM
con_wtop        EQU     STAGE_FILE+108
                ELSE
con_wtop        EQU     $030E   ; スクロール範囲 開始行 (1B。参考書籍)
                ENDC
                IFDEF   STAGE_ALT_RAM
con_wend        EQU     STAGE_FILE+109
                ELSE
con_wend        EQU     $030F   ; スクロール範囲 終了行 (1B。同上。con_wtop と隣接させること)
                ENDC
; CONSOLE の第 3・第 4 引数の現在値 (2B、隣接。省略時の既定値と、コンソール
;   イニシャライズ ($01) の相対 8 として送る値と、文字色を緑に固定する印)。
;   コールドスタートで impl_sub_init が 0 に初期化する。
;   con_pf の置き場所は参考書籍の「ファンクションキー表示フラグ」
;   ($0310。0=非表示 / 0 以外=表示) に合わせる。
                IFDEF   STAGE_ALT_RAM
con_pf          EQU     STAGE_FILE+110
                ELSE
con_pf          EQU     $0310   ; 機能キー行の表示指定 (1B。0=表示しない)
                ENDC
                IFDEF   STAGE_ALT_RAM
con_mono        EQU     STAGE_BUFFER+112
                ELSE
con_mono        EQU     $05AD   ; 単色表示の指定 (1B。0=COLOR の色 / 非 0=文字色を緑のみに。con_pf と隣接)
                ENDC
; TAB 停止位置の表 (10B = 80 桁ぶんのビット列。各バイトの bit7 が左の桁)。コールド
;   スタートで 8 桁ごとにして TAB SET ($0B) で送る (参考書籍
;   「画面表示関係初期化」「TAB セット」)。置き場所は参考書籍「TAB 停止位置」に合わせる。
                IFDEF   STAGE_ALT_RAM
tab_map         EQU     STAGE_FILE+0
                ELSE
tab_map         EQU     $02A2   ; TAB 停止位置 (10B。参考書籍)
                ENDC
; --- 拡張のためのフック ($022D-$02A1) ---
;   参考書籍「拡張のためのフック」。いずれも通常は RTS で、
;   コールドスタートが $39 (RTS) を敷く。本 ROM が呼ぶのは
;   ステートメント実行フックだけである (MON / TERM / EXTIRQ は未実装)。
                IFNDEF  STAGE_ALT_RAM
HOOK_STMT       EQU     $0263   ; ステートメント実行フック (文の切れ目ごとに JSR)
                ENDC
                IFNDEF  STAGE_ALT_RAM
HOOK_MON        EQU     $0269   ; MON コマンドフック
                ENDC
                IFNDEF  STAGE_ALT_RAM
HOOK_TERM       EQU     $0284   ; TERM コマンドフック
                ENDC
; --- デバイス登録テーブル・ルーチンベクトル (参考書籍) ---
;   DEVTBL は物理機番 0〜15 の 16 件で、各件はそのデバイスのルーチンベクトルの
;   あるアドレス (0 = その物理機番は使用しない)。物理機番は参考書籍
;   「物理機番対応表」の KYBD:=$00 / SCRN:=$01 / CAS0:=$02 / LPT0:=$03 /
;   COM0:=$04 / フロッピーディスク 0〜3 = $80〜$83 (DEVTBL の 16 件には載らない)。
;   ルーチンベクトルはデバイス名に続いて OPEN / CLOSE / 1 バイト入力 /
;   1 バイト出力 / POS / EOF / LOF の 7 アドレスを持つ。LPT0 のものだけは
;   置き場所が $0778-$078C と決まっており、21 バイト = デバイス名 7 バイト +
;   7 アドレス×2 バイトになる。他のデバイスのベクタは本 ROM 内に置く。
;   LOGPHY は論理機番 0〜17 の 18 件で、1 バイトの bit4-0 が物理機番、
;   bit5 が入力モード、bit6 が出力モードを表す (0 = 未使用)。
                IFDEF   STAGE_ALT_RAM
DEVTBL_PTR      EQU     STAGE_FILE+16
                ELSE
DEVTBL_PTR      EQU     $02B2   ; デバイス登録テーブルのアドレス (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
LOGPHY_PTR      EQU     STAGE_FILE+18
                ELSE
LOGPHY_PTR      EQU     $02B4   ; 論理機番/物理機番対応テーブルのアドレス (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
EVTBL_PTR       EQU     STAGE_FILE+20
                ELSE
EVTBL_PTR       EQU     $02B6   ; ON〜GOSUB 割込テーブルのポインタ 5 件×2B
                ENDC
                IFDEF   STAGE_ALT_RAM
COMTBL          EQU     STAGE_FILE+30
                ELSE
COMTBL          EQU     $02C0   ; COMn 登録テーブル (COM0〜COM4 の 5 件×2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
LPTVEC_PTR      EQU     STAGE_BUFFER+109
                ELSE
LPTVEC_PTR      EQU     $05AA   ; LPT0 ルーチンベクトルのアドレス (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
DEVTBL          EQU     STAGE_BUFFER+431
                ELSE
DEVTBL          EQU     $06EC   ; 入出力デバイス登録テーブル (16 件×2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
LOGPHY          EQU     STAGE_BUFFER+463
                ELSE
LOGPHY          EQU     $070C   ; 論理機番/物理機番対応テーブル (18B)
                ENDC
                IFDEF   STAGE_ALT_RAM
LPTVEC          EQU     STAGE_BUFFER+571
                ELSE
LPTVEC          EQU     $0778   ; LPT0 ルーチンベクトル (21B)
                ENDC
; --- CAS0 エラー種別・ファイル関係 (参考書籍) ---
                IFDEF   STAGE_ALT_RAM
CAS0_ERR        EQU     STAGE_FILE+51
                ELSE
CAS0_ERR        EQU     $02D5   ; CAS0 エラー種別 0=なし 1=検査和エラー 2=読み込みエラー
                ENDC
                IFDEF   STAGE_ALT_RAM
FIL_TYPE        EQU     STAGE_FILE+57
                ELSE
FIL_TYPE        EQU     $02DB   ; ファイルタイプ 0=BASIC のソース 1=BASIC のデータ 2=機械語
                ENDC
                IFDEF   STAGE_ALT_RAM
FIL_ASCII       EQU     STAGE_FILE+58
                ELSE
FIL_ASCII       EQU     $02DC   ; アスキーフラグ $00=バイナリ $FF=アスキー
                ENDC
; 保護状態 (参考書籍 SAVE の P オプション)。0=非保護 / 1=保護。SAVE ,P で立ち、
;   ヘッダ (テープ) とディレクトリ (ディスク) のアスキーフラグを $FF から $FE へ
;   下げて書き出す。LOAD はその値から復元し、保護中の LIST / EDIT は
;   Protected Program (62) となる。NEW とコールドスタートで落とす。
                IFDEF   STAGE_ALT_RAM
prot_flag       EQU     STAGE_RUNTIME+71
                ELSE
prot_flag       EQU     $01C7   ; 保護状態 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
FIL_NLEN        EQU     STAGE_FILE+59
                ELSE
FIL_NLEN        EQU     $02DD   ; ファイル名長 (0〜8)
                ENDC
                IFDEF   STAGE_ALT_RAM
FIL_NAME        EQU     STAGE_FILE+60
                ELSE
FIL_NAME        EQU     $02DE   ; ファイル名 8B (8 文字に満たない分は空白)
                ENDC
                IFDEF   STAGE_ALT_RAM
FIL_PHYS        EQU     STAGE_FILE+68
                ELSE
FIL_PHYS        EQU     $02E6   ; 入出力デバイス物理機番 (参考書籍 物理機番対応表)
                ENDC
                IFDEF   STAGE_ALT_RAM
TMR_REF         EQU     STAGE_FILE+82
                ELSE
TMR_REF         EQU     $02F4   ; タイマ参照時刻 6B (時・分・秒を各 2 けたの ASCII で。
                ENDC
                                ;   参考書籍「$02F4-$02F9」)
                IFDEF   STAGE_ALT_RAM
SYS_DATE        EQU     STAGE_FILE+88
                ELSE
SYS_DATE        EQU     $02FA   ; 日付 6B (年・月・日を各 2 けたの ASCII で。同「$02FA-$02FF」)
                ENDC
                IFDEF   STAGE_ALT_RAM
cmt_motor_st    EQU     STAGE_BUFFER+128
                ELSE
cmt_motor_st    EQU     $05BD   ; カセットのモータ状態の控え (0=停止 / $02=回転)。
                ENDC
                                ;   $FD00 は書き込み専用なので、MOTOR の省略形
                                ;   (反転) のために書いた値をここに残す
; --- 実行用行バッファ (E2) の管理ワーク ---
;   格納は ASCII 原文 (行レコード = [次行ptr 2B][行番号 2B][本文 原文][00])。
;   実行時に到達した 1 行だけを実行用行バッファ (EXECBUF, 256B) へ変換して
;   実行する。置き場所は参考書籍の「中間言語バッファ」
;   ($033A-$043C。キー入力された BASIC テキストを変換したものを格納する場所)。
;   変換は決定的なので、同じ行を再変換すればバッファ内ポインタは同値で有効になる。
                IFDEF   STAGE_ALT_RAM
EXECBUF         EQU     STAGE_EXEC+0
                ELSE
EXECBUF         EQU     $033A   ; 実行用行バッファ (256B。255 文字 + $00 終端)
                ENDC
                IFDEF   STAGE_ALT_RAM
EXECREC         EQU     STAGE_BUFFER+170
                ELSE
EXECREC         EQU     $05E7   ; バッファに変換済みの行レコード先頭 (2B)。
                ENDC
                                ;   0 = 無効 (次の変換要求で必ず再変換する)。
                                ;   本文の編集 (挿入/削除/NEW/LOAD/CLEAR) では
                                ;   cont_clear 経由で必ず 0 へ戻す
                IFDEF   STAGE_ALT_RAM
CONTREC         EQU     STAGE_BUFFER+172
                ELSE
CONTREC         EQU     $05E9   ; CONT 再開位置を含む行レコード先頭 (2B)。
                ENDC
                                ;   CONTPTR (バッファ内ポインタ) と対で記録する
; --- DEF FN (使用者定義関数) 用ワーク ---
;   格納の ASCII 化に伴い、定義スロットは「行レコード先頭 2B + 行内 (バッファ内)
;   オフセット 1B」の 3B になった。呼出時は該当行をバッファへ変換してから
;   オフセット位置を参照する (変換は決定的なのでオフセットは常に有効)。
                IFDEF   STAGE_ALT_RAM
fn_call_argptr  EQU     STAGE_BUFFER+150
                ELSE
fn_call_argptr  EQU     $05D3   ; FN 呼出: 実引数走査ポインタ (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
fn_def_scan     EQU     STAGE_BUFFER+152
                ELSE
fn_def_scan     EQU     $05D5   ; FN 呼出: 定義側パラメータ/本体走査ポインタ (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
fn_nparam       EQU     STAGE_BUFFER+154
                ELSE
fn_nparam       EQU     $05D7   ; FN 呼出: 束縛済みパラメータ数 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
fn_def_rec      EQU     STAGE_BUFFER+155
                ELSE
fn_def_rec      EQU     $05D8   ; FN 呼出: 定義行の行レコード先頭 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
fn_entry_rec    EQU     STAGE_BUFFER+157
                ELSE
fn_entry_rec    EQU     $05DA   ; FN 呼出: 呼出時にバッファへ載っていた行 (2B)。
                ENDC
                                ;   復帰時にこの行を ensure してから戻る
                                ;   (fn_call_argptr〜fn_entry_rec の 9B ($05D3-$05DB) は
                                ;   epr_fn が入れ子用に一括退避する連続域)
                IFDEF   STAGE_ALT_RAM
fn_def_tbl      EQU     STAGE_BUFFER+18
                ELSE
fn_def_tbl      EQU     $054F   ; DEF FN 定義表 (6 スロット×3B =)
                ENDC
                                ;   +0 行レコード先頭 (0=空き) / +2 行内オフセット
                IFDEF   STAGE_ALT_RAM
fn_tmp_ptr      EQU     STAGE_BUFFER+89
                ELSE
fn_tmp_ptr      EQU     $0596   ; FN 名前照合 プローブ複写の先頭 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
fn_tmp_ty       EQU     STAGE_EXEC+258
                ELSE
fn_tmp_ty       EQU     $043C   ; FN 名前照合キー = 型|名前長 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
tok_fold1       EQU     STAGE_RUNTIME+80
                ELSE
tok_fold1       EQU     $01D0   ; 行変換: 照合中の入力 1 文字目 (大文字畳み済) (1B)。
                ENDC
                                ;   予約語走査 (tmk_outer) が語頭比較に使う。
                                ;   走査の呼出しごとに tmk_idx / tmk_outer_first が設定
FN_SLOTSZ       EQU     3       ; 定義スロットの大きさ
; FN 名前照合プローブ (16B)。呼出側/定義側の名前は別々の行に在り、単一の
;   実行用行バッファでは同時に参照できないため、呼出側の名前をここへ複写して
;   から定義側と照合する。名前解析〜表照合の間だけ生きる一時 (式評価を挟まない)
;   なので、文字列連結作業バッファ (str_work_buf) の一部を時間排他で借用する。
fn_probe_buf    EQU     str_work_buf+$E0   ; FN 名前プローブ (16B)
; 恒久ブロック (文字列変数・配列要素が所有する実体) の割付ポインタ。
;   ブロックは FRETOP+1 から上へ並ぶ。初期値 = MEMTOP (= 領域の最上位)。
;   長さ L の確保は FRETOP -= L+2 (標準構成と同じ勘定。上位 2 byte は空ける)。
                IFDEF   STAGE_ALT_RAM
FRETOP          EQU     STAGE_PTR+15
                ELSE
FRETOP          EQU     $0041   ; 恒久ブロックの割付ポインタ (2B。
                ENDC
                                ;   参考書籍の「使用済み文字領域先頭−1」の位置)
; --- 単精度浮動小数 除算/表示 用スクラッチ ($005E-$006A, 13B 連続空き) ---
;   fdiv (除算) と print_float は排他実行のため同一領域を再利用する。
                IFDEF   STAGE_ALT_RAM
fdv_m1          EQU     STAGE_NUMBER+0
                ELSE
fdv_m1          EQU     $005E   ; 除数仮数 4B ($43-$46)
                ENDC
                IFDEF   STAGE_ALT_RAM
fdv_rem         EQU     STAGE_NUMBER+4
                ELSE
fdv_rem         EQU     $0062   ; 剰余(=被除数仮数) 4B ($47-$4A)
                ENDC
                IFDEF   STAGE_ALT_RAM
fdv_q           EQU     STAGE_NUMBER+8
                ELSE
fdv_q           EQU     $0066   ; 商 4B ($4B-$4E)
                ENDC
                IFDEF   STAGE_ALT_RAM
fdv_cnt         EQU     STAGE_NUMBER+12
                ELSE
fdv_cnt         EQU     $006A   ; 反復カウンタ 1B
                ENDC
;   int_q (整数部商ビット) は $43-$4F に空きが無いため, スタックの符号バイト
;   bit0 に格納する (bit7=符号)。fdv_q+3=$4E と競合させないための措置。
; print_float 用 (同 $43-$4F を再利用)
                IFDEF   STAGE_ALT_RAM
pf_ip           EQU     STAGE_NUMBER+0
                ELSE
pf_ip           EQU     $005E   ; 整数部 4B (dacc 上位)
                ENDC
                IFDEF   STAGE_ALT_RAM
pf_frac         EQU     STAGE_NUMBER+4
                ELSE
pf_frac         EQU     $0062   ; 小数部 4B (dacc 下位)
                ENDC
                IFDEF   STAGE_ALT_RAM
pf_dacc         EQU     STAGE_NUMBER+0
                ELSE
pf_dacc         EQU     $005E   ; 64bit 固定小数 (高4=整数部, 低4=小数部)
                ENDC
                IFDEF   STAGE_ALT_RAM
pf_nip          EQU     STAGE_NUMBER+8
                ELSE
pf_nip          EQU     $0066   ; 整数部桁数 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
pf_nfrac        EQU     STAGE_NUMBER+9
                ELSE
pf_nfrac        EQU     $0067   ; 小数部桁数 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
pf_tmp          EQU     STAGE_NUMBER+10
                ELSE
pf_tmp          EQU     $0068   ; fracx10 桁上げ / 出力有無フラグ 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
pf_fcount       EQU     STAGE_NUMBER+11
                ELSE
pf_fcount       EQU     $0069   ; 小数生成カウンタ 1B
                ENDC
pf_fbuf         EQU     str_conv_buf ; 小数桁バッファ ($0600, 9B 流用)
; RND 系列更新用 (同 $43-$4A を再利用。式評価完了後〜値の正規化前のみ生存)
                IFDEF   STAGE_ALT_RAM
rnd_x           EQU     STAGE_NUMBER+0
                ELSE
rnd_x           EQU     $005E   ; 系列状態の作業複製 32bit 4B ($43-$46)
                ENDC
                IFDEF   STAGE_ALT_RAM
rnd_t           EQU     STAGE_NUMBER+4
                ELSE
rnd_t           EQU     $0062   ; 桁送り結果の一時 32bit 4B ($47-$4A)
                ENDC
; print_float 指数表記 (E形式) 用スクラッチ。fmul/fdiv/fcmp が $43-$4F を壊すため,
;   スケール中も保持が要る 十進指数カウンタと桁バッファは str_work_buf を流用する
;   (数値 PRINT の間は文字列連結バッファは非活性で安全)。
pfe_cnt         EQU     str_work_buf        ; 十進指数シフトカウンタ 1B (符号付き)
pfe_dig         EQU     str_work_buf+1      ; 有効6桁バッファ (, MSB先頭)
; --- float 加減 (fadd_core) 用スクラッチ (同 $43-$4F を排他再利用) ---
                IFDEF   STAGE_ALT_RAM
fa_a            EQU     STAGE_NUMBER+0
                ELSE
fa_a            EQU     $005E   ; 大きい指数側 仮数24bit+ガード8bit (4B: $43-$46, $47 予備)
                ENDC
                IFDEF   STAGE_ALT_RAM
fa_b            EQU     STAGE_NUMBER+5
                ELSE
fa_b            EQU     $0063   ; 小さい指数側 仮数24bit+ガード8bit (4B: $48-$4B, $4C 予備)
                ENDC
                IFDEF   STAGE_ALT_RAM
fa_exp          EQU     STAGE_NUMBER+10
                ELSE
fa_exp          EQU     $0068   ; 結果指数 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
fa_sgn          EQU     STAGE_NUMBER+11
                ELSE
fa_sgn          EQU     $0069   ; 結果符号 (bit7) 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
fa_cnt          EQU     STAGE_NUMBER+12
                ELSE
fa_cnt          EQU     $006A   ; シフト/汎用カウンタ 1B
                ENDC
; --- float 乗算 (fmul_core) 用スクラッチ (同 $43-$4F を排他再利用) ---
                IFDEF   STAGE_ALT_RAM
fm_phi          EQU     STAGE_NUMBER+0
                ELSE
fm_phi          EQU     $005E   ; 積 上位32bit (4B: $43-$46)
                ENDC
                IFDEF   STAGE_ALT_RAM
fm_plo          EQU     STAGE_NUMBER+4
                ELSE
fm_plo          EQU     $0062   ; 積 下位32bit / 初期は M1 (4B: $47-$4A)
                ENDC
                IFDEF   STAGE_ALT_RAM
fm_m2           EQU     STAGE_NUMBER+8
                ELSE
fm_m2           EQU     $0066   ; 被乗数 M2 (4B: $4B-$4E)
                ENDC
                IFDEF   STAGE_ALT_RAM
fm_cnt          EQU     STAGE_NUMBER+12
                ELSE
fm_cnt          EQU     $006A   ; 反復カウンタ 1B
                ENDC
; push_fac1/pop_to_fac2 が戻りアドレスを一時保持する (Y を壊さないため)。
;   set→JMP 間だけの寿命で他と非重複 ($45-$46 を transient 流用)。
                IFDEF   STAGE_ALT_RAM
flt_ret         EQU     STAGE_NUMBER+2
                ELSE
flt_ret         EQU     $0060
                ENDC
; CMT ワークエリア (RAM $0084-$00AD: 既存ワークと重複しない空きエリア)
                IFDEF   STAGE_ALT_RAM
cmt_filename    EQU     STAGE_RUNTIME+0
                ELSE
cmt_filename    EQU     $0180   ; ファイル名バッファ 17B
                ENDC
                IFDEF   STAGE_ALT_RAM
cmt_blk_type    EQU     STAGE_NUMBER+31
                ELSE
cmt_blk_type    EQU     $007D   ; 読んだブロックタイプ (0=ヘッダ,1=データ,2=終端)
                ENDC
                IFDEF   STAGE_ALT_RAM
cmt_blk_len     EQU     STAGE_NUMBER+32
                ELSE
cmt_blk_len     EQU     $007E   ; ブロックデータ長
                ENDC
                IFDEF   STAGE_ALT_RAM
cmt_load_ptr    EQU     STAGE_IO+79
                ELSE
cmt_load_ptr    EQU     $00DA   ; ロード先ポインタ 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
cmt_chk_sum     EQU     STAGE_NUMBER+34
                ELSE
cmt_chk_sum     EQU     $0080   ; チェックサム作業
                ENDC
                IFDEF   STAGE_ALT_RAM
cmt_hdr_buf     EQU     STAGE_BUFFER+60
                ELSE
cmt_hdr_buf     EQU     $0579   ; ヘッダブロックデータ一時バッファ 20B
                ENDC
; --- ディスク LOAD 作業域 (cmt_chk_sum/cmt_hdr_buf へオーバーレイ) ---
;   テープ ブロック読取域と排他利用。ディスク LOAD 経路は CMT ブロック読取を
;   行わないため衝突しない (impl_load は device 分岐後に tape 域へ入らない)。
                IFDEF   STAGE_ALT_RAM
dl_device       EQU     STAGE_NUMBER+35
                ELSE
dl_device       EQU     $0081   ; 装置+ドライブ 1B (=cmt_chk_sum 転用。bit7=1:ディスク, bit0:ドライブ)
                ENDC
                IFDEF   STAGE_ALT_RAM
dl_start_clu    EQU     STAGE_IO+23
                ELSE
dl_start_clu    EQU     $00A2   ; ディレクトリ検索結果: 開始クラスタ 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
dl_ftype        EQU     STAGE_IO+24
                ELSE
dl_ftype        EQU     $00A3   ; ディレクトリ +11 ファイル種別 1B ($00=BASIC,$02=機械語)
                ENDC
                IFDEF   STAGE_ALT_RAM
dl_ascii        EQU     STAGE_IO+25
                ELSE
dl_ascii        EQU     $00A4   ; ディレクトリ +12 ASCII フラグ 1B ($00=トークン,$FF=ASCII)
                ENDC
                IFDEF   STAGE_ALT_RAM
dl_cluster      EQU     STAGE_IO+26
                ELSE
dl_cluster      EQU     $00A5   ; 逐次読取中の現クラスタ番号 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
dl_nsec         EQU     STAGE_IO+29
                ELSE
dl_nsec         EQU     $00A8   ; 現クラスタ内の残セクタ数 1B (1クラスタ=8セクタ)
                ENDC
                IFDEF   STAGE_ALT_RAM
dl_secidx       EQU     STAGE_IO+31
                ELSE
dl_secidx       EQU     $00AA   ; 現在読取セクタ番号 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
dl_blk          EQU     STAGE_BUFFER+59
                ELSE
dl_blk          EQU     $0578   ; 物理ブロック番号 (=クラスタ+8, システム領域オフセット) 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
da_bptr         EQU     STAGE_IO+36
                ELSE
da_bptr         EQU     $00AF   ; ASCII ディスク LOAD: セクタバッファ読出ポインタ 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
dl_clu_adv      EQU     STAGE_IO+43
                ELSE
dl_clu_adv      EQU     $00B6   ; クラスタ読了フラグ 1B (非0=次要求時に割当表の鎖を辿って前進)
                ENDC
                IFDEF   STAGE_ALT_RAM
dl_clu_cnt      EQU     STAGE_IO+44
                ELSE
dl_clu_cnt      EQU     $00B7   ; 読取済クラスタ数 1B (鎖異常による周回を打ち切る防御ガード)
                ENDC
                IFDEF   STAGE_ALT_RAM
inp_fld         EQU     STAGE_IO+48
                ELSE
inp_fld         EQU     $00BB   ; INPUT 文: 入力行 (INPBUF) のフィールド走査位置 2B
                ENDC
                                ;   (cmt_hdr_buf 末尾域 $009A-$00AD の空きを流用。テープ専用ゆえディスク LOAD と排他)
                IFDEF   STAGE_ALT_RAM
dat_tgt         EQU     STAGE_IO+50
                ELSE
dat_tgt         EQU     $00BD   ; READ 文: 文字列の値の作業列 (実行用行バッファ) 上の位置 2B。
                ENDC
                                ;   行変換 (tdd_end) が、この位置を含む DATA 文の原文の位置を dat_src へ記す。
                                ;   READ 文の実行中だけ生きる (同じく cmt_hdr_buf 末尾域の流用。
                                ;   テープ入出力・ディスク LOAD・INPUT 文とは時間排他)
                IFDEF   STAGE_ALT_RAM
dat_src         EQU     STAGE_IO+54
                ELSE
dat_src         EQU     $00C1   ; READ 文: dat_tgt に対応する原文 (格納本文) の位置 2B。0 = 未確定
                ENDC
DL_DISK0        EQU     $80     ; dl_device 値: ディスク ドライブ0 既定
DL_DISK1        EQU     $81     ; dl_device 値: ディスク ドライブ1
                IFDEF   STAGE_ALT_RAM
cmt_lead_cnt    EQU     STAGE_BUFFER+88
                ELSE
cmt_lead_cnt    EQU     $0595   ; リーダー検出用 連続$FFカウンタ (cmt_read_byte が B を破壊するため RAM 上に持つ)
                ENDC
                IFDEF   STAGE_ALT_RAM
il_rl_src       EQU     STAGE_RUNTIME+33
                ELSE
il_rl_src       EQU     $01A1   ; 再リンク: 読み出し元ポインタ 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
il_rl_dst       EQU     STAGE_BUFFER+55
                ELSE
il_rl_dst       EQU     $0574   ; 再リンク: 書き込み先ポインタ 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
il_rl_link      EQU     STAGE_BUFFER+91
                ELSE
il_rl_link      EQU     $0598   ; 再リンク: 現在行 next_ptr 書き込み位置 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
il_start_ln     EQU     STAGE_BUFFER+168
                ELSE
il_start_ln     EQU     $05E5   ; LIST 開始行番号 2B
                ENDC
; il_end_ln: LIST 終端行番号 2B。既定 $FFFF=終端指定なし (末尾まで)。
;   LOADM テープスクラッチ mlm_seg_len ($00C4-$00C5) と時間排他で共用する。
;   LIST はテープ入出力を一切行わず、LOADM は LIST を呼ばないため生存期間は重ならない。
                IFDEF   STAGE_ALT_RAM
il_end_ln       EQU     STAGE_IO+57
                ELSE
il_end_ln       EQU     $00C4   ; = mlm_seg_len (LIST 終端行番号 2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
mlm_dev         EQU     STAGE_RUNTIME+70
                ELSE
mlm_dev         EQU     $01C6   ; LOADM の装置の印 1B (bit7=1 ディスク / $00 カセット)。
                ENDC
                                ;   dl_device ($0081) はテープのブロック読取が
                                ;   チェックサム作業 (cmt_chk_sum) として同じアドレスを
                                ;   潰すため、セグメント解析中も生き残る控えを別に持つ。
                                ;   LOADM の入口 (ilm_args_done) で dl_device から写す。
                IFDEF   STAGE_ALT_RAM
sv_ptr          EQU     STAGE_SAVE+0
                ELSE
sv_ptr          EQU     $00F0   ; SAVE/汎用 走査ポインタ 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
sv_chk          EQU     STAGE_RUNTIME+67
                ELSE
sv_chk          EQU     $01C3   ; CMT 書込チェックサム作業 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
sv_len          EQU     STAGE_RUNTIME+68
                ELSE
sv_len          EQU     $01C4   ; CMT 書込ブロック長作業 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
sv_tmp          EQU     STAGE_RUNTIME+69
                ELSE
sv_tmp          EQU     $01C5   ; CMT 書込一時バイト 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
out_vec         EQU     STAGE_RUNTIME+65
                ELSE
out_vec         EQU     $01C1   ; detoken/print_uint16 出力先ベクタ 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
il_rl_save      EQU     STAGE_BUFFER+94
                ELSE
il_rl_save      EQU     $059B   ; 再リンク: 現在行のセーブ時絶対アドレス 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
ilm_have_addr   EQU     STAGE_IO+69
                ELSE
ilm_have_addr   EQU     $00D0   ; LOADM/SAVEM アドレス引数有無フラグ 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
svm_end         EQU     STAGE_RUNTIME+31
                ELSE
svm_end         EQU     $019F   ; SAVEM 終了アドレス 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
mlm_seg_len     EQU     STAGE_IO+57
                ELSE
mlm_seg_len     EQU     $00C4   ; LOADM セグメント残バイト数 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
mlm_exec        EQU     STAGE_IO+59
                ELSE
mlm_exec        EQU     $00C6   ; LOADM 実行開始アドレス 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
svm_exec        EQU     STAGE_IO+59
                ELSE
svm_exec        EQU     $00C6   ; = mlm_exec と時間排他共用。SAVEM 実行アドレス保持
                ENDC
                                ;   (SAVEM と LOADM/EXEC は同時実行されないため共用可)。
                IFDEF   STAGE_ALT_RAM
mlm_run         EQU     STAGE_IO+61
                ELSE
mlm_run         EQU     $00C8   ; LOADM ,R フラグ 1B (0=無,1=有)
                ENDC
                IFDEF   STAGE_ALT_RAM
mlm_offset      EQU     STAGE_IO+62
                ELSE
mlm_offset      EQU     $00C9   ; LOADM 相対オフセット 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
mlm_bptr        EQU     STAGE_IO+64
                ELSE
mlm_bptr        EQU     $00CB   ; LOADM ブロックバッファ読み出しポインタ 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
mlm_brem        EQU     STAGE_IO+66
                ELSE
mlm_brem        EQU     $00CD   ; LOADM ブロックバッファ残バイト数 1B
                ENDC
                IFDEF   STAGE_ALT_RAM
mlm_user_addr   EQU     STAGE_BUFFER+592
                ELSE
mlm_user_addr   EQU     $078D   ; LOADM ,&Hアドレス指定値 2B
                ENDC
                IFDEF   STAGE_ALT_RAM
for_var_ptr     EQU     STAGE_NUMBER+20
                ELSE
for_var_ptr     EQU     $0072   ; FOR 制御変数の整数値アドレス 2B
                ENDC
;   $00DE-$00E0 は BIOS へのジャンプ (参考書籍「BIOS ジャンプ」。
;     JSR $DE で BIOS を呼べる)。コールドスタートで JMP ml_io_dispatch を置く。
                IFDEF   STAGE_ALT_RAM
BIOS_JMP        EQU     STAGE_IO+83
                ELSE
BIOS_JMP        EQU     $00DE   ; BIOS へのジャンプ (RAM 上の JMP 3B)
                ENDC
;   $00E1-$00EF は作業変数を置かない予約区画 (RAM 台帳 !ZP_RESIDENT)。
;     テープ起動のプログラムが常駐型機械語ローダを持つ場合、低位ページ全域へ自前の
;     イメージを置くことがあり、この区画に実行コードが載り得る。ここに独自の作業変数を置くと、
;     テープ読取のたびにそのコードを書き潰してローダが暴走する。
;     テープのビット読取の途中経過 (適応閾値・組立バイト・立ち上がり待ち
;     カウンタ・終端種別) はレジスタとシステムスタックだけで持つ
;     (7t_cmt_io.inc の cmt_measure_high / cmt_read_byte / cmt_read_block)。
;     **ここへ新しい EQU を置くと make check-ram-map が落ちる。**
                IFDEF   STAGE_ALT_RAM
MEMTOP          EQU     STAGE_PTR+19
                ELSE
MEMTOP          EQU     $0045   ; BASIC 使用上限 −1 = CLEAR 第 2 引数 m − 1
                ENDC
                                ;   (参考書籍「BASIC 使用上限−1」。既定 $7FFE。
                                ;   参考書籍「BASIC 使用上限−1」の位置)。
                                ;   既定 $8000。CLEAR n,m の m でユーザが下げ、
                                ;   機械語ロード領域 ($8000 直下〜) を保護する。2B。
                IFDEF   STAGE_ALT_RAM
mlm_first_addr  EQU     STAGE_SAVE+2
                ELSE
mlm_first_addr  EQU     $00F2   ; LOADM: 最初のセグメントのロード先アドレス
                ENDC
                                ;   (LOADM_ADDR_WORK へ複写するため)
                                ;   2B ($00F2-$00F3)。$FFFF=未設定
                IFDEF   STAGE_ALT_RAM
cmt_strict_hdr  EQU     STAGE_SAVE+4
                ELSE
cmt_strict_hdr  EQU     $00F4   ; LOADM ヘッダ探索の偽 sync 弾き 有効フラグ 1B。
                ENDC
                                ;   0=判定しない / 非0=ブロックタイプ>2 を偽 sync とみなし
                                ;   リーダー再探索へ戻す。LOADM 実行中 (ilm_args_done〜
                                ;   ilm_done) のみ立て、LOAD/常駐ローダ入力は 0 のまま。
                                ;   clr_zp ($00-$7F) の対象外なので LOADM/LOAD 入口で明示制御。
; USR0〜USR9 のアドレス表 (10 スロット × 2B)。参考書籍「USR0〜USR9
;   アドレス」($0219-$022C)。DEF USRn が書き、USRn(引数) が読んで呼ぶ。未設定の
;   ときは EXEC アドレスと同じく Illegal Function Call の入口 (ramstub_ifc) を指す
;   (コールドスタートで EXEC_ADDR から連続 11 語を一括で入れる)。
                IFDEF   STAGE_ALT_RAM
usr_slot_tbl    EQU     STAGE_RUNTIME+153
                ELSE
usr_slot_tbl    EQU     $0219   ; DEF USR 用エントリアドレス表 (10スロット×2B =)
                ENDC
FN_NSLOTS       EQU     6       ; 定義可能な FN 数

; --- ファイル入出力の作業ワーク (割付は内部都合。外部契約は持たない) ---
                IFDEF   STAGE_ALT_RAM
FIO_ERR         EQU     STAGE_IO+53
                ELSE
FIO_ERR         EQU     $00C0   ; 1 文字入力のエラーフラグ (0=正常/非0=エラー)。
                ENDC
                                ;   注: il_rl_save とアドレス共有。再リンクは
                                ;   LOAD 処理中のみ・本フラグはファイル入出力中のみ使用
                                ;   され時間的に重ならない。
; --- CONT (中断再開) 用ワーク ---
;   再開位置は CONTPTR のみが持つ。再開位置を含む行レコードの先頭
;   (CURLINE_PTR / CURLIN の復元元) は、CONT 実行時に TXTTAB から CONTPTR を
;   含む行を検索して導出する (cont_find_line)。
;   行頭位置を常駐ワークに持たないのは、INPBUF が 256B (255 文字 + 終端) を
;     全域使うため、STOP→CONT の間に長い行を打つと INPBUF の内側に重ねた
;     位置が踏まれるからである。ZP は全て割当済で移設先が無く、導出方式なら
;     どの RAM 域にも常駐値を持たず、あらゆる緩衝域の書潰しと無縁になる
;     (CONT の有効性は CONTPTR≠0 とプログラム無変更が前提)。
; --- TIME$ / DATE$ ワーク ---
; TIME$ 計時はサブ CPU RTC ($3E/$3D) 参照へ移行。メイン CPU 側の周期タイマ
; 割込 (IRQ) は PLAY 割込演奏 (7t_play.inc) への tick 供給専用として残す。
; ハンドラは低位 RAM (TICK_ENTRY) に常駐させる (複写元 tick_irq_src を time_init が複写)。
                IFDEF   STAGE_ALT_RAM
IRQ_HOOK        EQU     STAGE_RUNTIME+93
                ELSE
IRQ_HOOK        EQU     $01DD   ; メイン CPU IRQ 入口 (RAM ベクタ表の既定の指し先。JMP 3B)
                ENDC
                IFDEF   STAGE_ALT_RAM
date_buf        EQU     STAGE_BUFFER+80
                ELSE
date_buf        EQU     $058D   ; DATE$ "YY/MM/DD" 保持バッファ (確保 10B。
                ENDC
                                ;   使うのは先頭 8B)。代入で格納・読出で参照する常駐値。
; 配列名退避 (式評価中も配列名を保持する必要があるための一時値)。
                IFDEF   STAGE_ALT_RAM
arr_nptr        EQU     STAGE_RUNTIME+74
                ELSE
arr_nptr        EQU     $01CA   ; 配列名退避 本文上の名前先頭 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
con_rows        EQU     STAGE_FILE+107
                ELSE
con_rows        EQU     $030D   ; CONSOLE のスクロール領域の上限行数の現在値 (1B)。
                ENDC
                                ;   起動時 CON_ROWS。WIDTH の行数指定に追随する。
                IFDEF   STAGE_ALT_RAM
fd02_shadow     EQU     STAGE_BUFFER+149
                ELSE
fd02_shadow     EQU     $05D2   ; 周期タイマ割込の所有状態 (1B。$FD02 は書込専用で
                ENDC
                                ;   読み戻せないため BASIC 側に持つ控え。控えの値を
                                ;   $FD02 へ書くことはない。詳細は 7t_time.inc 頭書き)
                                ;     bit7 = BASIC がハードの許可 ($FD02 bit2) を所有中
                                ;     bit2 = ROM 側 tick 処理の許可 (機械語へ未委譲)
; TIME$ の戻り値は STRPTR がこのバッファを指したまま呼出側へ渡るため、
;   消費前に別の処理へ踏まれると別物の文字列になる。
                IFDEF   STAGE_ALT_RAM
time_buf        EQU     STAGE_BUFFER+36
                ELSE
time_buf        EQU     $0561   ; TIME$ 読出し "HH:MM:SS" 整形バッファ (8B)。
                ENDC
                                ;   読出毎に再生成する一時値。
                IFDEF   STAGE_ALT_RAM
TICK_ENTRY      EQU     STAGE_BUFFER+0
                ELSE
TICK_ENTRY      EQU     $053D   ; タイマ割込ハンドラ常駐先 (低位 RAM、実体 18B)。
                ENDC
                                ;   INPBUF ($043D-$053C) の直後、fn_def_tbl ($054F) の
                                ;   手前に収まる。この区画に作業変数を置かない
                                ;   (下の危険域の注記を参照)。
; 未設定の EXEC / USR の入口。コールド起動時に JMP errc_5 を配置する。
                IFDEF   STAGE_ALT_RAM
ramstub_ifc     EQU     STAGE_RUNTIME+53
                ELSE
ramstub_ifc     EQU     $01B5   ; 未設定の EXEC / USR の入口 (JMP errc_5 = Illegal Function
                ENDC
                                ;   Call, 3B)。参考書籍は
                                ;   EXEC / USR の未設定値を「Illegal Function Call のアドレス」
                                ;   と定める。当方は ROM 内の誤り送出のアドレスを固定しない
                                ;   ので、低位 RAM のここで中継する。

; --- PLAY 文 (音楽演奏機能) ワーク (7t_play.inc) ---
; 音符キュー PLAYQ はテープの機械語ブロック緩衝 (MLBUF) / SAVE のブロック
; 組立バッファ (IIL_BUF) と同じ域 ($05ED) を時間排他で共用する (使用側の
; 入口で play_stop を呼び、停止後はキュー内容を参照しないため安全)。演奏
; 制御に必要な状態 (VSTATE/play_active) は他のどのバッファとも重ならない
; 専用アドレス (VSTATE $05C0-$05D1, play_active $059F) に置く。
                IFDEF   STAGE_ALT_RAM
PLAYQ           EQU     STAGE_BUFFER+176
                ELSE
PLAYQ           EQU     $05ED   ; 音符キュー 3声 × 16 音 × 4B
                ENDC
                IFDEF   STAGE_ALT_RAM
p_tmpb          EQU     STAGE_DRAW+14
                ELSE
p_tmpb          EQU     $0026   ; オクターブシフト量 一時 (1B)。ZP 安全域の
                ENDC
                                ;   空きに置く (演奏の解析中だけ生きる一時
                                ;   ワークで、他の作業変数と時間排他)
                IFDEF   STAGE_ALT_RAM
VSTATE          EQU     STAGE_BUFFER+131
                ELSE
VSTATE          EQU     $05C0   ; 声別状態 3 × 6B:
                ENDC
                                ;   +0 残tick(2) +2 head(1) +3 cnt(1)
                                ;   +4 音量(1) +5 Sパターン(1, $FF=V モード)
                IFDEF   STAGE_ALT_RAM
play_active     EQU     STAGE_BUFFER+98
                ELSE
play_active     EQU     $059F   ; 演奏中フラグ (タイマ割込ハンドラの入口ゲート)
                ENDC
                IFDEF   STAGE_ALT_RAM
p_voice         EQU     STAGE_FILE+151
                ELSE
p_voice         EQU     $0339   ; 解析中の声番号 0-2 (1B)
                ENDC
; PLAY 解析ワークは全て「PLAY 文の解析中だけ生きる一時ワーク」(演奏側の
;   タイマ割込は VSTATE/PLAYQ のみ参照) のため、ZP 安全域の空き
;   ($0016 / $0030 / $0037 / $0039) に置く。
                IFDEF   STAGE_ALT_RAM
p_mmlend        EQU     STAGE_DRAW+24
                ELSE
p_mmlend        EQU     $0030   ; MML 文字列終端 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
p_div           EQU     STAGE_PTR+7
                ELSE
p_div           EQU     $0039   ; 16bit 除算の除数 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
p_oct           EQU     STAGE_EXPR+20
                ELSE
p_oct           EQU     $0016   ; 解析中のオクターブ (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
p_len           EQU     STAGE_PTR+42
                ELSE
p_len           EQU     $005C   ; 解析中の既定音長 L (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
p_qtk           EQU     STAGE_PTR+5
                ELSE
p_qtk           EQU     $0037   ; 全音符の tick 数 = (29520/T)×4 (2B)
                ENDC

; --- 塗りつぶし指定 (CIRCLE 第 7 引数 F) の作業域 (7t_save_list.inc) ---
;   走行は cir_draw の内側でのみ生きる。ZP 安全域のうち CIRCLE が
;   使っていない枠を借りる (GET/PUT・配列名・乗算スクラッチは引数解析が
;   終わった時点で死んでおり、描画エンジンは式評価を呼ばないため排他)。
                IFDEF   STAGE_ALT_RAM
CFL_AX          EQU     STAGE_EXPR+2
                ELSE
CFL_AX          EQU     $0004           ; 走査元の頂点 X (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
CFL_AY          EQU     STAGE_EXPR+4
                ELSE
CFL_AY          EQU     $0006           ; 走査元の頂点 Y (2B)。CFL_AX+2 に置くこと
                ENDC
                                        ;   (cdr_lp が ,X / 2,X で一括退避する)
                IFDEF   STAGE_ALT_RAM
CFL_I           EQU     STAGE_EXPR+8
                ELSE
CFL_I           EQU     $000A           ; 残り回数 (2B)。CIR_R と排他共用
                ENDC
                                        ;   (CIR_R は cir_premag 完了で用済み)
                IFDEF   STAGE_ALT_RAM
CFL_BLK         EQU     STAGE_DRAW+0
                ELSE
CFL_BLK         EQU     $0018           ; 軸ブロック 2 本 (各 6B, $000D-$0018)
                ENDC
CFL_SX          EQU     CFL_BLK+0       ;   X 歩幅 (+1/-1)
CFL_IX          EQU     CFL_BLK+2       ;   X 増分 = |dx|+1
CFL_EX          EQU     CFL_BLK+4       ;   X 誤差
CFL_SY          EQU     CFL_BLK+6       ;   Y 歩幅 (+1/-1)
CFL_IY          EQU     CFL_BLK+8       ;   Y 増分 = |dy|+1
CFL_EY          EQU     CFL_BLK+10      ;   Y 誤差
                IFDEF   STAGE_ALT_RAM
CFL_N           EQU     STAGE_DRAW+12
                ELSE
CFL_N           EQU     $0024           ; 辺を覆う画素数 = max(|dx|,|dy|)+1 (2B)
                ENDC

; I/O
PORT_FIRQ       EQU     $FD04
PORT_SUBCPU     EQU     $FD05
PORT_PSG_ADDR   EQU     $FD0D
PORT_CMT        EQU     $FD02
PORT_IRQSTAT    EQU     $FD03           ; 読出 = IRQ 要因フラグ (bit2=タイマ, 0=発生)
PORT_CMT_CTRL   EQU     $FD00           ; bit1: モーターON
PORT_ROMSEL     EQU     $FD0F           ; 読出し=BASIC ROM有効, 書込み=RAM有効 (オーバレイ切替)
MULTIPAGE       EQU     $FD37           ; 重ね合わせ表示の面制御 (書込専用)。
                                        ;   bit0-2 = 書込を止める面, bit4-6 = 表示を止める面

; 共有 RAM（メイン側アドレス）
; サブ側 $D380 = メイン側 $FC80
SUBCMD_BASE     EQU     $FC80
; $FC80 = $D380 : エラーコードレジスタ（サブ→メイン）
; $FC81 = $D381 : ステータス
; $FC82 = $D382 : コマンドコード
; $FC83 = $D383 : パラメータ1（コマンド別）
; $FC84 = $D384 : パラメータ2
; $FC85 = $D385 : パラメータ3
; 等...
; --- サブシステム コマンド ステージング バッファ (ゼロページ常駐 RAM) ---
; FM-7 のデュアルポート共有 RAM ($FC80-$FCFF) は、サブ CPU を HALT
; ($FD05 bit7=1) している間だけメイン CPU から安全にアクセスできる。
; ハードウェアの調停により、サブ CPU 走行中にメイン CPU が共有 RAM を
; 読み書きしても正しく転送されない。そこでコマンド/パラメータは一旦この
; ゼロページのステージングへ組み立て、impl_sub_send_cmd が HALT 下で
; 共有 RAM へ複写してからサブを起動し、完了後に結果を読み戻す。
; 各オフセットは共有 RAM ($FC80 起点) の対応位置と一致させる。
; 共有 RAM 全域 ($FC80-$FCFF, 128B) を丸ごとミラーする (SYMBOL/CONNECT 等が
; パラメータ末尾に長い文字列を $D38D 以降へ書くため 16B では不足)。$0100-$017F
; は未使用 (他のバッファと非衝突)。
                IFDEF   STAGE_ALT_RAM
SUBSTG_BASE     EQU     STAGE_SUBCMD+0
                ELSE
SUBSTG_BASE     EQU     $0100           ; ステージング先頭 (, 128B)
                ENDC
SUBSTG_LEN      EQU     128             ; 複写バイト数 (共有 RAM 全域)
SUBCMD_CMD      EQU     SUBSTG_BASE+2   ; コマンドコード (= $FC82 相当)
SUBCMD_P1       EQU     SUBSTG_BASE+3   ; パラメータ1 (= $FC83/$D383)
SUBCMD_P2       EQU     SUBSTG_BASE+4   ; パラメータ2 (= $FC84/$D384)
SUBCMD_P3       EQU     SUBSTG_BASE+5   ; パラメータ3 (= $FC85/$D385)
SUBCMD_P4       EQU     SUBSTG_BASE+6   ; パラメータ4 (= $FC86/$D386)
SUBCMD_P5       EQU     SUBSTG_BASE+7   ; パラメータ5 (= $FC87/$D387)
SUBCMD_P6       EQU     SUBSTG_BASE+8   ; パラメータ6 (= $FC88/$D388)
SUBCMD_P7       EQU     SUBSTG_BASE+9   ; パラメータ7 (= $FC89/$D389)
SUBCMD_P8       EQU     SUBSTG_BASE+10  ; パラメータ8 (= $FC8A/$D38A)
SUBCMD_P9       EQU     SUBSTG_BASE+11  ; パラメータ9 (= $FC8B/$D38B)
SUBCMD_P10      EQU     SUBSTG_BASE+12  ; パラメータ10 (= $FC8C/$D38C)
SUBCMD_P11      EQU     SUBSTG_BASE+13  ; パラメータ11 (= $FC8D/$D38D)

; --- ROM 疑似変数の RAM 移設 ---
; ROM オーバレイ ($8000-$FBFF 読出=ROM) が有効の間、この領域への書込は実機では
; 保持されない。よって書換えを要する作業変数を ROM 内 FCB/FDB で持つと
; 「書いた値が読めず常に初期値」になる (PSET/LINE の座標が常に 0 になる等)。
; 全て RAM へ EQU 移設し、初期値はコールドスタートの init_ram_vars が設定する。
                IFDEF   STAGE_ALT_RAM
tmpX16          EQU     STAGE_IO+0
                ELSE
tmpX16          EQU     $008B   ; グラフィック座標 X1 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
tmpY16          EQU     STAGE_IO+2
                ELSE
tmpY16          EQU     $008D   ; グラフィック座標 Y1 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
tmpX16_2        EQU     STAGE_IO+4
                ELSE
tmpX16_2        EQU     $008F   ; グラフィック座標 X2 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
tmpY16_2        EQU     STAGE_IO+6
                ELSE
tmpY16_2        EQU     $0091   ; グラフィック座標 Y2 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gcur_x          EQU     STAGE_IO+96
                ELSE
gcur_x          EQU     $00EB   ; グラフィックカーソル X (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
gcur_y          EQU     STAGE_IO+98
                ELSE
gcur_y          EQU     $00ED   ; グラフィックカーソル Y (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
inp_dlm2        EQU     STAGE_IO+8
                ELSE
inp_dlm2        EQU     $0093   ; 値の取り出しの追加区切り (1B)。DATA 文からも
                ENDC
                                ;   INPUT 文からも ':' を入れる。DATA は文の区切りで
                                ;   値も終わり、INPUT へ打ち込まれた行も ':' で
                                ;   コンマと同じように項目が切れる。
                IFDEF   STAGE_ALT_RAM
disp_state      EQU     STAGE_BUFFER+107
                ELSE
disp_state      EQU     $05A8   ; コンソールコントロール (1B, 初期値 $1F)。
                ENDC
                                ;   位置は参考書籍「$05A8
                                ;   コンソールコントロール」。bit0 がカーソルの表示。
                                ;   コンソール制御 ($0C) は「カーソルだけ」ではなく
                                ;   表示状態を丸ごと受け取る枠なので、bit0 (カーソル
                                ;   有効) 以外のビットを落とさずに送るには現在値が
                                ;   要る。サブ CPU 側の作業域はメイン CPU 側から読めないため、
                                ;   メイン CPU 側で同じ値を控えておき、そこから派生させて
                                ;   発行する。定数を送ると他のビットが消える。
                IFDEF   STAGE_ALT_RAM
trace_flag      EQU     STAGE_IO+30
                ELSE
trace_flag      EQU     $00A9   ; TRON/TROFF フラグ (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
cur_fg_color    EQU     STAGE_RUNTIME+101
                ELSE
cur_fg_color    EQU     $01E5   ; 現在の描画色 (1B, 初期値 7)
                ENDC
                IFDEF   STAGE_ALT_RAM
cur_bg_color    EQU     STAGE_RUNTIME+102
                ELSE
cur_bg_color    EQU     $01E6   ; 現在の背景色 (1B, 初期値 0)
                ENDC
                IFDEF   STAGE_ALT_RAM
mml_tempo       EQU     STAGE_IO+9
                ELSE
mml_tempo       EQU     $0094   ; MML テンポ (1B, 初期値 100)
                ENDC
                IFDEF   STAGE_ALT_RAM
mml_octave      EQU     STAGE_IO+10
                ELSE
mml_octave      EQU     $0095   ; MML オクターブ (1B, 初期値 4)
                ENDC
                IFDEF   STAGE_ALT_RAM
mml_length      EQU     STAGE_IO+11
                ELSE
mml_length      EQU     $0096   ; MML 音長 (1B, 初期値 4)
                ENDC
                IFDEF   STAGE_ALT_RAM
mml_volume      EQU     STAGE_IO+12
                ELSE
mml_volume      EQU     $0097   ; MML 音量 (1B, 初期値 15)
                ENDC
; --- ON ERROR / RESUME / CLEAR n,m 用ワーク ---
                IFDEF   STAGE_ALT_RAM
ERRIP           EQU     STAGE_IO+39
                ELSE
ERRIP           EQU     $00B2   ; エラー発生文の CURIP (RESUME 用, 2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
ERRLPTR         EQU     STAGE_IO+13
                ELSE
ERRLPTR         EQU     $0098   ; エラー発生行の CURLINE_PTR (RESUME 用, 2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
EXEC_SP         EQU     STAGE_IO+15
                ELSE
EXEC_SP         EQU     $009A   ; 文の切れ目のシステム S = 枠列の先頭 (2B。誤り捕捉と
                ENDC
                                ;   CHAIN の巻戻し先。枠の出し入れで動く)
                IFDEF   STAGE_ALT_RAM
crs_oldtop      EQU     STAGE_PTR+39
                ELSE
crs_oldtop      EQU     $0059   ; clear_relocate_stack: 旧 STRBOT 一時 (2B。frame_del の作業にも借用)
                ENDC
                IFDEF   STAGE_ALT_RAM
crs_new         EQU     STAGE_RUNTIME+19
                ELSE
crs_new         EQU     $0193   ; clear_relocate_stack: 移設量 (2B。cmd_direct の作業にも借用)
                ENDC
sym_buf         EQU     str_work_buf+$00   ; SYMBOL 文字列作業バッファ (82B)

; スタック
STACK_TOP       EQU     $7FFF
; システムスタックが動くために残す最小の空き (バイト)。変数・配列の確保、枠の
;   積み上げ、式の入れ子 (括弧・関数の再帰) の各所で「S − STK_RESERVE が STREND を
;   割らないこと」を検査し、割るなら Out Of Memory。参考書籍が定める領域の並び
;   に固定の区画を足すものではなく、検査の刻み (再帰 1 段 + そこから先の
;   呼出の深さ) を覆う値である。内部の自動テストと公開する互換性テストの走行で
;   文の切れ目からの最深到達を計測して定めた (内部の自動テスト 25 本と公開する
;   互換性テスト 135 本を、命令ごとに S と EXEC_SP を採りながら走らせた最深値は
;   124 バイト = 使用者定義関数の入れ子。これを覆う 128 とする)。
STK_RESERVE     EQU     128
; CONSOLE のスクロール領域の上限行数の既定値 (起動直後の値)。
; 上限は WIDTH の**行数指定**で 20 / 25 に変わり、現在値は con_rows が持つ。
; 桁数だけを変える WIDTH (行数を省いた形) では上限は動かない (動作観察)。
; 機能キー行を表示する指定のときは、そこからさらに 2 行狭まる。
CON_ROWS        EQU     20

; ファンクションコード（グラフィック描画モード）
;   サブシステム (pb1_fntbl) の raster-op 定数表と一致させること:
;     0=PSET / 1=PRESET / 2=OR / 3=AND / 4=XOR / 5=NOT。
;   参考書籍の F-BASIC も OR=2 / AND=3 で送る。AND=2/OR=3 と取り違えると、
;   PUT@/PUT@A/LINE の OR と AND が入れ替わって描画されていた (是正)。
FUNC_PSET       EQU     0               ; ドット書込み
FUNC_PRESET     EQU     1               ; ドット消去
FUNC_OR         EQU     2               ; OR
FUNC_AND        EQU     3               ; AND
FUNC_XOR        EQU     4               ; XOR

; LINE Bフラグ
LINE_LINE       EQU     0               ; 直線
LINE_BOX        EQU     1               ; 矩形枠
LINE_BOXF       EQU     2               ; 矩形塗りつぶし

;==============================================================================
; PRINT USING スタックフレーム (S 相対オフセット)
;   RAM を新規占有しないため、文の実行中だけ S 上にフレームを確保する。
;   フレーム確保は ip_using 入口 (LEAS -UF_SIZE,S)、解放は ipu_dealloc。
;   値式評価 (impl_eval_expr) は駆動ループ (S=フレーム基底) からのみ呼ぶので、
;   駆動側は n,S 固定オフセット・下請け整形ルーチンは U=フレーム基底で参照する。
;   書式文字列が文字列連結の一時バッファ (str_work_buf) 上にある場合だけ、
;   後続の値評価による破壊を避けるためフレーム直上へ複写する (UF_CPLEN)。
;==============================================================================
UF_FLAGS        EQU     0       ; bit0=値保留 bit1=当周で消費 bit2=末尾';'改行抑止
UF_SPEC         EQU     1       ; bit0=先頭+ bit1=** bit2=円 bit3=, bit4=. bit6=数値核 bit7=0表示
UF_WINT         EQU     2       ; 整数部の桁位置数 (前置分含む)
UF_DECLEN       EQU     3       ; 小数部桁数
UF_POST         EQU     4       ; 後置符号 (0 / '+' / '-')
UF_NEG          EQU     5       ; 値の符号 (0/1)
UF_DEXP         EQU     6       ; 10進指数 (値 = 0.dig × 10^dexp, 符号付き)
UF_DLEN         EQU     7       ; 有効桁数
UF_TMP          EQU     8       ; 汎用一時
UF_TMP2         EQU     9       ; 汎用一時
UF_J            EQU     10      ; ループカウンタ
UF_FREM         EQU     11      ; 書式残りバイト数
;   ins_expchk は JSR 経由 (S = フレーム基底 + 2) で UF_EXPF を触るため、
;   UF_EXPF は 5 ビットオフセットに収まる位置 (+2 して 15 以下) に置く。
UF_EXPF         EQU     12      ; 指数形式 '^^^^' 指定 (0/1)
UF_TEXP         EQU     13      ; 指数表記文字 ('E'/'D')
UF_FPTR         EQU     14      ; 書式走査ポインタ (2B)
UF_CPLEN        EQU     16      ; 書式スタック複写長 (0=複写なし)
UF_FLEN         EQU     17      ; 書式全長
UF_FBASE        EQU     18      ; 書式基底 (2B)
UF_TEXPV        EQU     20      ; 指数形式 '^^^^' の指数値 (符号付き, -99..99)
UF_DIG          EQU     21      ; 10進有効桁バッファ (ASCII, 18B)
UF_SIZE         EQU     39      ; フレーム全長

;==============================================================================
; ROM 本体
;==============================================================================
