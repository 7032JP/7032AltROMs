; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 7t_core.s — 7T-BASIC 3.1 の組み上げ (最上位ソース)
;
; ROM イメージは $8000 を基底に組み、先頭 31744 byte ($8000-$FBFF) を切り出して
; メイン CPU 側の BASIC ROM とする。本ファイルは各モジュールを取り込む順序だけを
; 決める。順序は次の 2 点で定まる。
;   - アドレスを固定する入口・共有領域・ベクタを、それぞれの決められたアドレスに
;     置けること (各モジュール内の ZMB がそのアドレスまで詰める)。
;   - アドレスを固定しないコード・表は、その手前の区画 (ZMB が吸収する空き) に
;     収まること。参照はいずれもラベル経由で、位置には依存しない。
;
; 末尾には公開 JMP テーブルと、BIOS 間接ベクタのワード (2 バイト) $FBFA
; (参考書籍) を置く。参考書籍の固定入口は各モジュールに置く。
; このベクタの手前には、当方の起動 ROM と本 ROM の取り決めである目印の
; ワード (2 バイト) と起動ベクタ 2 ワードを置く (当方が選んだアドレス)。
;==============================================================================
                ORG     $8000

                include "7t_startup.inc"
;==============================================================================
; ディスク側システム (DISK モード) 起動連携 — 入口/FDD サービス
;   (起動連携の受け口と各サービス入口をまとめて置く)
;==============================================================================
                include "7t_diskboot.inc"
                include "7t_arrays.inc"
                include "7t_color_video.inc"
                include "7t_evalhelp.inc"
                include "7t_time.inc"
                include "7t_play.inc"
                include "7t_deffn.inc"
                include "7t_strgc.inc"          ; 文字列領域の詰め直し

                include "7t_runtime.inc"
                include "7t_statements.inc"
                include "7t_cmt_io.inc"
                include "7t_float.inc"
                include "7t_save_list.inc"
                include "7t_expr.inc"
                include "7t_vars_print.inc"
                include "7t_tokenizer.inc"
                include "7t_funcs.inc"
                include "7t_using_nscan1.inc"   ; PRINT USING 書式スキャナ 1/2 (プール)
                include "7t_keyarg.inc"         ; KEY 引数前処理 (プール)
                include "7t_strown.inc"         ; 文字列の所有 (容量の都合でここへ置く)

; eval_to_int_round — 式を評価し、整数を要求する文脈の規約で整数化する。
;   eval_to_int (ゼロ方向切り捨て・範囲検査なし) と違い、最近接丸めを行い、
;   整数の範囲 (-32768..32767) を外れたら Overflow とする。配列の添字や文字列
;   の位置指定のように「整数でなければならない」場所で使う。
;   (置き場所は容量の都合。参照はラベル経由のみで位置依存はない)
eval_to_int_round:
                JSR     impl_eval_expr
                JSR     chk_num         ; 文字列を渡されたら Type Mismatch
                JMP     impl_fac_to_int_round
                include "7t_strreloc.inc"       ; スタックの移動 (区画の都合でここへ)
                include "7t_strclr.inc"         ; 変数領域と一時の開放

;==============================================================================
; brk_gate / brk_stop — 中断キーの検査と中断停止
;
;   中断キーの押下は $FD04 (PORT_FIRQ) の bit1 に現れる (0=押下, 1=非押下)。
;   FM-7 のハードウェア仕様に基づく読み取りで、参考書籍
;   の中断操作 (走行中のプログラムを止めて直接モードへ戻す) を実現する。
;
;   brk_gate は「文の切れ目」から呼ばれる。実行ループ (iep_stmt_gate) と直接
;   モード (cmd_direct_stmt) はどちらも文の切れ目で一時文字列を開放するので、
;   その呼出をここへ通し、検査 → 通過なら本来の開放へ末尾呼びする。呼出の数を
;   増やさないため、走行速度への影響は 1 文あたり数命令にとどまる。
;
;   押下時は brk_stop へ落ちる。停止位置は CURIP (これから実行する文の先頭) で、
;   CONT はその文からやり直す。カセットの走行 (モータ) は中断とともに止める
;   (読取待ちからの中断でも巻き取りが続かないようにする)。
;   表示と再開位置の記録は STOP と同じ経路 (impl_stop) が受け持つ。
;==============================================================================
; (brk_gate / brk_stop の本体は容量の都合で 7t_cmt_io.inc の空きプールに置く)

;--- 制御移行ヘルパ ---
; impl_jump_to_line: 行レコード先頭 X へ実行を移す。
;   入力: X = 行レコード先頭。CURLINE_PTR を更新し CURIP=トークン先頭。X も前進。
;   GOTO/GOSUB/ON 後に行末で iep_next_line が正しい次行を辿れるようにする。
; gosub_push: GOSUB / ON GOSUB / 割込の GOSUB の枠 (5B) を枠列へ積む。
;   入力: X = 復帰 IP。溢れは frame_ins が Out Of Memory (戻らない)。X 保存。
;   gw_push: B = 種別 (TAG_GOSUB / TAG_WHILE) を指定する入口。破壊: A,B,U。
gosub_push:
                LDB     #TAG_GOSUB
gw_push:
                PSHS    X,B
                LDB     #FRM_SMALL
                JSR     frame_ins       ; U = 枠 (S〜EXEC_SP の呼出フレームは移動済)
                PULS    B,X
                STB     ,U
                STX     1,U
                LDD     CURLINE_PTR
                STD     3,U
                RTS

;--- 起動ベクタ (当方の起動 ROM との取り決め) ---
; 目印 ("7T") の直後にコールドスタートと二次入口のベクタを置く (BASVEC_SIG)。
; 二次入口はディスク起動が成立しなかったときの入口で、中身はコールドスタートと
; 同じ。起動の間接ベクタ $FBFC / $FBFE もコールドスタートを指す。

;==============================================================================
; RENUM — 行番号の付け直し
;
;   書式: RENUM [新行番号][,[旧行番号][,増分値]]
;     既定値: 新行番号 10、旧行番号 = 先頭の行、増分値 10。
;     GOTO / GOSUB / THEN / ON / ERL の参照先も付け直す。無い行を参照して
;     いたら Undefined Line Number (参照先と新しい行番号を添える) を出し、
;     その参照は残す。実行後はコマンドレベルへ戻る。
;     63999 を越える・順序が入れ替わる指定は Illegal Function Call。
;
;   本実装の格納は ASCII 原文の行リンク ([次行ptr 2B][行番号 2B][本文][00])
;   なので、参照している行番号は本文中の 10 進数字列である。桁数が変わると
;   本文の長さが変わるため、置換のたびに以降の本文を動かして次行ptr を補正
;   する。本文が伸縮すると変数域の位置が変わるので、行の挿入・削除と同じく
;   全変数を消去する。
;==============================================================================
impl_renum:
                LDD     #10
                STD     rn_new                  ; 省略値
                STD     rn_inc                  ; 省略値
                CLRA
                CLRB
                STD     rn_old                  ; 省略値 = 最初の行 (0 で全行)
                ; 3 個の引数は隣り合わせに置いてあるので順に読み込む
                LDU     #rn_new
rn_a1:
                JSR     rn_arg
                BCC     rn_a2
                STD     ,U
rn_a2:
                LEAU    2,U
                CMPU    #rn_ptr                 ; 3 個読み終えた
                BEQ     rn_top
                JSR     skip_comma
                BCS     rn_a1
rn_top:
                ; --- 付け直しの先頭行 X と、その直前の行 U を求める ---
                LDU     #0
                LDX     TXTTAB
rn_t1:
                LDD     ,X
                LBEQ    rn_fin                  ; 対象の行が無い → 何もしない
                LDD     2,X
                CMPD    rn_old
                BHS     rn_t2
                TFR     X,U
                LDX     ,X
                BRA     rn_t1
rn_t2:
                LEAU    ,U
                BEQ     rn_t3
                LDD     2,U                     ; 直前の行の行番号
                CMPD    rn_new
                BLO     rn_t3
rn_ill:
                JMP     errc_5                  ; 順序が変わる指定
rn_t3:
                ; --- 63999 を越える行番号を生じないことを確かめる ---
                TFR     X,U
                LDD     rn_new
rn_t4:
                LDY     ,U
                BEQ     rn_scan
                CMPD    #64000
                BHS     rn_ill
                ADDD    rn_inc
                BCS     rn_ill
                TFR     Y,U
                BRA     rn_t4
