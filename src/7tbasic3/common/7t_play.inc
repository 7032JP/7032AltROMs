; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 7t_play.inc — PLAY 文 / MML パーサ / 割込み駆動プレイヤ
;
; 参考書籍「音楽演奏機能」準拠:
;   PLAY "MML" [, "MML" [, "MML"]]  … 3 和音 (3声) まで。
;   MML: A-G(音程, 後置 #/+/-)、R(休符)、Ln(音長 1-64, 後置 '.' 付点)、
;        On(オクターブ 1-8)、Nn(特殊音程 1-96)、Tn(テンポ 32-255)、
;        Vn(音量 0-15)、Sn(エンベロープパターン 0-15, 指定中は V 無効)、
;        Mn(エンベロープ周期 0-65535)、関係指定 (n に変数)。
;   デフォルト (参考書籍): L=4, O=4, T=120, V=4。
;   注(3): 最高 3 声で 16 音までメモリーに記憶し、割込処理で音階を発生する
;   ため、他の処理を実行しつつバックグラウンドで演奏できる。
;
; 実装方式:
;   - 文の実行時に MML を解析し、声別 16 エントリのリングキュー (PLAYQ) へ
;     積む。キューが満杯の間は空きが出るまで待つ (割込みが消費する)。
;   - 周期タイマ割込 (2.0325ms, 7t_time.inc の tick 基盤に相乗り) が
;     play_irq_tick で残り時間を減算し、音の切れ目でキューから次を取り出して
;     PSG (AY-3-8910 タイプ, $FD0D/$FD0E) へ設定する。
;   - 音長: 4分音符の tick 数 = 60000ms/T/2.0325ms = 29520/T。
;     全音符 tick 数 (p_qtk) = その 4 倍。音符の tick 数 = p_qtk/n。
;     付点 '.' は 1/2, 1/4, … を逐次加算 (参考書籍)。
;   - 音程: f = f_clock/(16*D), f_clock=1.2288MHz (参考書籍 SOUND の周波数式)
;     より D = 76800/f。O4 の 12 音を表に持ち、オクターブ差はシフトで求める。
;     O4 A = 440Hz → D = 175。
;   - ミキサ R7 は演奏開始/停止時に $F8 (トーン 3 声有効・ノイズ無効・音量 0
;     で無音) を設定し、演奏中は音量レジスタ (R8-R10) のみで発音を制御する。
;   - 中断 (参考書籍): BREAK / 入力待ちの CTRL-X・CTRL-C /
;     オーディオカセット関係のコマンド (MOTOR を除く) / RUN / Abort 表示。
;     この 5 つの入口にだけ play_stop をフックし、PSG を初期状態へ戻す。
;     参考書籍のとおり、それ以外の処理は演奏を続けたまま進む。
;
; RAM 割当 (7t_defs.inc):
;   PLAYQ  $05ED-: 3声 × 16 エントリ × 4B。テープの機械語ブロック緩衝 (MLBUF)・
;     SAVE のブロック組立バッファ (IIL_BUF)・ディスクのセクタ読取バッファ
;     (DA_SECBUF) と同じ CAS0 入出力バッファの域を時間排他で共用するため、
;     それらの使用直前にも play_stop をフックする (演奏停止後はキュー内容を
;     参照しない)。
;   VSTATE $05C0-: 声別 6B (残tick 2 / head 1 / cnt 1 / 音量 1 / S状態 1)。
;   play_active $059F: 演奏中フラグ (割込みハンドラの入口ゲート)。
;
; キューエントリ (4B):
;   b0 < $10 … 音符: 分周値 = (b0<<8)|b1 (0 なら休符), 音長 tick = (b2<<8)|b3
;   b0 = $FF … 制御: b1=0 音量(V) b3=値 / b1=1 エンベロープ形状(S) b3=値 /
;                     b1=2 エンベロープ周期(M) b2b3=値 (R12/R11 へ即時反映)
;==============================================================================

;------------------------------------------------------------------------------
; 音名 → 半音番号 (C=0)。A=ラ..G=ソ (参考書籍)
;------------------------------------------------------------------------------
play_note_tbl:
                FCB     9               ; A
                FCB     11              ; B
                FCB     0               ; C
                FCB     2               ; D
                FCB     4               ; E
                FCB     5               ; F
                FCB     7               ; G

; (O4 の PSG 分周値の表 play_tp_tbl は容量の都合で ROM 末尾 (7t_core.s) へ移した。
;  参照は LDU #play_tp_tbl だけで位置依存はない)

;==============================================================================
; PLAY 文 (二次コマンド $F3)
;   呼出規約: iep_2nd / cmd_direct_2nd は本文ポインタを 2,S へ退避して
;   `JSR ,X` で呼ぶ。読了位置は 2,S へ書き戻す (impl_chain と同じ規約)。
;==============================================================================
impl_play:
                LDX     2,S             ; X = 本文ポインタ (PLAY トークン直後)
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     ipl_ret         ; 引数なし → no-op
                CMPA    #':'
                BEQ     ipl_ret
                ; ミキサ初期化: トーン 3 声有効 (音量 0 のため未発音)
                LDD     #$07F8                  ; レジスタ 7 = ミキサ
                JSR     impl_psg_write
                CLR     >p_voice
ipl_vloop:
                ; 声ごとの MML 既定値 (参考書籍: T=120, O=4, L=4)
                LDD     #984            ; 全音符 tick 数 = (29520/120)*4
                STD     p_qtk
                LDA     #4
                STA     p_oct
                STA     p_len
                JSR     eval_chk_str    ; Z=1 なら文字列型 (共通化)
                LBNE    err_tmm
                PSHS    X               ; 本文ポインタを退避して MML を走査
                LDB     STRLEN
                CLRA
                ADDD    STRPTR
                STD     p_mmlend       ; MML 終端 = STRPTR + STRLEN
                LDX     STRPTR
                BSR     mml_parse_voice
                PULS    X
                JSR     skip_comma
                BCC     ipl_ret
                INC     >p_voice
                LDA     >p_voice
                CMPA    #3
                LBHS    errc_5          ; 4 声目は Illegal Function Call
                BRA     ipl_vloop
ipl_ret:
                STX     2,S             ; 読了位置を実行ループへ返す
                RTS

;==============================================================================
; MML 1 声ぶんの解析 (X = MML 先頭, p_mmlend まで)
;==============================================================================
mml_parse_voice:
mpv_lp:
                JSR     mml_peek
                BEQ    mpv_done
                LEAX    1,X
                CMPA    #'T'
                BEQ    mpv_T
                CMPA    #'O'
                BEQ    mpv_O
                CMPA    #'L'
                BEQ     mpv_L
                CMPA    #'V'
                LBEQ    mpv_V
                CMPA    #'S'
                LBEQ    mpv_S
                CMPA    #'M'
                LBEQ    mpv_M
                CMPA    #'N'
                LBEQ    mpv_N
                CMPA    #'R'
                LBEQ    mpv_R
                CMPA    #'A'
                BLO     mpv_lp          ; 未定義文字は読み飛ばす
                CMPA    #'G'
                BHI     mpv_lp
                LBRA    mpv_note
mpv_done:
                RTS

; --- Tn: テンポ 32-255 (参考書籍) ---
mpv_T:
                BSR     mml_num_req     ; D = 数値引数 (共通化)
                BSR     mml_num_rng
                FDB     32
                FDB     255
                LDA     >p_voice
                BNE     mpt_nomirror
                STB     mml_tempo      ; 声 1 の互換ミラー
mpt_nomirror:
                STD     p_div
                LDD     #29520          ; 4分音符 tick 数の分子 (60000/2.0325)
                JSR     play_udiv16
                ASLB
                ROLA
                ASLB
                ROLA                    ; ×4 = 全音符 tick 数
                STD     p_qtk
                BRA     mpv_lp

; >>FOLD mml_num_req
;------------------------------------------------------------------------------
; mml_num_req — MML の数値引数を 1 個読む。数字が無ければ Illegal Function Call。
;   O/L/V/S/M/N/T の 7 箇所が同じ 2 命令を並べていたので 1 本にまとめた
;   (容量捻出)。D = 値。CC は呼出側が改めて CMPD で作る。
;   誤りのときは復帰せずエラー処理へ抜ける (スタックは復帰時に張り直される)。
;------------------------------------------------------------------------------
mml_num_req:
                JSR     mml_num_arg
                LBCS    errc_5
                RTS

; mml_num_rng — D が指定の範囲の外なら Illegal Function Call。
;   呼出し形式:  BSR mml_num_rng / FDB 下限 / FDB 上限
;   範囲検査の 4 命令が MML の各命令で同じ形なので 1 本にまとめた
;   (容量の都合。判定する値と分岐先は変わらない)。破壊: X
mml_num_rng:
                PSHS    X               ; 本文ポインタは呼出側が使い続ける
                LDX     2,S             ; 戻りアドレス = 下限語の位置
                CMPD    ,X++
                LBLO    errc_5
                CMPD    ,X++
                LBHI    errc_5
                STX     2,S             ; 上限語の次から再開する
                PULS    X,PC
; <<FOLD mml_num_req

; --- On: オクターブ 1-8 (参考書籍) ---
mpv_O:
                BSR     mml_num_req     ; D = 数値引数 (共通化)
                BSR     mml_num_rng
                FDB     1
                FDB     8
                LDA     >p_voice
                BNE     mpo_nomirror
                STB     mml_octave
mpo_nomirror:
                STB     p_oct
                LBRA    mpv_lp

; --- Ln: 音長 1-64 (参考書籍) ---
mpv_L:
                BSR     mml_num_req     ; D = 数値引数 (共通化)
                BSR     mml_num_rng
                FDB     1
                FDB     64
                LDA     >p_voice
                BNE     mpl_nomirror
                STB     mml_length
mpl_nomirror:
                STB     p_len
                LBRA    mpv_lp

; --- Vn: 音量 0-15。S 指定の解除を兼ねる (参考書籍) ---
mpv_V:
                BSR     mml_num_req     ; D = 数値引数 (共通化)
                CMPD    #15
                LBHI    errc_5
                LDA     >p_voice
                BNE     mpvv_nomirror
                STB     mml_volume
mpvv_nomirror:
                TFR     D,Y
                LDA     #$FF
                CLRB                    ; 制御タイプ 0 = V

; >>FOLD mml_ctl_enq
;------------------------------------------------------------------------------
; mml_ctl_enq — MML の制御イベント (Y = 値, B = 種別) をキューへ積んで走査へ戻る。
;   V / S / M の 3 箇所が同じ尾部 2 命令を並べていたので 1 本にまとめた
;   (容量捻出)。命令の並べ替えはしていない。
;   末尾は元と同じ LBRA なので呼出側から見た意味は変わらない。
;------------------------------------------------------------------------------
mml_ctl_enq:
                JSR     play_enq
                LBRA    mpv_lp
; <<FOLD mml_ctl_enq

; --- Sn: エンベロープパターン 0-15 (参考書籍) ---
mpv_S:
                BSR     mml_num_req     ; D = 数値引数 (共通化)
                CMPD    #15
                LBHI    errc_5
                TFR     D,Y
                LDD     #$FF01          ; 制御タイプ 1 = S
                BRA     mml_ctl_enq     ; (共通化)

; --- Mn: エンベロープ周期 0-65535 (参考書籍) ---
mpv_M:
                BSR     mml_num_req     ; D = 数値引数 (共通化)
                TFR     D,Y
                LDD     #$FF02          ; 制御タイプ 2 = M
                BRA     mml_ctl_enq     ; (共通化)

; --- Nn: 特殊音程 1-96 (O1C=1 〜 O8B=96, 参考書籍) ---
mpv_N:
                BSR     mml_num_req     ; D = 数値引数 (共通化)
                BSR     mml_num_rng
                FDB     1
                FDB     96
                SUBD    #1
                LDA     #1              ; A = オクターブ, B = 残り半音数
mpn12_lp:
                CMPB    #12
                BLO     mpn12_done
                SUBB    #12
                INCA
                BRA     mpn12_lp
mpn12_done:
                BSR     mml_tp          ; D = 分周値
                PSHS    B,A
                LDB     p_len          ; 音長は L 指定に従う
                CLRA
                STD     p_div
                LDD     p_qtk
                JSR     play_udiv16
                ADDD    #0              ; D = 0 か (CMPD #0 と同じフラグで 1 バイト短い)
                BNE     mpn_durok
                LDD     #1
mpn_durok:
                TFR     D,Y
                PULS    A,B
                BRA     mml_ctl_enq     ; 積んで次の字句へ (共通尾部)

; --- R: 休符。音長は数字/付点付き、なければ L に従う (参考書籍) ---
mpv_R:
                BSR     mml_dur
                TFR     D,Y
                CLRA
                CLRB                    ; 分周値 0 = 休符
                BRA     mml_ctl_enq     ; 積んで次の字句へ (共通尾部)

; --- A-G: 音符。後置 #/+/-、数字音長、付点 (参考書籍) ---
mpv_note:
                SUBA    #'A'
                TFR     A,B
                LDU     #play_note_tbl
                LDB     B,U             ; B = 半音番号 (C=0)
                JSR     mml_peek
                CMPA    #'#'
                BEQ     mpn_sharp
                CMPA    #'+'
                BEQ     mpn_sharp
                CMPA    #'-'
                BEQ     mpn_flat
                BRA     mpn_accdone
mpn_sharp:
                LEAX    1,X
                INCB
                BRA     mpn_accdone
mpn_flat:
                LEAX    1,X
                DECB
mpn_accdone:
                ; 半音の桁跨ぎ (C- / B#) をオクターブへ繰り込む
                LDA     p_oct
                TSTB
                BPL     mpn_hi
                ADDB    #12
                DECA
                BRA     mpn_wrapped
mpn_hi:
                CMPB    #12
                BLO     mpn_wrapped
                SUBB    #12
                INCA
mpn_wrapped:
                TSTA
                BNE     mpn_clamp1
                LDA     #1              ; O1 の下端で飽和
mpn_clamp1:
                CMPA    #8
                BLS     mpn_clamp2
                LDA     #8              ; O8 の上端で飽和
mpn_clamp2:
                BSR     mml_tp          ; D = 分周値
                PSHS    B,A
                BSR     mml_dur         ; D = 音長 tick (数字/付点/関係指定)
                TFR     D,Y
                PULS    A,B
                LBRA    mml_ctl_enq     ; 積んで次の字句へ (共通尾部。短分岐は届かない)

;------------------------------------------------------------------------------
; mml_tp: 分周値を求める。入力 A=オクターブ(1-8), B=半音番号(0-11)。出力 D。
;   O4 の表引き後、オクターブ差ぶんシフトする。壊す: A,B,U,p_tmpb
;------------------------------------------------------------------------------
mml_tp:
                SUBA    #4
                STA     p_tmpb         ; シフト量 (負 = 左シフト)
                ASLB
                LDU     #play_tp_tbl
                LEAU    B,U
                LDD     ,U
                TST     p_tmpb
                BEQ     mtp_done
                BMI     mtp_left
mtp_right:
                LSRA
                RORB
                DEC     p_tmpb
                BNE     mtp_right
                RTS                ; (元: BRA mtp_done)
mtp_left:
                NEG     p_tmpb
mtp_l_lp:
                ASLB
                ROLA
                DEC     p_tmpb
                BNE     mtp_l_lp
mtp_done:
                RTS

;------------------------------------------------------------------------------
; mml_dur: 音符/休符の音長 tick 数を求める。
;   直後の数字 (単発音長 1-64) が有れば p_qtk/n、無ければ p_qtk/L。
;   後置 '.' (付点) ごとに 1/2, 1/4, … を加算 (参考書籍)。
;   出力 D (最低 1)。X は消費分前進。壊す: A,B,Y
;------------------------------------------------------------------------------
mml_dur:
                BSR     mml_num_sfx
                BCS     mdu_deflen
                LBSR    mml_num_rng     ; 当区間からは短分岐が届かない
                FDB     1
                FDB     64
                STD     p_div
                BRA     mdu_div
mdu_deflen:
                LDB     p_len
                CLRA
                STD     p_div
mdu_div:
                LDD     p_qtk
                JSR     play_udiv16
                TFR     D,Y             ; Y = 合計, D = 次の加算単位
mdu_dot_lp:
                PSHS    B,A
                BSR     mml_peek
                CMPA    #'.'
                BNE     mdu_dot_end
                LEAX    1,X
                PULS    A,B
                LSRA
                RORB                    ; 加算単位 /= 2
                LEAY    D,Y
                BRA     mdu_dot_lp
mdu_dot_end:
                PULS    A,B
                TFR     Y,D
                ADDD    #0              ; D = 0 か (CMPD #0 と同じフラグで 1 バイト短い)
                BNE     mdu_ret
                LDD     #1              ; 0 tick は 1 に切上げ
mdu_ret:
                RTS

;------------------------------------------------------------------------------
; mml_peek: MML の次の有効文字を A に返す (空白は読み飛ばし、小文字は大文字化)。
;   終端なら A=0 (Z=1)。X は空白ぶんだけ前進 (有効文字は消費しない)。
;------------------------------------------------------------------------------
mml_peek:
mpk_lp:
                CMPX    p_mmlend
                BHS     mpk_end
                LDA     ,X
                CMPA    #' '
                BNE     mpk_uc
                LEAX    1,X
                BRA     mpk_lp
mpk_uc:
                CMPA    #'z'
                BHI     mpk_chk
                CMPA    #'a'
                BLO     mpk_chk
                SUBA    #$20
mpk_chk:
                TSTA
                BNE     mpk_ret
mpk_end:
                CLRA                    ; 終端 (Z=1)
mpk_ret:
                RTS

;------------------------------------------------------------------------------
; mml_num_arg / mml_num_sfx: MML 中の数値 n を読む。
;   mml_num_arg: コマンド引数位置。数字列 / '='変数名';' / 変数名';' を受理。
;   mml_num_sfx: 音符直後の音長位置。数字列 / '='変数名';' のみ
;     (裸の変数名は音名 A-G と区別できないため不可)。
;   関係指定 (参考書籍): n に変数を使える。変数値は整数化する。
;   出力: C=0 なら D=値 / C=1 なら数値なし。X は消費分前進。
;------------------------------------------------------------------------------
mml_num_sfx:
                BSR     mml_peek
                BEQ     mnum_none
                CMPA    #'='
                BEQ     mnum_vareq
                BRA     mnum_digchk
mml_num_arg:
                BSR     mml_peek
                BEQ     mnum_none
                CMPA    #'='
                BEQ     mnum_vareq
                CMPA    #'A'
                BLO     mnum_digchk
                CMPA    #'Z'
                BHI     mnum_digchk
                BRA     mnum_var
mnum_digchk:
                CMPA    #'0'
                BLO     mnum_none
                CMPA    #'9'
                BHI     mnum_none
                CLRA
                CLRB
                PSHS    B,A             ; 累算値
mnum_dlp:
                BSR     mml_peek
                CMPA    #'0'
                BLO     mnum_dend
                CMPA    #'9'
                BHI     mnum_dend
                LEAX    1,X
                SUBA    #'0'
                PSHS    A               ; 新しい桁
                LDD     1,S
;------------------------------------------------------------------------------
; play_mul10: D = D × 10
;------------------------------------------------------------------------------
play_mul10:
                ASLB
                ROLA                    ; ×2
                PSHS    B,A
                ASLB
                ROLA
                ASLB
                ROLA                    ; ×8
                ADDD    ,S++
                ADDB    ,S+
                ADCA    #0
                STD     ,S
                BRA     mnum_dlp
mnum_dend:
                PULS    A,B
                ANDCC   #$FE            ; C=0 (数値あり)
                RTS
mnum_vareq:
                LEAX    1,X             ; '=' を消費
mnum_var:
                JSR     impl_get_var_val ; X 前進, FAC1 = 変数値
                JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
                LBEQ    err_tmm         ; 文字列変数は Type Mismatch
                JSR     impl_fac_to_addr ; MML の値はアドレスと同じ規約 (-65536..65535)
                PSHS    B,A
                JSR     mml_peek
                CMPA    #';'
                BNE     mnum_vns
                LEAX    1,X             ; 終端 ';' を消費
mnum_vns:
                PULS    A,B
                ANDCC   #$FE
                RTS
mnum_none:
                ORCC    #$01            ; C=1 (数値なし)
                RTS


;------------------------------------------------------------------------------
; play_udiv16: D = D ÷ p_div (符号なし 16bit, 余り切捨て)。X/Y 保存。
;------------------------------------------------------------------------------
play_udiv16:
                PSHS    D,X
                CLRA
                CLRB                    ; D = 剰余
                LDX     #16
pud_lp:
                ASL     1,S
                ROL     ,S
                ROLB
                ROLA
                CMPD    p_div
                BLO     pud_no
                SUBD    p_div
                INC     1,S             ; 商ビットを立てる
pud_no:
                LEAX    -1,X
                BNE     pud_lp
                PULS    D,X,PC          ; D = 商

;------------------------------------------------------------------------------
; play_enq: 現在の声 (p_voice) のキューへ 1 エントリ積む。
;   入力: A=b0, B=b1, Y=b2b3 (音長 or 制御値)
;   キュー満杯 (16) の間は割込みの消費を待つ (バックグラウンド演奏中)。
;   待ちには上限を置く: 刻みが 1 度も届かない構成でも制御が BASIC へ戻るように
;   する (上限に達したら演奏を諦める)。
;   head/cnt の更新は短い割込みマスク区間で行う。A/B/X/Y 保存。
;------------------------------------------------------------------------------
play_enq:
                PSHS    A,B,Y           ; 0,S=b0 1,S=b1 2,S=b2b3
                ; ここから BASIC は刻みを要る。周期タイマ割込の許可を所有する
                ; (所有規約は 7t_time.inc 頭書き)。満杯待ち penq_wait は割込みが
                ; キューを消費して初めて明けるので、待ちに入る前に取っておく。
                ; 所有だけでなく「ROM 側 tick 処理の許可」も戻す (time_hw_claim)。
                ;   委譲先が呼出元へ戻らずウォームスタートへ飛ぶと控えの許可が
                ;   倒れたまま実行が再開し、刻みは入るのに tick ハンドラが ROM 側
                ;   プレイヤへ入らないため、下の満杯待ちが永久に明けなくなる。
                JSR     time_hw_claim
                LDB     >p_voice
                LDA     #6
                MUL
                ADDD    #VSTATE
                TFR     D,U             ; U = 声ステート
                ; 満杯待ちの上限計数に X を借りる (呼出元の X はここで退避し、
                ; 積み終わり penq_room 末尾で戻す)。B は入口で退避済み。
                PSHS    X
                LDB     #24             ; 外周の回数。内周 65536 回 × 24 ≒ 30 秒
penq_outer:
                LDX     #0
penq_wait:
                ORCC    #$10
                LDA     3,U             ; cnt
                CMPA    #16
                BLO     penq_room
                ANDCC   #$EF            ; 満杯: 一旦開けて割込みに消費させる
                LEAX    -1,X
                BNE     penq_wait
                DECB
                BNE     penq_outer
                ; 上限到達 = 刻みが 1 度も届いていない (割込入口スロットを自前の
                ; イメージへ向けたまま去った常駐機械語など)。待ち続けても明けないので
                ; 演奏を諦め、資源を返して制御を BASIC へ戻す (永久停止にしない)。
                PULS    X
                JSR     play_stop
                PULS    A,B,Y,PC
penq_room:
                LDB     >p_voice
                LDA     #64
                MUL
                ADDD    #PLAYQ
                TFR     D,X             ; X = 声のキュー基底
                LDA     2,U             ; head
                ADDA    3,U             ; + cnt
                ANDA    #$0F
                ASLA
                ASLA
                LEAX    A,X             ; X = 空きスロット
                LDA     2,S             ; b0 (PSHS X の後は +2)
                STA     ,X
                LDA     3,S             ; b1
                STA     1,X
                LDD     4,S             ; b2b3
                STD     2,X
                INC     3,U             ; cnt++
                LDA     #1
                STA     >play_active
                PULS    X
                ANDCC   #$EF
                PULS    A,B,Y,PC

;==============================================================================
; play_irq_tick — タイマ割込プレイヤ本体 (RAM 側 tick ハンドラから JMP される)
;   play_active 非 0 のときのみ入る。処理後 RTI。
;   各声: 残 tick 減算 → 0 でキューから取り出して PSG 設定。
;   キューが尽きた声は音量 0 (一度だけ)。全声停止で play_active を下ろす。
;==============================================================================
play_irq_tick:
                LDY     #VSTATE
                LDX     #PLAYQ
                CLRB
pit_vloop:
                PSHS    B,X             ; 0,S=声番号 1,S=キュー基底
                LDD     ,Y              ; 残 tick
                BEQ    pit_idle
                SUBD    #1
                STD     ,Y
                LBNE    pit_vnext       ; 発音継続
                ; ちょうど終了: 続きがあれば取出し、無ければ消音 (一度だけ)
                LDA     3,Y
                BNE    pit_deq
pit_sil:
                LDA     ,S              ; 声番号
                ADDA    #8
                CLRB
                JSR     impl_psg_write  ; R8+v = 0
                LBRA    pit_vnext
pit_idle:
                LDA     3,Y             ; cnt
                LBEQ    pit_vnext
pit_deq:
                LDA     3,Y
                BEQ     pit_sil         ; 制御だけ消費して尽きた → 消音
                LDU     1,S             ; キュー基底
                LDA     2,Y             ; head
                ASLA
                ASLA
                LEAU    A,U             ; U = 先頭スロット
                LDA     2,Y
                INCA
                ANDA    #$0F
                STA     2,Y             ; head++
                DEC     3,Y             ; cnt--
                LDA     ,U
                CMPA    #$FF
                BNE     pit_note
                ; --- 制御エントリ ---
                LDA     1,U
                BEQ     pit_ctlV
                CMPA    #1
                BEQ     pit_ctlS
                ; M: エンベロープ周期 → R11(下位)/R12(上位) 即時反映
                LDA     #11
                LDB     3,U
                JSR     impl_psg_write
                LDA     #12
                LDB     2,U
                JSR     impl_psg_write
                BRA     pit_deq
pit_ctlV:
                LDA     3,U
                STA     4,Y             ; 音量更新
                LDA     #$FF
                STA     5,Y             ; S 指定解除 (V 再指定で解除の仕様)
                BRA     pit_deq
pit_ctlS:
                LDA     3,U
                STA     5,Y             ; S パターン設定
                BRA     pit_deq
pit_note:
                LDD     2,U             ; 音長 tick
                STD     ,Y
                LDA     ,U
                ORA     1,U
                BNE     pit_tone
                ; 休符: 音量 0
                LDA     ,S
                ADDA    #8
                CLRB
                JSR     impl_psg_write
                BRA     pit_vnext
pit_tone:
                LDA     ,S              ; 声番号 v
                ASLA                    ; R(2v) = 分周値下位
                LDB     1,U
                JSR     impl_psg_write
                LDA     ,S
                ASLA
                INCA                    ; R(2v+1) = 分周値上位
                LDB     ,U
                ANDB    #$0F
                JSR     impl_psg_write
                LDA     5,Y             ; S 指定中?
                CMPA    #$FF
                BEQ     pit_vol
                TFR     A,B
                LDA     #13             ; エンベロープ形状 (音ごとに再書込)
                JSR     impl_psg_write
                LDA     ,S
                ADDA    #8
                LDB     #$10            ; bit4=1: エンベロープ従属 (V 無効)
                JSR     impl_psg_write
                BRA     pit_vnext
pit_vol:
                LDA     ,S
                ADDA    #8
                LDB     4,Y             ; 音量 (V)
                JSR     impl_psg_write
pit_vnext:
                PULS    B,X
                LEAY    6,Y
                LEAX    64,X
                INCB
                CMPB    #3
                LBLO    pit_vloop
                ; 全声終了判定 (残 tick=0 かつ cnt=0 が 3 声そろったら停止)
                ; 注: LDD は B (D の下位) を壊すため、ループ制御はカウンタでなく
                ;     X の終端比較で行う
                LDX     #VSTATE
pit_chk:
                LDD     ,X
                BNE     pit_ret
                LDA     3,X
                BNE     pit_ret
                LEAX    6,X
                CMPX    #VSTATE+18      ; 3 声 × 6B
                BLO     pit_chk
                CLR     >play_active
                BSR     play_silence
                JSR     time_hw_sync    ; 演奏終了 → 刻みが要らなくなれば所有を返す
pit_ret:
                RTI

;------------------------------------------------------------------------------
; play_silence: 3 声の音量を 0 にする (R8-R10 = 0)。壊す: A,B (CC)
;------------------------------------------------------------------------------
play_silence:
                CLRB
                LDA     #8
                JSR     impl_psg_write
                LDA     #9
                JSR     impl_psg_write
                LDA     #10
                JMP     impl_psg_write

;==============================================================================
; play_stop — 演奏の全停止 + PSG 初期状態復帰 (参考書籍の中断処理)
;   キュー破棄・音量 0・ミキサ初期値。全レジスタ保存 (フック挿入用)。
;   コールドスタートの初期化も本ルーチンを使う。
;==============================================================================
play_stop:
                PSHS    CC,A,B,X
                ORCC    #$10
                CLR     >play_active
                LDX     #VSTATE
                LDB     #3
pst_lp:
                CLR     ,X              ; 残 tick = 0
                CLR     1,X
                CLR     2,X             ; head = 0
                CLR     3,X             ; cnt = 0
                LDA     #4
                STA     4,X             ; 音量は既定値 4 (参考書籍)
                LDA     #$FF
                STA     5,X             ; S 指定解除
                LEAX    6,X
                DECB
                BNE     pst_lp
                BSR     play_silence
                LDD     #$07F8                  ; レジスタ 7 = ミキサ
                JSR     impl_psg_write
                JSR     time_hw_sync    ; 演奏停止 → 刻みが要らなくなれば所有を返す
                PULS    CC,A,B,X,PC

;==============================================================================
; tok_match_kw2: 二次予約語 (kw_2nd_tbl: CHAIN/ERASE/LPRINT/LLIST/PLAY/SOUND)
;   のトークン化。tok_match_kw と同形で、トークン値は TOK_2ND_FIRST 起点。
;   出力: 成功 → Z=1, X=語末次, U にトークン 1 バイト書込/前進。
;         失敗 → Z=0, X/U 不変。
;==============================================================================
tok_match_kw2:
                ; 走査本体は 7t_tokenizer.inc の tmk_outer を共用する (従来の
                ; 専用走査 tmk2_* は撤去。共用化で語頭昇順の早期打切りと
                ; 小文字畳み込みが二次語にも効く)。二次命令語のトークン
                ; ($EE-$F3) は $FF 前置なし — tmk_matched が B の値域で判別。
                PSHS    Y,X
                TFR     X,Y
                LDX     #kw_2nd_fn_tbl  ; kw_2nd_tbl の直後 = 表終端
                PSHS    X
                LDX     #kw_2nd_tbl
                LDB     #TOK_2ND_FIRST
                JMP     tmk_outer_first ; 入力 1 文字目の畳みを設定してから走査へ

;------------------------------------------------------------------------------
; エラー出口
;------------------------------------------------------------------------------