rn_scan:
                ; --- 参照している行番号を付け直す ---
                JSR     vars_clear_all          ; 本文が伸縮するので変数を消す
                LDY     TXTTAB
rn_ln:
                LDD     ,Y
                LBEQ    rn_hdr
                LEAX    4,Y                     ; X = 本文 (ASCII 原文)
                BRA     rn_sc
rn_lnx:
                LDY     ,Y                      ; 次の行へ
                BRA     rn_ln
rn_sc:
                LDA     ,X
                BEQ     rn_lnx
                CMPA    #'"'
                BNE     rn_sk
                LEAX    1,X                     ; 文字列定数はそのまま読み飛ばす
rn_sq:
                LDA     ,X+
                BEQ     rn_lnx
                CMPA    #'"'
                BNE     rn_sq
                BRA     rn_sc
rn_sk:
                LDU     #sym_buf                ; 照合結果の捨て場
                JSR     tok_match_kw
                BNE     rn_id                   ; 予約語でない
                LDA     sym_buf
                CMPA    #TOK_GO
                BEQ     rn_go
                CMPA    #TOK_THEN
                BEQ     rn_num
                CMPA    #TOK_ERL
                BEQ     rn_erl
                CMPA    #TOK_REM
                BEQ     rn_lnx                  ; 注記の後は原文 → 行の走査を終える
                CMPA    #TOK_REM_APOS
                BEQ     rn_lnx
                CMPA    #TOK_DATA
                BEQ     rn_lnx                  ; DATA の値も原文
                BRA     rn_sc
rn_go:
                JSR     skip_sp_x               ; GOTO / GOSUB
                LDU     #sym_buf
                JSR     tok_match_kw
                BNE     rn_sc
                LDA     sym_buf
                CMPA    #TOK_TO
                BEQ     rn_num
                CMPA    #TOK_SUB
                BNE     rn_sc
rn_num:
                ; 行番号のならび (ON 〜 GOTO / ON 〜 GOSUB はコンマ区切り)
                JSR     skip_sp_x
                SUBA    #'0'
                CMPA    #9
                BHI     rn_sc
                JSR     rn_fix
                JSR     skip_comma
                BCS     rn_num
                BRA     rn_sc
rn_erl:
                ; ERL に続く比較演算子 ('<' '=' '>' は隣り合う符号) を読み飛ばす
                JSR     skip_sp_x
                SUBA    #'<'
                CMPA    #2
                BHI     rn_sc
                LEAX    1,X
                BRA     rn_erl
rn_id:
                ; 予約語に当たらなかった 1 字を消費する。英字なら名前の先頭なので
                ; 続く英数字も名前の一部として読み飛ばす (名前の途中に予約語の
                ; 綴りが現れても分解しない。行変換器と同じ規則)。
                LDA     ,X+
                ANDA    #$DF                    ; 小文字を大文字へ畳む
                SUBA    #'A'
                CMPA    #26
                BHS    rn_sc                   ; 英字以外 → 1 字だけ消費
rn_id1:
                LDA     ,X
                CMPA    #'0'
                BLO     rn_id2
                CMPA    #'9'
                BLS     rn_id3
rn_id2:
                ANDA    #$DF
                SUBA    #'A'
                CMPA    #26
                LBHS    rn_sc
rn_id3:
                LEAX    1,X
                BRA     rn_id1
rn_hdr:
                ; --- 行番号そのものを付け直す (本文の走査で位置が動いたので
                ;     先頭行はここで求め直す) ---
                LDX     TXTTAB
rn_h0:
                LDD     ,X
                BEQ     rn_fin
                LDD     2,X
                CMPD    rn_old
                BHS     rn_h1
                LDX     ,X
                BRA     rn_h0
rn_h1:
                LDD     rn_new
rn_h2:
                LDU     ,X
                BEQ     rn_fin
                STD     2,X
                ADDD    rn_inc
                TFR     U,X
                BRA     rn_h2
rn_fin:
                JSR     cont_clear              ; 本文が動いたので再開位置は無効
                JMP     warm_start              ; コマンドレベルへ戻る

;------------------------------------------------------------------------------
; rn_arg — 省略できる行番号の引数を 1 個読む。
;   出力: C=1 … D = 行番号 (X は直後) / C=0 … 省略 (X は据置き)
;------------------------------------------------------------------------------
rn_arg:
                JSR     skip_sp_x
                SUBA    #'0'
                CMPA    #9
                BHI     rn_ano
                JSR     parse_prog_linenum      ; 上限 63999 の検査つき
                ORCC    #$01
                RTS
rn_ano:
                ANDCC   #$FE
                RTS

;------------------------------------------------------------------------------
; rn_map — 旧行番号 D を新しい行番号へ写す。
;   出力: C=0 … D = 新行番号 / C=1 … その行番号は存在しない (D は不変)
;   X / Y は保存する。
;------------------------------------------------------------------------------
rn_map:
                PSHS    D,X,Y,U                 ; 0,S = 探している旧行番号 (兼 返り値)
                LDY     rn_new                  ; Y = 走査中の新行番号
                LDX     TXTTAB
rn_m1:
                LDU     ,X
                BEQ     rn_mnf
                LDD     2,X
                CMPD    ,S
                BEQ     rn_mhit
                CMPD    rn_old
                BLO     rn_m2                   ; 対象の手前 → 番号は変わらない
                LDD     rn_inc
                LEAY    D,Y
rn_m2:
                TFR     U,X
                BRA     rn_m1
rn_mhit:
                CMPD    rn_old
                BLO     rn_mkeep                ; 対象の手前 → 番号は変わらない
                TFR     Y,D
                STD     ,S
rn_mkeep:
                ANDCC   #$FE
                PULS    D,X,Y,U,PC
rn_mnf:
                ORCC    #$01
                PULS    D,X,Y,U,PC

;------------------------------------------------------------------------------
; rn_put — print_uint16 の出力先 (out_vec)。10 進の綴りを rn_ptr へ積む。
;   out_char が X/Y/U を保存するので、ここでは保存しなくてよい。
;------------------------------------------------------------------------------
rn_put:
                LDX     rn_ptr
                STA     ,X+
                STX     rn_ptr
                RTS

;------------------------------------------------------------------------------
; rn_fix — 本文中の 10 進行番号 1 個を新しい行番号へ置き換える。
;   入力: X = 数字列の先頭, Y = その行のレコード先頭。
;   出力: X = 置き換えた綴りの直後。Y は保存する。
;------------------------------------------------------------------------------
rn_fix:
                PSHS    X                       ; 数字列の先頭
                JSR     pn2_uint                ; D = 参照している行番号
                PSHS    X                       ; 数字列の直後
                BSR     rn_map
                BCS     rn_undef
                ; 新しい行番号を 10 進の綴りへ直す
                LDU     #rn_dbuf
                STU     rn_ptr
                LDU     #rn_put
                STU     out_vec
                JSR     print_uint16    ; out_vec は次に使う側が張り替える規約
                LDD     rn_ptr
                SUBD    #rn_dbuf                ; D = 新しい綴りの長さ
                ADDD    2,S                     ; D = 置換後の綴りの直後
                PSHS    D                       ; 0,S 新末尾 2,S 旧末尾 4,S 先頭
                SUBD    2,S                     ; D = 伸縮量
                BEQ     rn_cp
                BMI     rn_shr
                ; --- 伸ばす: 本文の残りを後ろから動かす ---
                LDU     VARTAB                  ; コピー元 (排他)
                LEAX    D,U                     ; コピー先 (排他)
                STS     crs_oldtop              ; 上限 = システム S (枠列と直接モードの行はその上)
                CMPX    crs_oldtop
                BHS     rn_oom
                STX     VARTAB
rn_gl:
                CMPU    2,S
                BEQ     rn_lnk
                LDA     ,-U
                STA     ,-X
                BRA     rn_gl
rn_shr:
                ; --- 縮める: 本文の残りを前から動かす ---
                LDU     2,S                     ; コピー元 = 旧末尾
                LDX     ,S                      ; コピー先 = 新末尾
rn_sl:
                CMPU    VARTAB
                BEQ     rn_sl2
                LDB     ,U+
                STB     ,X+
                BRA     rn_sl
rn_sl2:
                STX     VARTAB
rn_lnk:
                ; 次行ptr を伸縮量ぶん補正する (当該行から終端まで)
                TFR     Y,U
rn_ll:
                LDX     ,U
                BEQ     rn_cp
                LDD     ,S
                SUBD    2,S                     ; D = 伸縮量
                LEAX    D,X
                STX     ,U
                TFR     X,U
                BRA     rn_ll
rn_cp:
                ; 新しい綴りを書き込む
                LDX     4,S
                LDU     #rn_dbuf
rn_cl:
                CMPX    ,S
                BEQ     rn_cd
                LDA     ,U+
                STA     ,X+
                BRA     rn_cl
rn_cd:
                LEAS    6,S
                RTS
rn_oom:
                JMP     err_rg                  ; Out of Memory
rn_undef:
                ; 存在しない行番号は表示だけ行い、綴りはそのまま残す (参考書籍)
                LDU     #impl_putchar
                STU     out_vec
                PSHS    D                       ; 存在しなかった行番号
                LDX     #msg_undef_ln
                JSR     print_str7      ; 語末 bit7 形式
                JSR     put_space               ; 空白 1 個を出す
                PULS    D
                JSR     print_uint16
                JSR     print_inline
                ; 誤りの表示は他の誤りと同じ体裁 (前置きの大文字) に揃える
                FCC     " In "
                FCB     0
                LDD     2,Y                     ; その文の行番号
                JSR     rn_map                  ; → 新しい行番号
                JSR     print_uint16
                JSR     disk_crlf
                LDX     ,S                      ; 数字列の直後から走査を続ける
                LEAS    4,S
                RTS
rn_dbuf         EQU     str_work_buf+$D0               ; 10 進の綴り (最大 5 桁) の組立先

;==============================================================================
; 機能キー割込 (参考書籍)
;
;   書式: KEY(n) { ON | OFF | STOP } / ON KEY(n) GOSUB 行番号 (n = 1〜10)
;     ON で受付け、OFF で禁止、STOP では発生を 1 件保留して次の ON で呼ぶ。
;     RUN で全キーを OFF にする。戻りは RETURN (通常の GOSUB 枠で入る)。
;
;   ハードウェア側:
;     - PF 割込みで共有 RAM の STATUS の bit0〜3 に PF 番号が入る
;     - Interrupt Control ($2C) の相対 3,4 の下位 10 ビットが PF1〜PF10 の
;       受付けの印 (RESET 時は $0000)
;     - 受け付けたらメイン CPU が STATUS の割込原因のビットを 0 に戻す
;       (bit7 は変えない)
;     - ATTENTION は FIRQ 要因で、$FD04 の bit0 に現れる (0=あり)
;
;   分岐そのものは INTERVAL 割込と同じ仕組みに相乗りする。文の切れ目
;   (iep_stmt_gate) と行ジャンプ直後 (iep_jump_gate) でのみ分岐し、
;   GOSUB 枠を積んで登録行へ移る。ハンドラ実行中は自動保留 (intv_st bit2 を
;   事象ハンドラ共通の「実行中」の印として使う。RETURN が倒す) とし、
;   割込みの入れ子を作らない。
;==============================================================================

; key_mask — キー番号 → ビットパターン。入力 B = 1..10、出力 D = 1<<(B-1)。破壊: CC
key_mask:
                PSHS    B
                LDD     #1
km_lp:
                DEC     ,S
                BEQ     km_done
                ASLB
                ROLA
                BRA     km_lp
km_done:
                LEAS    1,S
                RTS

; key_send_ic — 現在の割り込みマスクの値をサブシステムへ通知する (Interrupt Control $2C)。
;   パラメータの並びは参考書籍のとおり相対 3,4 の 16 ビット。
;   impl_sub_send_cmd はレジスタを保存するので、呼出元の本文ポインタは無事。
key_send_ic:
                LDD     >key_arm
                STD     SUBCMD_P1       ; 相対 3,4 = 割込制御フラグ (16bit)
                LDA     #$2C
                JMP     sub_send_cmd_a

; key_reset — 機能キー割込の全状態を初期化する (登録行・割り込みマスク・保留)。
;   コールドスタート (time_init 経由) と RUN 毎 (iep_start_x 経由) に
;   intv_reset から呼ぶ。割り込みマスクの値が 0 でないときだけサブシステムへ全禁止を送る。
;   破壊: A, B, X。
key_reset:
                LDA     >key_arm
                ORA     >key_arm+1      ; 直前に許可していたキーがあったか
                PSHS    A
                LDX     #key_line
                LDB     #50             ; 表の KEY(n) の欄 10 件 (5B×10)
                BSR     krs_clr
                LDX     #key_arm
                LDB     #2              ; 割り込みマスク 2B
                BSR     krs_clr
                PULS    A
                TSTA
                BNE     key_send_ic     ; 許可があった → 全禁止 ($0000) を送る
                RTS
krs_clr:
                JMP     mz_l            ; B バイトのゼロ詰め (多倍長ヘルパと共用)

; key_attn_poll — 受領済の ATTENTION を消費し、共有 RAM の STATUS から
;   PF 割込コードを取り出して保留 (key_pend) へ積む。文の切れ目と行ジャンプ
;   直後から呼ぶので X (文の先頭) を温存する。
;
;   通知線は参考書籍のとおり $FD04 の 1 本のリード専用フラグで、ATTENTION
;   (bit0) と中断キー (bit1) の 2 要因が相乗りしており、「0=割込みあり」の表示は
;   一度読むと落ちる。中断キーの検査 (brk_active / brk_gate) が同じレジスタを
;   絶えず読むため、ここで直接読むと先に読まれた ATTENTION を取りこぼす。
;   よって $FD04 を読む場所は firq_read 1 つに集約し、そこが控え (key_attn) へ
;   積んだものをここで消費する。
;   共有 RAM を読むには HALT が要るが、サブがコマンド処理中 (BUSY) の間に
;   HALT を挟むと処理中のコマンドを壊すので、BUSY が降りるまで見送る
;   (控えは残るので次の機会に必ず取り込まれる)。
;   受け付けた割込原因のビットは 0 へ戻し、継続フラグ (bit7) は変更しない
;   (サブシステムは STATUS のビットを立てるだけなので、落とすのはメイン CPU の
;   役目である。参考書籍)。破壊: A, B。
key_attn_poll:
                JSR     firq_read       ; $FD04 を見て ATTENTION を控えへ積む
                TST     >key_attn       ; 受領済のものがあるか
                BNE     kap_look
                TST     >timer_st       ; TIME ON 中は STATUS の bit4 を見に行く
                BEQ     kap_rts         ;   (参考書籍)
kap_look:
                LDA     PORT_SUBCPU     ; $FD05 (bit7 = BUSY)
                BMI     kap_rts         ; 処理中 → 見送る (控えは残す)
                PSHS    X
                JSR     ssc_halt        ; 共有 RAM を読むので HALT を確立する
                BSR     kap_take        ; STATUS から PF 割込コードを取り込む
                JSR     ssc_atn_run     ; サブを待機ループ先頭へ戻して RUN
                                        ;   (BUSY=0 を確かめてから HALT したので
                                        ;    SH_CMD は 0。ATN を立てないとサブは
                                        ;    待機ループの途中から再開して BUSY を
                                        ;    落とさず、以後の発行が永久待ちになる)
                PULS    X
kap_rts:
                RTS

; kap_take — HALT 中に共有 RAM の STATUS (相対 1) を読み、PF 割込コードを保留
;   (key_pend) へ積み、受け付けた割込原因のビットを 0 へ戻す。継続フラグ (bit7)
;   は変更しない (参考書籍)。呼ぶのは HALT を確立した場所だけ。
;   コマンド発行 (impl_sub_send_cmd) からも呼ぶ。発行はステージング 128 バイトを
;   共有 RAM へ一括複写するため、複写の前に取り込まないと相対 1 の STATUS ごと
;   潰してしまい、発生を取り逃がす。破壊: A, B。
;
;   STATUS の割込原因は参考書籍の
;   とおり bit0〜3 = PF キー番号、bit4 = タイマ、bit5 = インターバルタイマ、
;   bit6 = 0 時。PF キー・タイマ・インターバルは BASIC の割込みの表 (KEY(n) /
;   ON TIME / ON INTERVAL) の欄へ積む。0 時 (bit6) に対応する文は 参考書籍に
;   無いので、原因のビットを 0 へ戻すだけで欄へは積まない。
kap_take:
                CLR     >key_attn       ; 控えを消費
                LDA     SUBCMD_BASE+1   ; STATUS ($FC81)
                TFR     A,B
                ANDA    #$80            ; 継続フラグだけ残す
                STA     SUBCMD_BASE+1   ;   = 割込原因のビットを 0 へ戻す
                BITB    #$10            ; bit4 = TIMER (割込予約時刻に達した)
                BEQ     kt_notimer
                INC     >timer_pend     ; +2 = 割込があった
kt_notimer:
                BITB    #$20            ; bit5 = インターバルタイマ
                BEQ     kt_nointv
                INC     >intv_pend      ; +2 = 割込があった (ON INTERVAL GOSUB の欄)
kt_nointv:
                ANDB    #$0F            ; ビット 0〜3 = PF 割込コード
                BEQ     kt_rts          ; PF 以外の要因 (bit4〜6 のタイマ等)
                CMPB    #10
                BHI     kt_rts          ; 1〜10 の外は取らない
                DECB
                LDA     #5              ; 1 件 5B (参考書籍の表の形)
                MUL
                LDX     #key_line
                LEAX    D,X             ; X = そのキーの欄
                LDA     #1
                STA     2,X             ; +2 = 割込があった
kt_rts:
                RTS

; ijg_key — 機能キー割込の分岐判定と GOSUB 突入 (iep_jump_gate から落ちて来る)。
;   X = これから実行する文の先頭。分岐しないときは X を温存して RTS。
;   STOP 中のキーは保留を残したまま戻る (次の KEY(n) ON で分岐する)。
ijg_key:
                LDD     >key_arm        ; 許可しているキーが 1 つも無ければ走査しない
                BEQ     ijgk_rts
                LDU     #key_line
ijgk_lp:
                LDA     2,U             ; +2 = 割込があった
                BNE     ijgk_chk
ijgk_nx:
                LEAU    5,U             ; 1 件 5B (参考書籍の表の形)
                CMPU    #key_line+50
                BNE     ijgk_lp
ijgk_rts:
                RTS
ijgk_chk:
                TST     ,U              ; +0 = ON 状態か
                BEQ     ijgk_drop       ;   OFF → 発生分は捨てる
                TST     1,U             ; +1 = STOP / 割込処理ルーチン実行中か
                BNE     ijgk_nx         ;   保留はそのまま持ち越す
                LDD     3,U             ; +3,+4 = 登録された飛び先行番号
                BEQ     ijgk_drop       ; 未登録 → 発生分は捨てる
                PSHS    D
                CLR     2,U             ; 保留は 1 件に畳んで分岐
                LDA     #2
                STA     1,U             ; 割込処理ルーチン実行中 (RETURN が倒す)
                JSR     gosub_push      ; 復帰枠 = 中断する文の先頭 (X)。溢れは Out Of Memory
                PULS    D
                JSR     impl_find_line  ; D = 登録行 → X = 行レコード
                LBEQ    isg_undef       ; 実行中に消えていた → Undefined line number
                JMP     impl_jump_to_line
ijgk_drop:
                CLR     2,U
                BRA     ijgk_nx

; impl_key_int — KEY(ファンクションキー番号) { ON | OFF | STOP }
;   impl_key が命令語の直後に '(' を見つけたときに入る。X = '(' の位置。
;   番号は 1〜10 (外は Illegal Function Call。KEY n,"文字列" と同じ規約)。
;   引数はこの 1 つだけで、後に続けられるのは文の区切り (':' か行末) のみ。
impl_key_int:
                LEAX    1,X             ; '(' を消費
                JSR     eval_to_int_key ; B = キー番号 1..10 (外は Illegal Function Call)
                LDA     ,X+
                CMPA    #')'
                BNE     iki_syn
                PSHS    B               ; キー番号 (後で 4,S)
                JSR     key_mask        ; D = そのキーのビット
                PSHS    D               ; 2,S:3,S = ビットパターン
                COMA
                COMB
                PSHS    D               ; 0,S:1,S = その補数
                JSR     skip_sp_x       ; A = 制御語
                LEAX    1,X             ; 制御語を消費
                PSHS    A               ; 制御語を退避 (以下で D を使う)
                LDB     5,S             ; キー番号
                DECB
                LDA     #5              ; 1 件 5B (参考書籍の表の形)
                MUL
                LDU     #key_line
                LEAU    D,U             ; U = そのキーの欄
                PULS    A               ; A = 制御語
                CMPA    #TOK_ON
                BEQ     iki_on
                CMPA    #TOK_STOP
                BEQ     iki_stop
                CMPA    #TOK_OFF
                BNE     iki_syn
                ; --- OFF: 割込みを禁止する。保留していた発生分も捨てる ---
                CLR     ,U              ; +0 = ON 状態を倒す
                CLR     1,U             ; +1 = STOP を倒す
                CLR     2,U             ; +2 = 保留していた発生分を捨てる
                LDD     >key_arm
                ANDA    ,S              ; ビットパターンの補数で当該ビットだけを落とす
                ANDB    1,S
                BRA     iki_send
iki_stop:
                ; --- STOP: 分岐だけを止める。割込みの受付は続けるので、押した
                ;     ぶんは表の +2 に積まれ、次の KEY(n) ON で呼出される ---
                LDA     #1
                STA     ,U              ; +0 = ON 状態
                STA     1,U             ; +1 = STOP
                BRA     iki_arm
iki_on:
                ; --- ON: 割込みを許可し、分岐も行う ---
                LDA     #1
                STA     ,U              ; +0 = ON 状態
                CLR     1,U             ; +1 = STOP を解除
iki_arm:
                LDD     >key_arm
                ORA     2,S             ; ビットパターンで当該ビットを立てる
                ORB     3,S
iki_send:
                STD     >key_arm
                LEAS    5,S             ; ビットパターン・補数・キー番号を捨てる
                JSR     key_send_ic     ; 更新した割り込みマスクの値をサブシステムへ ($2C)
                ; 引数はここまで。続きがあれば Syntax Error
stmt_end_check:
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     iki_done        ; 行末
                CMPA    #':'
                BEQ     iki_done        ; 文の区切り
iki_syn:
                JMP     ion_err         ; Syntax Error (尾部併合)
iki_done:
                RTS

; impl_on_key — ON KEY(ファンクションキー番号) GOSUB 行番号
;   impl_on_entry が ON の次に KEY を見たときに入る。X = KEY トークンの位置。
;   行番号の存在を確かめてから、そのキーの飛び先として登録する。
;   復帰時の X は本文ポインタ (行番号の直後) でなければならない。
impl_on_key:
                JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
                CMPA    #'('
                BNE     iok_syn0
                LEAX    1,X             ; '(' を消費
                JSR     eval_to_int_key ; B = キー番号 1..10
                PSHS    B
                LDA     ,X+
                CMPA    #')'
                BNE     iok_syn
                JSR     skip_sp_take
                CMPA    #TOK_GO         ; GO
                BNE     iok_syn
                JSR     skip_sp_take
                CMPA    #TOK_SUB        ; SUB (GOSUB のみ。GOTO は不可)
                BNE     iok_syn
                JSR     skip_sp_x       ; A = 行番号の先頭
                CMPA    #'0'
                BLO     iok_syn         ; 行番号が無い
                CMPA    #'9'
                BHI     iok_syn
                JSR     impl_eval_linenum       ; D = 行番号, X 前進
                PSHS    X               ; impl_find_line は X を壊す
                JSR     impl_find_line
                PULS    X               ; (PULS は CC 不変なので Z は保たれる)
                BEQ     iok_undef
                LDU     #key_line
                PSHS    D               ; 行番号を退避 (impl_find_line は D 温存)
                LDB     2,S             ; キー番号
                DECB
                LDA     #5              ; 1 件 5B (参考書籍の表の形)
                MUL
                LEAU    D,U
                PULS    D
                STD     3,U             ; 飛び先として登録 (+3,+4)
                LEAS    1,S             ; キー番号を捨てる
                RTS
iok_syn:
                LEAS    1,S
iok_syn0:
                JMP     ion_err         ; Syntax Error (尾部併合)
iok_undef:
                LEAS    1,S
                JMP     irst_undef      ; Undefined line number (尾部併合)


;==============================================================================
; devtbl_init — 入出力デバイス登録テーブル / 論理機番対応テーブル /
;   ON〜GOSUB 割込テーブル / LPT0 ルーチンベクトルの起動時初期化
;   (参考書籍)
;
;   $06EC-$078C をまず 0 に均す。0 は物理機番の未使用、開いている論理機番
;   なし、割込の登録なしを表す。
;   その上で ROM の初期化データを配る。データは
;     [宛先 2B][長さ 1B][内容 …] … の並びで、長さ 0 で終端。
;   破壊: A, B, X, U
;==============================================================================
devtbl_init:
                LDX     #DEVTBL
dvi_clr:
                CLR     ,X+
                CMPX    #LPTVEC+21
                BNE     dvi_clr
                LDX     #dvi_data
dvi_blk:
                LDU     ,X++            ; 宛先
                LDB     ,X+             ; 長さ
                BEQ     dvi_done
dvi_cpy:
                LDA     ,X+
                STA     ,U+
                DECB
                BNE     dvi_cpy
                BRA     dvi_blk
dvi_done:
                RTS


;==============================================================================
; impl_mon / impl_term — MON / TERM コマンド
;   本 ROM はどちらの機能も持たないので、参考書籍の位置に置く MON のフック
;   ($0269) と TERM のフック ($0284) を呼んでから
;   引数を文末まで読み捨てる。フックは既定では RTS なので、拡張を置いていない
;   限り何も起こらない。
;==============================================================================
impl_mon:
                IFNDEF  STAGE_ALT_RAM
                JSR     >HOOK_MON
                ENDC
                JMP     impl_stub
impl_term:
                IFNDEF  STAGE_ALT_RAM
                JSR     >HOOK_TERM
                ENDC
                JMP     impl_stub

;==============================================================================
; sixdig_save — "nn/nn/nn" 形の 8 文字から区切りを除いた 6 けたを写す。
;   参考書籍の位置に置くタイマの参照時刻 ($02F4-$02F9) と日付
;   ($02FA-$02FF) は、どちらも 2 バイトずつ 3 組の並びである。
;   入力: X = 元 (8 文字), U = 先 (6 バイト)。破壊: A, B, X, U
;==============================================================================
sixdig_save:
                LDB     #3
sds_lp:
                LDA     ,X+
                STA     ,U+
                LDA     ,X+
                STA     ,U+
                LEAX    1,X             ; 区切り 1 文字を飛ばす
                DECB
                BNE     sds_lp
                RTS

;==============================================================================
; カセットのモータ制御 (参考書籍 MOTOR)
;
;   モータの回転は $FD00 の bit1 で切り替える。書き込み専用の制御なので、
;   MOTOR の省略形 (反転) が現在の状態を知れるよう、書いた値を cmt_motor_st に
;   控える。テープを回す・止める場所はすべてこの 2 つの入口を通す。
;   BIOS の MOTOR ($01) と、参考書籍の MOTOR ON /
;   MOTOR OFF も同じ入口を使う。
;   cmt_motor_on は A = $02 を残す。
;   cmt_motor_off はレジスタを壊さず、CC も CLR と同じ (Z=1) にする。
;==============================================================================
cmt_motor_on:
                LDA     #$02            ; $FD00 bit1 = モータ ON
                STA     >cmt_motor_st
                STA     >PORT_CMT_CTRL
                RTS
cmt_motor_off:
                CLR     >cmt_motor_st
                CLR     >PORT_CMT_CTRL
                RTS

;==============================================================================
; impl_motor — MOTOR [ON | OFF] (参考書籍 MOTOR。省略形 M.)
;   スイッチを ON にすればモータはオン、OFF にすればオフ。スイッチの指定が
;   ない場合は、オンならオフに、オフならオンにする。
;==============================================================================
impl_motor:
                JSR     skip_sp_x       ; A = スイッチの先頭
                CMPA    #TOK_ON
                BEQ     imo_on
                CMPA    #TOK_OFF
                BEQ     imo_off
                TSTA
                BEQ     imo_tgl         ; 行末 → 反転
                CMPA    #':'
                BNE     iok_syn0        ; 他の綴りは Syntax Error
imo_tgl:
                LDA     >cmt_motor_st   ; 回っていれば止め、止まっていれば回す
                BNE     imo_off0
                BRA     imo_on0
imo_on:
                LEAX    1,X             ; スイッチを消費
imo_on0:
                BSR     cmt_motor_on
                BRA     imo_eos
imo_off:
                LEAX    1,X             ; スイッチを消費
imo_off0:
                BSR     cmt_motor_off
imo_eos:
                ; 引数はここまで。続きがあれば Syntax Error (検査は INTERVAL と共通)
                JMP     iintv_eos

;==============================================================================
; impl_skipf — SKIPF ["[CAS0:]ファイル名"] (参考書籍 SKIPF。省略形 SK.)
; impl_loadq — LOAD? ["[CAS0:]ファイル名"] (参考書籍 LOAD?)
;
;   どちらもテープ上のファイルを終端ブロックまで読み進めながら、各ブロックの
;   チェックサムを SAVE 時に書かれた値と照合する。一致しなければ
;   Device I/O Error (参考書籍の 53)。ファイル名を省略したときは
;   テープ上の先頭のファイルが対象になる。読み終えたところでテープは
;   そのファイルの次に進んでいるので、SKIPF もこの処理で済む。
;   ブロックの形式は参考書籍 (ヘッダ $00 / データ $01 / 終端 $FF)。
;==============================================================================
impl_loadq:
                LEAX    1,X             ; '?' を消費 (impl_load から)
impl_skipf:
                JSR     play_stop       ; テープ系は演奏を止める (参考書籍)
                CLR     cmt_strict_hdr
                JSR     skip_spc
                JSR     impl_parse_fname        ; ファイル名 → cmt_filename
                PSHS    X               ; 文末位置を退避 (ブロック読取が X を壊す)
                BSR     cmt_motor_on
isk_hdr:
                LDU     #cmt_hdr_buf
                JSR     cmt_read_hdr
                BCS     isk_ioerr       ; 検査和エラー / テープ終端
                LDA     cmt_blk_type
                BNE     isk_hdr         ; ヘッダブロックでない → 次を読む
                JSR     cmt_name_chk
                BCS     isk_hdr         ; 名前不一致 → 次のファイルを探す
isk_body:
                LDU     #DA_SECBUF      ; 内容は捨てる (照合だけが目的)
                JSR     cmt_read_block
                BCS     isk_ioerr
                LDA     cmt_blk_type
                CMPA    #$FF            ; 終端ブロック (参考書籍)
                BNE     isk_body
                BSR     cmt_motor_off
                PULS    X,PC
isk_ioerr:
                BSR     cmt_motor_off
                LEAS    2,S             ; 退避した文末位置を捨てる (戻らない)
                LDB     #53             ; Device I/O Error (参考書籍)
                JMP     impl_error_handler

;==============================================================================
; files_cas — FILES "CAS0:"
;   テープを送りながらヘッダブロックを拾い、`ファイル名 種類 形式` を 1 行
;   ずつ並べる。終了は BREAK キーか、テープの終わり。
;   種類は 0 = BASIC プログラム / 1 = データ / 2 = 機械語、形式は
;   A = アスキー / B = バイナリ。
;==============================================================================
files_cas:
                JSR     impl_stub       ; 引数を文末まで読み飛ばし (X=':'/$00)
                PSHS    X               ; 文末位置を退避
                JSR     disk_crlf       ; 一覧の前に 1 行あける
                BSR     cmt_motor_on
ifc_lp:
                JSR     firq_read       ; $FD04 を読んで中断キーの押下を控えへ
                TST     >BRK_FLAG       ; BREAK 押下で一覧を終える (参考書籍 FILES)
                BNE     ifc_end
                LDU     #cmt_hdr_buf
                JSR     cmt_read_hdr
                BCC     ifc_have
                TSTB
                BNE     ifc_end         ; テープ終端 → 一覧の終わり
                BRA     ifc_lp          ; 検査和エラー → 読み続ける
ifc_have:
                LDA     cmt_blk_type
                BNE     ifc_lp          ; ヘッダブロックでない
                ; ヘッダの中身を 1 行に出す部分は、テープの読込み (LOAD /
                ;   LOADM) の表示と同じものなので共通ルーチンに委ねる。
                JSR     cmt_hdr_show    ; ファイル名 + ファイル形式 + 属性 + 改行
                BRA     ifc_lp
ifc_end:
                CLR     >BRK_FLAG       ; 一覧を終えるのに使った押下は消費する
                JSR     cmt_motor_off
                PULS    X,PC

;==============================================================================
; fileinfo_set — 解析した cmt_filename / dl_device から、参考書籍の位置の
;   ファイル名長 ($02DD) / ファイル名 ($02DE-$02E5。8 文字に満たない
;   分は空白) / 入出力デバイスの物理機番 ($02E6) を埋める。物理機番は
;   CAS0: = $02、フロッピーディスク 0〜3 = $80〜$83 (参考書籍)。
;   X は保存する。破壊: A, B, U
;==============================================================================
fileinfo_set:
                PSHS    X
                LDX     #cmt_filename
                LDU     #FIL_NAME
                CLR     >FIL_NLEN
fis_lp:
                LDA     ,X+
                BEQ     fis_pad
                STA     ,U+
                INC     >FIL_NLEN
                CMPU    #FIL_NAME+8
                BNE     fis_lp
                BRA     fis_dev
fis_pad:
                LDA     #' '
fis_pad_lp:
                STA     ,U+
                CMPU    #FIL_NAME+8
                BNE     fis_pad_lp
fis_dev:
                LDA     dl_device       ; bit7=1 ならディスク ($80-$83)
                BMI     fis_put
                LDA     #$02            ; CAS0:
fis_put:
                STA     >FIL_PHYS
                PULS    X,PC

dvi_data:
                ; --- 各テーブルのアドレス ($02B2-$02BF) と COMn 登録テーブル
                ;     ($02C0-$02C9。つながっている COMn は無いので 0) ---
                FDB     DEVTBL_PTR
                FCB     24
                FDB     DEVTBL          ; $02B2 デバイス登録テーブル
                FDB     LOGPHY          ; $02B4 論理機番/物理機番対応テーブル
                FDB     EVTBL           ; $02B6 PEN (現在未使用)
                FDB     EVTBL+5         ; $02B8 COM(n)
                FDB     key_line        ; $02BA KEY(n)
                FDB     EVTBL+80        ; $02BC TIMER
                FDB     intv_st         ; $02BE INTERVAL
                FDB     0,0,0,0,0       ; $02C0-$02C9 COMn 登録テーブル
                ; --- LPT0 ルーチンベクトルのアドレス ($05AA) ---
                FDB     LPTVEC_PTR
                FCB     2
                FDB     LPTVEC
                ; --- 物理機番 0〜2 の登録 (3 以降は 0 = 使用しない) ---
                FDB     DEVTBL
                FCB     6
                FDB     devvec_kybd     ; 物理機番 $00 = KYBD:
                FDB     devvec_scrn     ; 物理機番 $01 = SCRN:
                FDB     devvec_cas0     ; 物理機番 $02 = CAS0:
                ; --- LPT0 ルーチンベクトルのデバイス名 ---
                FDB     LPTVEC
                FCB     7
                FCC     "LPT0   "
                ; 終端は 3 バイト。読み手は「宛先 2B」を先に取ってから
                ;   「長さ 1B」を見るので、長さ 0 の手前に宛先の 2 バイトが
                ;   要る。1 バイトだけだと後続のデバイス名を宛先と長さに
                ;   読み違え、低位 RAM を 89 バイト塗り潰す。
                FDB     0               ; 終端: 宛先 (読み捨て)
                FCB     0               ; 終端: 長さ 0

;--- ルーチンベクトル (デバイス名 7B + OPEN / CLOSE / 1 バイト入力 /
;    1 バイト出力 / POS / EOF / LOF の 7 アドレス)。
;    本 ROM が持たない処理は 0 とする。
devvec_kybd:
                FCC     "KYBD   "
                FDB     0,0
                FDB     impl_sub_inkey
                FDB     impl_putchar
                FDB     0,0,0
devvec_scrn:
                FCC     "SCRN   "
                FDB     0,0
                FDB     impl_sub_inkey
                FDB     impl_putchar
                FDB     0,0,0
devvec_cas0:
                FCC     "CAS0   "
                FDB     0,0
                FDB     cmt_read_byte
                FDB     cmt_write_byte
                FDB     0,0,0


;==============================================================================
; prog_cut — プログラム本文の [X, U) を切り取り、後続を詰める。
;
;   入力: X = 切り取りの先頭 (行レコードの境界)
;         U = 切り取りの末尾 (排他。行レコードの境界か、本文の終端 [00 00])
;   VARTAB を切り取った長さだけ縮め、切り取った位置から後ろの次行ポインタを
;   一律に補正する (終端 [00 00] も一緒に移動する)。X == U のときは何もしない。
;   破壊: A,B,X,Y,U
;   (行の置換・削除 impl_insert_line と DELETE impl_delete が共用する)
;==============================================================================
prog_cut:
                PSHS    X               ; 切り取りの先頭
                TFR     U,D
                SUBD    ,S++            ; D = U - X = 切り取る長さ L
                BEQ     pcut_ret        ; 長さ 0 → 切り取るものが無い
                PSHS    D               ; [S+0:1] = L
                TFR     X,Y             ; Y = 詰め先
pcut_mv:
                CMPU    VARTAB          ; コピー元が旧末尾 (排他) に達したら終了
                BEQ     pcut_fix
                LDB     ,U+
                STB     ,Y+
                BRA     pcut_mv
pcut_fix:
                LDD     VARTAB
                SUBD    ,S
                STD     VARTAB          ; VARTAB -= L
pcut_lk:
                ; リンク補正: 切り取った位置から後ろの次行ポインタは移動前の値の
                ; ままなので一律 -L する (終端 [00 00] で停止)
                LDD     ,X
                BEQ     pcut_end
                SUBD    ,S
                STD     ,X
                TFR     D,X             ; 次レコードへ
                BRA     pcut_lk
pcut_end:
                LEAS    2,S             ; L を捨てる
pcut_ret:
                RTS

;==============================================================================
; DELETE [行番号 1][[-][行番号 2]] — プログラムの行を削除する。
;
;   範囲は n / n- / -m / n-m / - (区切りはコンマも可)。実行後はコマンド
;   レベルへ戻る。範囲の書き方は LIST と同じなので、解析は il_parse_range を共用する
;   (引数を省いたときの既定は 開始 0 / 終端 $FFFF = 全ての行)。
;   行の削除なので、行を打ち込んだときと同じく全変数を消去し、CONT の再開位置を
;   破棄する。
;==============================================================================
impl_delete:
                JSR     il_parse_range  ; il_start_ln / il_end_ln を設定
                LDX     TXTTAB
idel_top:
                ; 切り取りの先頭 = 最初に 行番号 >= 開始行 となるレコード
                LDD     ,X
                BEQ     idel_tail       ; 終端 → 該当なし (X = U で空になる)
                LDD     2,X
                CMPD    il_start_ln
                BHS     idel_tail
                LDX     ,X
                BRA     idel_top
idel_tail:
                TFR     X,U
idel_t1:
                ; 切り取りの末尾 = 最初に 行番号 > 終端行 となるレコード
                LDD     ,U
                BEQ     idel_cut        ; 終端まで
                LDD     2,U
                CMPD    il_end_ln
                BHI     idel_cut
                LDU     ,U
                BRA     idel_t1
idel_cut:
                BSR     prog_cut        ; [X, U) を切り取って後続を詰める
                JSR     vars_clear_all  ; 行の削除の後は全変数を消去する
                JSR     cont_clear      ; 本文が動いたので再開位置は無効
                JMP     warm_start      ; 実行後はコマンドレベルへ戻る

;==============================================================================
; UNLIST 行番号 — 非表示行番号を指定する。
;
;   指定した行番号から先を LIST で表示しない。アスキー形式の SAVE でも
;   その範囲の行は書き出さない。保持先は unlist_ln。
;   LIST はこの値以上の行番号を持つ行を表示せず、解除はこの位置へ
;   $FFFF を書き込むことによる (コールドスタートの既定値も $FFFF)。
;   行番号は省略できない。実行後は同じ行の続きへ戻る (コマンドレベルへは戻らない)。
;==============================================================================
impl_unlist:
                JSR     rn_arg          ; C=1 … D = 行番号 (X 前進) / C=0 … 省略
                BCC     iunl_syn
                STD     unlist_ln
                RTS
iunl_syn:
                JMP     errc_2          ; 行番号の指定が無い → Syntax Error

;==============================================================================
; AUTO [行番号][[,]増分値] — 行の先頭に行番号を自動的に発生する。
;
;   既定値は行番号 10、増分値 10。コンマだけで増分値を省くと直前の増分値を
;   使う。CTRL+C・CTRL+X・BREAK か、行番号の直後のリターンで終えて
;   コマンドレベルへ戻る。保持先は auto_flag / auto_next_ln / auto_inc。
;
;   打ち込んだ本文は、行番号を自分で打ったときと同じ形 (行番号の直後に区切りの
;   空白 1 個) で格納する。LIST は本文を原文のまま出すので、ここで形を揃えないと
;   一覧の行番号と本文が地続きになる。
;==============================================================================
impl_auto:
                LDD     #10
                STD     auto_next_ln    ; 行番号の省略値
                JSR     rn_arg          ; C=1 … D = 最初の行番号
                BCC     iaut_a1
                STD     auto_next_ln
iaut_a1:
                CLRB                    ; B = 0: コンマ無し
                JSR     skip_sp_x
                CMPA    #','
                BNE     iaut_a2
                LEAX    1,X
                INCB                    ; B ≠ 0: コンマ有り
iaut_a2:
                PSHS    B               ; コンマの有無を退避 (rn_arg は D を返すので
                JSR     rn_arg          ;   B を使い回せない)
                BCS     iaut_a3         ; C=1 … D = 増分値
                TST     ,S
                BNE     iaut_a4         ; コンマだけ → 直前の増分値を引き継ぐ
                LDD     #10             ; 指定が無い → 増分値の省略値
iaut_a3:
                STD     auto_inc
iaut_a4:
                LEAS    1,S             ; コンマの有無を捨てる
                INC     auto_flag       ; AUTO の機能を選択中
iaut_lp:
                LDD     auto_next_ln
                CMPD    #64000
                BHS     iaut_end        ; 63999 を越える行番号は発生できない
                ; --- 行番号と区切りの空白を出してから 1 行受け付ける ---
                LDU     #impl_putchar
                STU     out_vec
                JSR     print_uint16    ; 行番号を 10 進で出す
                JSR     put_space
                JSR     impl_input_line ; CTRL+C / CTRL+X は auto_flag を 0 に戻す
                TST     auto_flag
                BEQ     iaut_end
                JSR     firq_read       ; BREAK キーの押下を控えへ
                TST     >BRK_FLAG
                BNE     iaut_end
                JSR     expand_abbrev   ; キーワードの省略形を完全形へ (参考書籍)
                LDA     INPBUF
                BEQ     iaut_end        ; 行番号の直後にリターン → AUTO を終える
                ; --- 本文の先頭へ区切りの空白 1 個を入れる ---
                CLR     >INPBUF+254     ; 1 個ずらしても緩衝を越えないよう切り詰める
                LDX     #INPBUF
iaut_e1:
                LDA     ,X+
                BNE     iaut_e1         ; X = 終端 $00 の次
iaut_e2:
                LDA     ,-X
                STA     1,X             ; 1 バイトずつ後ろへずらす
                CMPX    #INPBUF
                BNE     iaut_e2
                LDA     #' '
                STA     ,X
                ; --- 行を格納して次の行番号を作る ---
                JSR     play_stop       ; 行挿入の緩衝は演奏のキューと時間排他
                LDD     auto_next_ln
                LDX     #INPBUF
                JSR     ins_line_dx     ; D = 行番号 / X = 本文
                LDD     auto_next_ln
                ADDD    auto_inc
                BCS     iaut_end        ; 行番号があふれた
                STD     auto_next_ln
                BRA     iaut_lp
iaut_end:
                CLR     auto_flag       ; AUTO の機能は終了
                CLR     >BRK_FLAG       ; 終了に使った押下は消費する
                JMP     warm_start      ; BASIC のコマンドレベルへ戻る

;==============================================================================
; EDIT 行番号 — 指定された行を画面に表示して 1 行の編集を行う。
;
;   画面を消して行を表示し、カーソルを行番号の直後に置く。リターンで
;   編集を終えてコマンドレベルへ戻る。編集そのものはサブシステムの $04 GET (参考書籍) が
;   担う。画面を消した先頭で SF オーダ (参考書籍) を送って 1 つのフィールドを
;   定め、そこへ行を表示してから、カーソルを行番号の直後 (SBA、参考書籍) へ
;   戻して $04 を発行する。オペレータが手を入れた行は変更フィールドとして返るので、
;   打ち込んだ行と同じ経路 (impl_insert_line) で本文へ格納する。手を入れずに
;   リターンを押したときは変更フィールドが無く (N=0)、本文は変わらない。
;   行番号は省略できない。無い行番号は Undefined Line Number となる。
;==============================================================================
impl_edit:
                BSR     chk_prot        ; 保護中は Protected Program (参考書籍)
                JSR     rn_arg          ; C=1 … D = 行番号
                BCC     ied_syn
                STD     il_start_ln     ; 表示する行番号 (LIST と同じ控えを借りる)
                JSR     impl_find_line  ; D = 行番号 → X = 行レコード先頭
                BEQ     ied_undef
                PSHS    X
                JSR     out_vec_screen  ; 出力先を画面へ戻す (共通化)
                LDA     #$0C            ; EA (参考書籍): 画面を消して先頭へ
                JSR     impl_putraw
                CLR     PRINT_COL       ; PRINT ゾーン桁追跡を画面の先頭へ揃える
                JSR     ic12_send       ; SF: 画面の先頭から 1 つのフィールドを定める
                LDD     il_start_ln
                JSR     print_uint16    ; 行番号を 10 進で出す
                LDA     #$0A            ; Get Buffer Address (参考書籍)
                JSR     sub_send_cmd_a  ;   = 行番号の直後の位置
                LDD     >SUBSTG_BASE+3  ; A = 桁 / B = 行
                PSHS    A,B
                LDX     2,S             ; 行レコード先頭
                JSR     list_body_emit  ; 本文 (原文) をそのまま出す
                PULS    A,B             ; A = 桁 / B = 行
                LEAS    2,S             ; 行レコードを捨てる
                PSHS    A,B
                LDA     #$12            ; SBA (参考書籍): 行番号の直後へ戻す
                JSR     impl_putraw
                PULS    A               ; 桁
                STA     PRINT_COL
                JSR     impl_putraw
                PULS    A               ; 行
                JSR     impl_putraw
                JSR     iil_issue       ; $04 GET で 1 行を編集し、返った行を INPBUF へ
                JSR     expand_abbrev   ; キーワードの省略形を完全形へ (参考書籍)
                LDA     INPBUF
                CMPA    #'0'
                BLO     ied_end         ; 行番号で始まらない (空も含む) → 本文は変えない
                CMPA    #'9'
                BHI     ied_end
                JSR     play_stop       ; 行挿入の緩衝は演奏のキューと時間排他
                LDX     #INPBUF
                JSR     impl_insert_line
ied_end:
                JMP     warm_start      ; 編集を終えたらコマンドレベルへ戻る
ied_undef:
                JMP     errc_8          ; Undefined Line Number
ied_syn:
                JMP     errc_2          ; 行番号の指定が無い → Syntax Error

;------------------------------------------------------------------------------
; chk_prot — 保護されたプログラムの本文表示を禁じる。
;   参考書籍 SAVE の P オプション: P を付けて保存したプログラムは、メモリへ
;   ロードして LIST や EDIT で内容を見ると、行の処理を始める前に
;   Protected Program (参考書籍の 62) となる。解除する手段は無い。
;   保護中は呼出元へ戻らず誤りを送出する。破壊: B (保護時は戻らない)
;------------------------------------------------------------------------------
chk_prot:
                TST     prot_flag
                BEQ     cpr_ok
                LDB     #62             ; 62 Protected Program
                JMP     impl_error_handler
cpr_ok:
                RTS


;------------------------------------------------------------------------------
; if_free_out — FILES の末尾に 1 行あけて `nnn Clusters Free` を出す。
;   nnn は割当表で未使用 ($FF) のクラスタの数 (参考書籍)。破壊: A, B, X
;------------------------------------------------------------------------------
if_free_out:
                JSR     disk_crlf       ; 一覧と空き表示の間を 1 行あける
                LDX     #DA_SECBUF+5
                CLRB
ifo_lp:
                LDA     ,X+
                INCA                    ; $FF = 未使用 (参考書籍)
                BNE     ifo_used
                INCB
ifo_used:
                CMPX    #DA_SECBUF+5+152
                BNE     ifo_lp
                CLRA
                JSR     print_uint16
                LDX     #if_msg_free    ; 文言は 7t_diskboot.inc の区画に置く
                JMP     impl_print_str  ; 末尾呼び

; disk_crlf の本体は 7t_float.inc の MOTOR の入口の手前に置く。

;------------------------------------------------------------------------------
; ssc_chk_err — サブシステムの応答の相対 0 (READY REQ(MSB) / ERROR CODE。
;   参考書籍) を見て、エラーコード表に
;   載る $3C〜$46 (INIT のパラメタ・コンソール座標・オーダシーケンス・
;   グラフィック座標・ファンクションコード・座標数・文字数・色数・PF キー番号・
;   コマンドとパラメータ・コマンドコードの誤り) を、参考書籍の
;   5 Illegal Function Call へ写す。
;   表に無い値は写さない。破壊: A (誤りのときは戻らない)
;------------------------------------------------------------------------------
ssc_chk_err:
                LDA     >SUBSTG_BASE
                ANDA    #$7F            ; MSB = READY REQ を落とす
                SUBA    #$3C
                CMPA    #$0A            ; $3C〜$46 の 11 種
                BHI     sce_ok
sce_fc:
                JMP     errc_5          ; 5 Illegal Function Call
sce_ok:
                RTS

;------------------------------------------------------------------------------
; cont_clear — CONT の再開位置を破棄する。
;   NEW / CLEAR / 行の追加・置換・削除の後に呼ぶ。以後の CONT は
;   Can't Continue (17) になる。
;   (本区画に配置。参照はラベル経由のみで位置依存なし)
;------------------------------------------------------------------------------
cont_clear:
                CLR     CONTPTR
                CLR     CONTPTR+1
                ; 本文の編集 (挿入/置換/削除/NEW/LOAD/CLEAR) の後は、変換済み行の
                ; 控えも無効化する (格納が動き、同じアドレスでも中身が別物になり得る)。
                JMP     clr_execrec

; 倍精度加算。X/Y/U を保持する。
fpa_dbl:
                PSHS    X,Y,U
                JSR     d_prep
                JSR     dfadd_core
                JSR     d_pack
                PULS    X,Y,U,PC

; CIRCLE の比率・開始位置・終了位置は 0〜1 (参考書籍)。
; 単精度へ丸める前に比較し、倍精度で 1 をわずかに超える値や負数も拒む。
; FAC2 を整数定数として毎回初期化する (比較は型を昇格する場合がある)。
; X/Y/U は既存の比較・単精度変換入口が保持する。
circle_unit_ensure:
                JSR     chk_num
                LDD     #1
                STD     FAC2+2
                CLR     FAC2TYPE
                JSR     fp_cmp_impl     ; 1 - 引数
                BLT     sce_fc
                CLRA
                CLRB
                STD     FAC2+2
                CLR     FAC2TYPE
                JSR     fp_cmp_impl     ; 0 - 引数
                BGT     sce_fc
                JMP     fx_ensure

;==============================================================================
; impl_fn_csrlin — CSRLIN
;   CSRLIN は画面上のカーソルの垂直位置を
;   行単位で与える。引数を取らない書式なので、関数評価が復帰時に復元する
;   本文ポインタ (fn_xsave) には入口の X をそのまま置く。
;   行は Get Buffer Address ($0A。参考書籍) の応答の
;   $D384 に載る (同じ欄の $D383 は水平位置)。
;   (置き場所は容量の都合。参照はラベル経由のみで位置依存はない)
;==============================================================================
impl_fn_csrlin:
                STX     fn_xsave                ; 引数を取らないので本文位置はここ
                LDA     #$0A                    ; Get Buffer Address
                JSR     sub_send_cmd_a
                LDB     SUBCMD_P2               ; $D384 = カーソルの垂直位置
                JMP     int_to_fac_b    ; A=0 を畳んだ入口

;==============================================================================
; file_close_all — 開いている全ファイル番号 (1〜16) を閉じる。
;   LOGPHY の該当欄を 0 (未使用) に戻すだけである。キーボードと画面は入出力の
;   緩衝を持たないので、閉じるときに吐き出すものは無い。
;   番号を省略した CLOSE と
;   END は全てのファイルを閉じる。STOP は閉じない (CONT で続けられるため)。
;   (置き場所は容量の都合。参照はラベル経由のみで位置依存はない)
;==============================================================================
file_close_all:
                LDU     #LOGPHY+1
                LDB     #16             ; ファイル番号 1〜16
fca_lp:
                CLR     ,U+
                DECB
                BNE     fca_lp
                RTS

; POS(0) — 画面上のカーソルの水平位置 (参考書籍)。PRINT_COL は画面の幅で
;   折り返して 0 に戻す (impl_putchar) ので、その値がそのまま桁である。
;   (置き場所は容量の都合。参照はラベル経由のみで位置依存はない)
impl_fn_pos:
                JSR     eval_p_num
                LDB     PRINT_COL
                JMP     int_to_fac_b    ; A=0 を畳んだ入口


; play_tp_tbl — O4 の PSG 分周値 (f_clock=1.2288MHz, D=round(76800/f), 平均律 A4=440Hz)。
;   PLAY (7t_play.inc) が LDU #play_tp_tbl で読む純データ。
;   (置き場所は容量の都合。参照はラベル経由のみで位置依存はない)
play_tp_tbl:
                FDB     294             ; C4  261.63Hz
                FDB     277             ; C#4 277.18Hz
                FDB     262             ; D4  293.66Hz
                FDB     247             ; D#4 311.13Hz
                FDB     233             ; E4  329.63Hz
                FDB     220             ; F4  349.23Hz
                FDB     208             ; F#4 369.99Hz
                FDB     196             ; G4  392.00Hz
                FDB     185             ; G#4 415.30Hz
                FDB     175             ; A4  440.00Hz
                FDB     165             ; A#4 466.16Hz
                FDB     156             ; B4  493.88Hz

                ; --- 目印 + 起動ベクタ (当方の起動 ROM との取り決め) ---
                ;   当方が独自に選んだアドレスに置く。当方の起動 ROM は目印 "7T" を
                ;   見て、この直後の 2 つのベクタを読む。
                ; --- ROM 内の権利表示とライセンス識別子 (バージョン固有層。目印の手前の空きへ) ---
                include "7t31_sig.inc"
                ZMB     BASVEC_SIG-*
                FCC     "7T"            ; 目印 (当方の BASIC ROM の印)
                FDB     cold_start_main ; BASVEC_COLD   = コールドスタート
                FDB     cold_start_main ; BASVEC_SECOND = 二次入口 (ディスク起動の不成立時。A=0)
                ; --- BIOS の間接ベクタ ($FBFA) ---
                ; BIOS は RCB 先頭を X に置いて JSR [$FBFA] で呼ぶ。
                ; 入口は 1 か所だけ (参考書籍)。
                FDB     ml_io_dispatch  ; $FBFA = BIOS 入口 (RCB を X で渡す)
                ; --- 起動のアドレス ($FBFC / $FBFE) ---
                ; ディスクのローダは A (0 = ROM モード / 0 以外 = DISK モード) と
                ; X (DISK モードの初期化の入口) を置いて JMP [$FBFE] で飛んでくる。
                ; 読み込みに失敗したときも A = 0 で同じアドレスへ飛ぶ。7T-BASIC は
                ; ディスクから読み込んだ DISK コードを実行する作りではないので、
                ; X の入口は呼ばず、A によらずコールドスタート (ROM モード) へ進む。
                ; $FBFC も同じ入口にする。
                IFNDEF  STAGE_ALT_RAM
                FDB     cold_start_main ; $FBFC
                FDB     cold_start_main ; $FBFE
                ENDC

;==============================================================================
; $FC00-$FFFF: ROM 封入範囲外 (配布 ROM は $8000-$FBFF の 31744 byte で打ち切り)
;
; 6809 割り込みベクタ ($FFF0-$FFFF) はこの領域にあり、本 ROM には封入できない。
;   - FM-7 系: ベクタはブート ROM 側が供給する (IRQ=$01DD 等、低位 RAM の
;     割込フックスロット $01D1-$01E2 を指す固定マッピング)。
;   - FM77AV 系: ベクタは RAM にあり、起動直後は不定。BASIC ROM 自身が
;     コールドスタート中に $FFF0-$FFFD へ実行時設置する (7t_time.inc:
;     time_init。フックスロット設置と同じ IRQ マスク区間で行う)。
;     設置を欠くと IRQ 許可 (ANDCC #$EF) 直後にベクタ $0000 へ飛んで暴走する。
;     $FFFE (RESET) はブート側管理のため実行時設置でも触らない。
;     ベクタ表を ROM に置かないのは、この範囲が ROM の封入範囲の外に落ちるため
;     (実行時設置 time_init が正)。
;==============================================================================
                ZMB     $FFF0-*
                ZMB     16              ; 生 ROM (32768B) のサイズ充足用ゼロ詰め

                END
