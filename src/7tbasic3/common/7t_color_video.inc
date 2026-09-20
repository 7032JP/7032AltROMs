; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; COLOR 形式振り分け + 形式3 (パレット定義)
;   空きプール (zero-pad) に配置し、impl_color 以降のコードアドレスを
;   一切移動させない (既存の配置を動かさずに追加するため)。
;   ジャンプテーブル $B5 はここ (impl_color_disp) を指す。
;   入口: X = COLOR トークン直後の本文ポインタ
;   先頭が '=' トークン (TOK_EQ) なら形式3、それ以外は形式1/2 (impl_color12)。
;   形式3: COLOR = (パレットコード, カラーコード)
;     パレットコード p(0-7) とカラーコード c(0-15) を読み、TTL パレットレジスタ
;     $FD38+p に c を書込む (低3ビットが RGB)。どちらも丸めた値が範囲外なら
;     Illegal Function Call (動作観察: COLOR=(7.5,3) も COLOR=(3,-.6) も誤り。
;     下位ビットへ黙って切り詰めることはしない)。
;     参考書籍 COLOR の項は、パレットコード 0 に黒以外を割り付け
;     られないと規定する。以下はその規定に従い、動作観察でも同じ挙動を確認した。
;     ただし p=0 だけは書込む値を常に 0 とする。0 番のパレットは画面の地
;     (どの画面ビットも立っていない画素) に効くため、ここを塗り替えると
;     画面全体の地色が変わって文字が読めなくなる。標準の処理系を走らせて
;     動作観察では、カラーコードに何を与えても 0 番のパレットは 0 のまま
;     据え置かれ、画面の見た目は変わらない。値そのものは正当な値域として
;     受理し (誤りにはしない)、書込む値だけを 0 に固定して同じ挙動に揃える。
;==============================================================================
impl_color_disp:
                LDA     ,X              ; 本文先頭バイトを覗く
                CMPA    #TOK_EQ         ; '=' トークン?
                BNE     impl_color12    ; '=' でなければ形式1/2 へ
impl_color_pal:
                JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
                CMPA    #'('            ; '(' 必須
                BNE     icp_err
                LEAX    1,X
                ; パレットコードもカラーコードも最近接丸め (動作観察:
                ;   COLOR=(3.7,5.7) は $FD3C へ 6 を書く = 4 番へ 6)。
                ; 丸め + 値域検査 (0..7) (ここへ展開)
eval_to_int_7:
                JSR     eval_to_int_round
                CMPD    #7
                BHI     ic12_err
                PSHS    B               ; パレットコード退避
                JSR     skip_comma
                BCC     icp_err1
                JSR     eval_to_int_round   ; 式評価→整数化 (最近接丸め)
                CMPD    #15             ; カラーコードは 0-15 (符号なし比較で負も捕捉)
                BHI     ic12_err
                JSR     skip_sp_x
                CMPA    #')'            ; ')' 必須
                BNE     icp_err1
                LEAX    1,X
                PULS    A               ; A = パレットコード (0-7)
                LDU     #$FD38
                TSTA
                BNE     icp_wr          ; 1-7 番はカラーコードをそのまま書く
                CLRB                    ; 0 番 (画面の地) は常に色 0 で据え置く
icp_wr:
                STB     A,U             ; $FD38+p へ書込む (低3ビット有効)
                RTS
icp_err1:
icp_err:
                JMP     errc_2         ; Syntax Error

;------------------------------------------------------------------------------
; impl_color12 — COLOR [前景][,背景] (形式1/2)
;
;   参考書籍の色指定に従う。次の 3 点は標準の処理系を走らせて
;   動作観察で確かめたうえで揃えてある。
;     ・前景は 0-15、背景は 0-7。範囲外 (負・上限超) はいずれも
;       Illegal function call とする。
;     ・省略した引数は現在値を保つ (0 に戻さない)。引数を 1 つも書かない
;       COLOR は前景・背景とも変えない。
;     ・範囲外を与えたときは前景・背景のどちらも書き換えない
;       (COLOR 5,8 で前景だけ 5 になる、といった中途の反映はしない)。
;   入口: X = COLOR トークン直後の本文ポインタ。
;------------------------------------------------------------------------------
impl_color12:
                LDD     cur_fg_color    ; A = 前景, B = 背景 (隣り合う 1 組を 16 ビットで読む)
                PSHS    D               ; ,S = 新前景 / 1,S = 新背景 (既定=現在値)
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     ic12_set        ; 行末 → 両方省略
                CMPA    #':'
                BEQ     ic12_set        ; 文の区切り → 両方省略
                CMPA    #','
                BEQ     ic12_bg         ; 前景省略 (",背景" 形)
                JSR     eval_to_int_round ; D = 前景 (最近接丸め。動作観察:
                                        ;   COLOR 3.7 は前景 4、15.6 は範囲外誤り)
                CMPD    #15
                BHI     ic12_err        ; 負は符号なし比較で 15 超になる
                STB     ,S
                JSR     skip_sp_comma
                BNE     ic12_set        ; 背景の指定なし
ic12_bg:
                JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
                TSTA
                BEQ     ic12_set        ; 行末 → 背景省略
                CMPA    #':'
                BEQ     ic12_set
                JSR     eval_to_int_round ; D = 背景 (最近接丸め。動作観察:
                                        ;   COLOR 1,7.6 は範囲外誤り)
                CMPD    #7
                BHI     ic12_err
                STB     1,S
ic12_set:
                PULS    D               ; A = 前景, B = 背景
                STD     cur_fg_color    ; 前景と背景は隣り合うので 16 ビットで書く
                ; コンソール文字色もサブシステムへ反映する: 制御コード
                ; $11 <色> を $03 出力経路へ送り、コンソール文字色を更新する
                ; (図形描画色は cur_fg_color を各図形文が使用する)。
                ; ic12_send は CONSOLE (icon_apply) からも末尾呼出しされる。
ic12_send:
                LDA     #$11
                JSR     impl_putraw
                BSR     eff_txt_col
                JMP     impl_putraw
;------------------------------------------------------------------------------
; eff_txt_col — A = コンソールへ送る文字色 (COLOR で指定した色)。
;   単色表示 (参考書籍「CONSOLE 文」のコンソールカラースイッチ)
;   はコンソールイニシャライズ ($01) の相対 10 = GR で伝えてあり、緑だけで描くのは
;   サブシステム側が行う。CLS の消去後文字色と $11 の文字色の両方がここを通る。
;------------------------------------------------------------------------------
eff_txt_col:
                LDA     cur_fg_color
                RTS
ic12_err:
                JMP     errc_5          ; Illegal Function Call

;------------------------------------------------------------------------------
; tok_numlit — 入力行中の数値定数を、写す前に表現範囲へ収まるか確かめる。
;
;   参考書籍の数値定数は単精度・倍精度の表現できる範囲に収まって
;   いなければならない。標準の処理系は範囲外の定数を含む行を **入力した時点で**
;   Overflow として弾き、その行を記憶しない (動作観察: 直前までの文の出力も出ない)。
;   本ルーチンは定数を実際に変換して同じ検査を行う。範囲外なら変換側が
;   Overflow を送出し、行の受け付けがそこで中止される。
;
;   写す範囲は変換が読み進めた範囲とする。ただし指数部の数字が 1 桁も無い
;   ("1E" や "1E+" のような綴り) と変換側が 'E' や符号まで読み進めてしまい、
;   その符号は本来は演算子である ("1E+B" = 1E と B の加算)。よって数字で
;   終わっていないときは 'E'/'D' と符号を写す範囲から外し、続きは呼出元の
;   通常の予約語照合に委ねる。
;
;   入力: X = 定数の先頭 (数字または '.'), U = 出力位置。
;   出力: X = 定数の直後, U = 写した分だけ前進。破壊: A,B,Y,CC。
;------------------------------------------------------------------------------
tok_numlit:
                PSHS    U,X             ; ,S = 先頭 / 2,S = 出力位置
                JSR     pn_decimal      ; 変換して範囲を確かめる (X = 読了位置)
                PULS    Y,U             ; Y = 定数の先頭
                LDA     -1,X            ; 読了位置の直前
                CMPA    #'+'
                BEQ     tnl_bs
                CMPA    #'-'
                BNE     tnl_be
tnl_bs:
                LEAX    -1,X            ; 空の指数部の符号を戻す
tnl_be:
                LDA     -1,X
                CMPA    #'D'
                BEQ     tnl_bd
                CMPA    #'E'
                BNE     tnl_cp0
tnl_bd:
                LEAX    -1,X            ; 数字を伴わない指数記号を戻す
tnl_cp0:
                PSHS    X               ; ,S = 写す終端
tnl_cp:
                CMPY    ,S
                BEQ     tnl_fin
                LDA     ,Y+
                STA     ,U+
                BRA     tnl_cp
tnl_fin:
                LEAS    2,S
                RTS

;------------------------------------------------------------------------------
; ml_io_dispatch: BIOS の唯一の入口 (参考書籍)
;
;   参考書籍は「BIOS の入口は 1 ヵ所だけであり、ディバイスの識別やパラメータの
;   受け渡しは、すべて RCB (Request Control Block) と呼ばれる、8 バイトの
;   インタフェース領域によって行なわれます」と定める (参考書籍)。呼び手は RCB の
;   先頭アドレスを X に置き、間接ベクタ語 $FBFA 経由で `JSR [$FBFA]` と呼ぶ
;   (参考書籍)。アドレスは固定せずベクタ語だけを固定位置に置く。
;   低位 RAM に置く中継 JMP ($00DE) も同じこの入口を指す (入口は 1 ヵ所)。
;
;   RCB (X が指す 8 バイト。相対値は参考書籍の割付):
;     +0 RQNO   = リクエスト番号 (参考書籍のリクエスト番号表)
;                   1 = MOTOR    … オーディオカセットのモータ ON/OFF
;                   2 = CTBWRT   … カセットテープ 1 バイトライト
;                   3 = CTBRED   … カセットテープ 1 バイトリード
;                   8 = RESTOR / 9 = DWRITE / 10 = DREAD … フロッピィ (7t_diskboot.inc)
;                  12 = BEEPON / 13 = BEEPOF … ブザー ($FD03)
;                  16 = SUBOUT   … サブシステムへコマンド/データを転送
;                  17 = SUBIN    … 転送して、処理結果をデータバッファへ持ち帰る
;                  18 = INPUT / 19 = INPUTC … 変更フィールドの 1 行入力とその継続
;                  20 = OUTPUT   … サブシステムへ 1 行を出力
;                  21 = KEYIN    … キーボードから 1 文字入力
;                  22 = KANJIR   … 漢字 ROM の文字パターン 32 バイト
;                  24 = BIINIT   … BIOS の初期化 (することは無い。RCBSTA = 0)
;                  他 (リザーブ、プリンタ関係 5 / 14 / 15 / 23) = Device Unavailable
;     +1 RCBSTA = エラーステータス (BIOS が設定。0 = 正常、参考書籍の番号)
;     +2 以降   = リクエスト番号ごとの引数/結果 (参考書籍)
;   出力: キャリー = エラー有無 (参考書籍。1 = エラー有り、0 = 無し)。
;   呼び出し側は B をカウンタに、X を RCB 参照に使ったまま呼ぶため、
;   全レジスタを保存する (A は復帰時に元値へ戻す)。
;
;   組立て: 入口で全レジスタ (A,B,X,Y,U,DP) を積み、番号の表 (bios_tbl) を引いて
;   番号ごとの処理へ JSR する。処理は X = RCB で入り、RCB+1 (RCBSTA) とキャリーを
;   整えて RTS する (共通の出口は bios_rcb_ret。X を壊したら 5,S から取り直す)。
;   戻ったら枠を降ろし、処理が立てた FIRQ のマスクを解いて呼び手へ返る
;   (キャリーは処理が置いたまま)。表の外の番号は 0 の欄 (Device Unavailable)。
;------------------------------------------------------------------------------
ml_io_dispatch:
                PSHS    A,B,X,Y,U,DP    ; 呼び出し側レジスタを保存 (X は 5,S)
                LDA     ,X              ; A = リクエスト番号 (RQNO)
                CMPA    #24
                BLS     mld_ok
                CLRA                    ; 表の外 → 0 の欄 (Device Unavailable)
mld_ok:
                ASLA
                LDU     #bios_tbl
                JSR     [A,U]           ; 番号ごとの処理 (X = RCB)
                PULS    A,B,X,Y,U,DP
                ANDCC   #$BF            ; FIRQ のマスクを解く (キャリーは処理のまま)
                RTS
bios_tbl:
                FDB     dmd_unavail     ;  0 リザーブ
                FDB     cfd_cmd1        ;  1 MOTOR
                FDB     cfd_cmd2        ;  2 CTBWRT
                FDB     cfd_cmd3        ;  3 CTBRED
                FDB     dmd_unavail     ;  4 リザーブ
                FDB     dmd_unavail     ;  5 SCREEN (プリンタ)
                FDB     dmd_unavail     ;  6 リザーブ
                FDB     dmd_unavail     ;  7 リザーブ
                FDB     cfd_disk        ;  8 RESTOR
                FDB     cfd_disk        ;  9 DWRITE
                FDB     cfd_disk        ; 10 DREAD
                FDB     dmd_unavail     ; 11 リザーブ
                FDB     cfd_beepon      ; 12 BEEPON
                FDB     cfd_beepof      ; 13 BEEPOF
                FDB     dmd_unavail     ; 14 LPOUT (プリンタ)
                FDB     dmd_unavail     ; 15 HDCOPY (プリンタ)
                FDB     ml_io_gfx_send  ; 16 SUBOUT
                FDB     ml_io_gfx_send  ; 17 SUBIN (同じ入口。番号で分ける)
                FDB     cfd_input       ; 18 INPUT
                FDB     cfd_inputc      ; 19 INPUTC
                FDB     ml_io_con_out   ; 20 OUTPUT
                FDB     cfd_keyin       ; 21 KEYIN
                FDB     cfd_kanjir      ; 22 KANJIR
                FDB     dmd_unavail     ; 23 LPCHK (プリンタ)
                FDB     cfd_ok          ; 24 BIINIT (RCBSTA = 0 を返すだけ)
cfd_cmd1:
                ; MOTOR: 相対 2 のモーターフラグ ($FF = モータ ON / それ以外は
                ; モータ OFF。参考書籍 BIOS: MOTOR) に従う。
                ; 参考書籍は「エラーは発生しないので、
                ; RCBSTA は常に正常終了 (値=0)」と定める。
                LDA     2,X             ; モーターフラグ
                INCA                    ; $FF → 0
                BEQ     cfd_m_on
                JSR     cmt_motor_off
                BRA     cfd_ok
cfd_m_on:
                JSR     cmt_motor_on
                BRA     cfd_ok
cfd_cmd2:
                ; CTBWRT: 1 バイトライト。参考書籍の割付どおり
                ; 相対 2 (CRWDAT) のデータを書く。パルス幅の計数に周期タイマの
                ; 割込みが乗らないよう、SAVE のブロック書込みと同じく止めて書く。
                JSR     time_pause
                LDA     2,X             ; CRWDAT
                JSR     cmt_write_byte  ; (壊す A,B,X,Y)
                JSR     time_resume
                BRA     cfd_ok
cfd_cmd3:
                ; CTBRED: 1 バイトリード。参考書籍の割付どおり、読込んだ
                ; データは相対 2 (CRWDAT) へ返し、相対 1 は RCBSTA に用いる。
                ; 読取に失敗したときは 52 = オーディオカセットリードエラー (参考書籍)。
                ; 中断キーはテープの読取と同じく直接モードへ戻る (brk_stop)。
                ; cmt_read_byte は B,X,Y を壊すので、退避済みスタック上の X を
                ; 取り直して格納する。呼び手が読み値を A に保持したまま次を
                ; 呼ぶ使い方に耐えるよう、A は元値へ戻す (枠)。
                JSR     time_pause
                JSR     cmt_read_byte   ; A = 読んだバイト。キャリー = 読取失敗
                LDB     #52             ; オーディオカセットリードエラー
                BCS     cfd_c3s
                CLRB
cfd_c3s:
                LDX     5,S             ; 退避した X (= RCB 先頭)
                STA     2,X             ; CRWDAT ← リードデータ
                JSR     time_resume     ; (B は保存される)
                JMP     bios_rcb_ret
cfd_beepon:
                ; BEEPON / BEEPOF: 参考書籍。ベルは $FD03 の
                ; 書込み (参考書籍の I/O 表: bit7 = 連続ブザ ON / OFF)。
                ; RCBSTA は常に 0。RCB は 2 バイト (相対 2 以降は見ない)。
                LDA     #$80            ; bit7 = 連続ブザ ON
                FCB     $21             ; BRN: 次の CLRA を読み飛ばす
cfd_beepof:
                CLRA                    ; OFF (bit0 = 0 でスピーカは ON のまま)
                STA     PORT_IRQSTAT    ; $FD03 (書込み = ブザー)
                BRA     cfd_ok
cfd_kanjir:
                ; KANJIR: 相対 4,5 の JIS コードの文字パターン 32 バイトを相対 2,3 の
                ; データバッファへ (参考書籍)。JIS コードにない
                ; 値のときは 32 バイトの $00。変換は PRINT@ と同じ (kanji_jis2adr)。
                LDD     4,X             ; RCBJCD
                LDY     2,X             ; RCBDBA
                JSR     kanji_jis2adr   ; D = 漢字 ROM のアドレス。キャリー = 範囲外
                BCS     ckj_blank
                JSR     kanji_read32    ; 32 バイトを Y へ
                BRA     cfd_ok
ckj_blank:
                LDB     #32
ckj_lp:
                CLR     ,Y+
                DECB
                BNE     ckj_lp
cfd_ok:
                ; 正常終了の共通の出口 (BIINIT もここ)。X を取り直して RCBSTA = 0。
                LDX     5,S
                CLRB
                JMP     bios_rcb_ret
cfd_keyin:
                ; KEYIN: サブシステムの INKEY ($29、制御フラグ 0 = 待たない) を発行し、
                ; 応答の相対 3 (キーコード) と 4 (有無) をそのままデータバッファの
                ; +0 / +1 へ写す (参考書籍: +0 = 入力キーコード、
                ; +1 = キー入力の有無 0 / 1)。データバイト数 (RCBLNH) は常に 2。
                LDA     #$29
                BSR     bios_sub_cmd    ; 発行して完了 (BUSY → 0) を待ち、HALT 下で戻る
                LDD     SUBCMD_BASE+3   ; $FC83:$FC84 = キーコード:有無
                LDU     2,X             ; U = RCBDBA
                STD     ,U
                LDD     #2
                STD     4,X             ; RCBLNH = 2
                BRA     bios_sub_done
cfd_input:
                ; INPUT: 相対 2,3 の文字列を相対 4,5 の文字数だけ表示してから (OUTPUT と
                ; 同じ割付なので同じ処理を呼ぶ)、GET ($04、N = 0) でオペレータの入力を
                ; 受け、変更フィールドのうち画面最上部の 1 つを持ち帰る
                ; (参考書籍)。INPUTC は GETC ($05) で残りの
                ; 変更フィールドを持ち帰る (参考書籍)。
                ; 持ち帰る形 (参考書籍): RCBDBA+0 = 終了コード (応答の K)、+1 = $12、
                ; +2 = X、+3 = Y、+4〜 = テキスト (応答の相対 5 から N バイト)。
                ; RCBLNH = 転送した N (0 = 変更フィールドなし)。データバッファ長
                ; (RCBBMH、下位 8 ビット) を越えては転送しない。
                ; 共有 RAM に入り切らない長いフィールドの継続 ($64) は行わない
                ; (テキストは 120 バイトまで)。
                JSR     ml_io_con_out   ; 文字列の表示 (文字数 0 なら何もしない)
                LDA     #$04            ; GET
                FCB     $8C             ; CMPX #: 次の LDA #$05 を即値として読み飛ばす
cfd_inputc:
                LDA     #$05            ; GETC
                BSR     bios_sub_cmd
                LDU     2,X             ; U = RCBDBA
                LDA     SUBCMD_BASE+3   ; K = 終了キー (0 RETURN / 1 CTRL+C, CTRL+X / 2 CLS)
                STA     ,U+
                LDB     SUBCMD_BASE+4   ; N = $12・X・Y・テキストのバイト数
                TST     6,X
                BNE     cin_len         ; RCBBMH ≧ 256 → 制限なし
                CMPB    7,X
                BLS     cin_len
                LDB     7,X             ; データバッファ長まで
cin_len:
                CLRA
                STD     4,X             ; RCBLNH = 転送するバイト数
                TSTB
                BEQ     bios_sub_done
                LDY     #SUBCMD_BASE+5
cin_cp:
                LDA     ,Y+
                STA     ,U+
                DECB
                BNE     cin_cp
bios_sub_done:
                ; 応答を読み終えた後の共通の後始末: ATN を立てて RUN し、サブを待機
                ; ループの先頭へ戻して BUSY を解放させる (ssc_atn_run。X を壊す)。
                PSHS    X
                JSR     ssc_atn_run
                JMP     fco_bwd         ; X を戻し、RCBSTA = 0・キャリー = 0 で復帰
bios_sub_cmd:
                ; A = サブシステムへ発行するコマンド (相対 3 は 0)。直前の完了を待って
                ; HALT し、共有 RAM へ置いて RUN、完了 (BUSY → 0) を待って再び HALT
                ; した状態で戻る (応答は共有 RAM から直接読める)。壊す: A, B。
                BSR     gfx_halt
                STA     SUBCMD_BASE+2   ; $FC82 = コマンド
                CLR     SUBCMD_BASE+3   ; $FC83 = 0 (制御フラグ / N)
                CLR     PORT_SUBCPU     ; RUN
                BRA     gfx_halt        ; 完了同期 + 読み戻し用 HALT

;------------------------------------------------------------------------------
; ml_io_gfx_send: 機械語グラフィクス命令 ($10 SUBOUT / $11 SUBIN) の実体。
;
;   公開参考書籍の機械語グラフィクス I/O は、制御ブロックに
;   「サブシステムへ渡すコマンドイメージ」へのポインタを持たせ、BIOS の間接
;   ベクタ語 [$FBFA] 経由でサブ CPU へ図形描画コマンドを発行する。ここでは制御ブロック
;   の cb[2:3] が指す 16 バイト (共有 RAM $FC80-$FC8F と同一レイアウト:
;   +0=SH_ERR, +2=コマンド, +3.. =パラメータ) を、デュアルポート共有 RAM
;   ($FC80-) へ HALT 下で複写し、サブ CPU に処理させる。手順は
;   impl_sub_send_cmd と同一の BUSY ポーリング プロトコル。ステージング
;   ($0100-) を経由せず、呼出側提供ブロックを直接共有 RAM へ送るため、
;   呼出側のメモリ領域を汚さない。
;   SUBIN (17) は送った後に処理の終了を待ち、共有 RAM の先頭から入力バイト数
;   (RCBBMH、1〜128) だけをデータバッファへ写し、RCBLNH を同じ値にする
;   (参考書籍)。
;
;   入力: X = 制御ブロック先頭 (cb[0]=$10 / $11, cb[2:3]=コマンドイメージポインタ,
;         cb[5]=長さ 1..$80。長さ 0 / 範囲外は RCB エラー (1))
;   出力: キャリー=0 成功。全レジスタ保存 (firmware 互換。枠は入口が積む)。
;------------------------------------------------------------------------------
ml_io_gfx_send:
                LDU     2,X             ; U = cb[2:3] = コマンドイメージポインタ
                LDB     5,X             ; B = cb[5] = イメージの長さ (可変, 1..$80)
                BEQ     fgs_skip        ; 長さ0 は RCB エラー
                CMPB    #$80
                BHI     fgs_skip        ; 範囲外 (>128) も RCB エラー
                ; --- サブ CPU を HALT (完了同期は事実上のハンドシェイク規約どおり
                ;     「次コマンド発行時の HALT 冒頭で直前コマンドの BUSY 解放を
                ;     待つ」方式。gfx_halt が先頭で $FD05 bit7=0 を確認してから
                ;     HALT 要求する。ブロック転送コマンド ($10) は結果を持たない
                ;     ため、送出後は完了を待たずに復帰する fire-and-forget) ---
                PSHS    B               ; 長さ退避 (gfx_halt が B を壊すため)
                BSR     gfx_halt
                ; --- 共有 RAM の現況下位 7bit をイメージ[1] に引き継ぐ (ATN 整合) ---
                LDA     1,U
                ANDA    #$80
                STA     1,U
                LDA     SUBCMD_BASE+1   ; $FC81 現況
                ANDA    #$7F
                ORA     1,U
                STA     1,U
                ; --- 呼出側提供 cb[5] バイト → 共有 RAM ($FC80-) ---
                LDY     #SUBCMD_BASE
                PULS    B               ; 長さ復元
fgs_cp:
                LDA     ,U+
                STA     ,Y+
                DECB
                BNE     fgs_cp
                ; --- RUN させる ---
                CLR     PORT_SUBCPU
                LDA     ,X
                CMPA    #17             ; SUBIN?
                LBNE    fco_bwd_cb      ; SUBOUT: 待たずに復帰 (fire-and-forget)
                ; --- SUBIN: 処理の終了 (BUSY → 0) を待って HALT し、応答を写す ---
                BSR     gfx_halt
                LDB     7,X             ; RCBBMH (入力バイト数) の下位
                BEQ     bios_sub_done   ; 0 は写さない
                CMPB    #$80
                BLS     fsi_len
                LDB     #$80            ; 共有 RAM の大きさまで
fsi_len:
                STB     5,X             ; RCBLNH ← RCBBMH
                LDU     2,X
                LDY     #SUBCMD_BASE
fsi_cp:
                LDA     ,Y+
                STA     ,U+
                DECB
                BNE     fsi_cp
                BRA     bios_sub_done
fgs_skip:
                JMP     fco_skip        ; RCB エラー (1)

; gfx_halt: 事実上のハンドシェイク規約に沿う HALT。まず直前コマンドの完了
;   ($FD05 bit7=0) を待ち、続いて HALT 要求し、確立 (bit7=1) を有界回数だけ
;   待つ (サブ無応答でも進行を止めない)。壊す: A (B は退避)。
gfx_halt:
                ; --- 直前コマンド完了 (bit7→0) を待つ (事実上のハンドシェイク規約に沿う) ---
gfxh_rdy:
                LDA     PORT_SUBCPU     ; $FD05 (bit7 = BUSY)
                BMI     gfxh_rdy        ; bit7=1 の間 (前コマンド処理中) 待つ
                ORCC    #$40            ; FIRQ マスク (臨界区間の ATN 割込を抑止)
                LDA     #$80
                STA     PORT_SUBCPU     ; HALT 要求
                PSHS    B
                LDB     #$08            ; 確立待ちは有界 (相互運用要件値)
gfxh_est:
                DECB
                BEQ     gfxh_ret
                LDA     PORT_SUBCPU
                BPL     gfxh_est        ; bit7=0 の間待つ (確立=bit7→1)
gfxh_ret:
                PULS    B,PC

;------------------------------------------------------------------------------
; ml_io_con_out: 機械語コンソール出力命令 ($14) の実体。
;
;   参考書籍が定める機械語 I/O 入口
;   ([$FBFA]) は、制御ブロック cb[2:3] が指す文字列 (長さ cb[5]) をコンソールへ出力する
;   命令 $14 を持つ。文字列には表示文字のほか、コンソール制御コード
;   ($0C 画面消去 / $11 色指定 / $12 カーソル位置指定 等) を含み得る。
;   ここではサブシステムのコンソール出力 (コマンド $03: +3=文字数,
;   +4..=文字列) へ翻訳し、共有 RAM へ HALT 下で組み立てて発行する。
;
;   同期方針: 発行前に直前コマンドの消化 (SH_CMD=0) を有界に待ち、発行後も
;   SH_CMD クリアを有界に待つ (完了同期)。$03 の処理は画面消去等で長引く
;   一方、後続のブロック転送 ($10) は発行前に SH_CMD を確認しない
;   fire-and-forget のため、ここで完了を待たないと未取得のうちに共有 RAM が
;   上書きされ文字が失われる。待ちはすべて上限付き (サブシステムを自前
;   常駐コードで占有するソフトでもハングしない)。
;
;   入力: X = 制御ブロック先頭 (cb[0]=$14, cb[2:3]=文字列, cb[5]=長さ)
;   出力: キャリー=0 成功, cb[1]=0 (長さ 0/範囲外は cb[1]=1 = RCB エラー、
;         キャリー=1)。X は保つ (INPUT からも呼ぶ)。全レジスタ保存 (枠は入口が積む)。
;------------------------------------------------------------------------------
ml_io_con_out:
                LDB     5,X             ; B = cb[5] = 文字数
                BEQ    fco_skip        ; 長さ 0 は RCB エラー
                CMPB    #$78            ; 共有 RAM パラメータ域 (120B) 上限
                BHI    fco_skip        ; 範囲外も RCB エラー
                LDU     2,X             ; U = cb[2:3] = 文字列ポインタ
                ORCC    #$40            ; FIRQ マスク (臨界区間の ATN 割込を抑止)
                PSHS    B               ; 文字数退避
                ; --- 直前コマンドの消化を有界に待つ ---
                ;   受理条件は「BUSY=0 (走行中に採取) かつ SH_CMD=0 (HALT 下で確認)」。
                ;   SH_CMD=0 だけでは不十分: サブのコマンド応答完了処理は BUSY を
                ;   立てたまま SH_CNT → SH_CMD → SH_ERR の順にクリアするため、
                ;   SH_CMD クリア直後・SH_ERR クリア前の一瞬に HALT が挟まると、
                ;   ここで置いた新コマンドが再開後の後続クリアで消される
                ;   (SCORE ラベル位置ずれ = 先頭カーソル移動列の喪失の真因)。
                ;   BUSY=0 は応答完了処理を抜けて受付ループへ戻った事の証明になる。
                LDB     #$FF            ; poll 上限
fco_ewait:
                LDA     PORT_SUBCPU     ; $FD05 (bit7 = BUSY, 走行中に採取)
                BMI     fco_edly0       ; 処理中 → 走らせたまま待つ
                JSR     ssc_halt        ; HALT 確立 (共有 RAM を読むため)
                LDA     SUBCMD_BASE+2   ; SH_CMD ($FC82)
                BEQ     fco_post        ; 0 → 消化済 (HALT のまま組み立てへ)
                CLR     PORT_SUBCPU     ; 未消化 → RUN で進める
fco_edly0:
                LDY     #$0400          ; サブを走らせる遅延 (画面消去等を覆う長さ)
fco_edly:
                LEAY    -1,Y
                BNE     fco_edly
                DECB
                BNE     fco_ewait
                JSR     ssc_halt        ; 上限枯渇 → HALT だけ確立し強行
fco_post:
                ; --- $03 コンソール出力ブロックを共有 RAM へ組み立て (HALT 下) ---
                CLR     SUBCMD_BASE     ; $FC80 = 0 (SH_ERR/ATN クリア)
                LDA     #$03
                STA     SUBCMD_BASE+2   ; $FC82 = $03 (コンソール出力)
                PULS    B               ; B = 文字数
                STB     SUBCMD_BASE+3   ; $FC83 = 文字数
                LDY     #SUBCMD_BASE+4
fco_cp:
                LDA     ,U+
                STA     ,Y+             ; $FC84.. = 文字列
                DECB
                BNE     fco_cp
                CLR     PORT_SUBCPU     ; RUN ($03 を処理させる)
                ; --- 完了同期 (SH_CMD クリアまで, 有界) ---
                LDB     #$FF            ; poll 上限 (サブ占有ソフトでのハング防止)
fco_poll:
                LDY     #$0400          ; サブを走らせる遅延 (画面消去を覆う長さ)
fco_dly:
                LEAY    -1,Y
                BNE     fco_dly
                JSR     ssc_halt        ; HALT 確立 (共有 RAM を読むため)
                LDA     SUBCMD_BASE+2   ; SH_CMD
                BEQ     fco_done        ; 0 → 処理完了
                CLR     PORT_SUBCPU     ; まだ → RUN 再試行
                DECB
                BNE     fco_poll
                PSHS    X               ; 上限枯渇 → 打ち切り
                JSR     ssc_run_wait    ; 打ち切り時は ATN を立てずに RUN (共通)
                BRA     fco_bwd
fco_done:
                ; --- サブを待機ループ先頭へ戻し BUSY を解放させる ---
                PSHS    X
                JSR     ssc_atn_run     ; ATN を立てて RUN → BUSY 解放待ち (共通。X を壊す)
fco_bwd:
                PULS    X               ; X = 制御ブロック先頭
fco_bwd_cb:
                CLR     1,X             ; cb[1] = 0 (成功結果)
                ANDCC   #$FE            ; キャリー=0
                RTS
fco_skip:
                LDB     #1              ; RCB エラー (参考書籍)。キャリー = 1 で復帰
                JMP     bios_rcb_ret

; LOG(x): 自然対数 (単精度)。
;   x = m * 2^e (m = 仮数, 0.7071 <= m < 1.4142 に正規化) と分解し、
;   ln(x) = ln(m) + e*ln2。ln(m) は t=(m-1)/(m+1) の atanh 級数
;   ln(m) = 2t*(1 + s/3 + s^2/5 + s^3/7 + s^4/9), s=t^2 (|t|<=0.1716)。
;   x<=0 は Illegal function call (ERR 5, 動作観察)。
impl_fn_log:
                JSR     fx_arg1         ; 引数式を評価して単精度へ揃える
flog_core:                              ; べき乗用核入口: FAC1=単精度引数
                LDA     FAC1
                BEQ    flog_err        ; 0 → ERR 5
                LDA     FAC1+5
                BMI    flog_err        ; 負 → ERR 5
                ; 仮数先頭バイトで sqrt(2) 境界を判定し m の指数を 127/128 に固定
                LDB     FAC1+1
                CMPB    #$B5            ; m1 >= $B5 → 仮数 >= 約 sqrt(1/2)
                BHS     flog_hi
                LDB     FAC1
                CLRA
                SUBD    #128            ; e = exp - 128 (16bit 符号付き)
                STD     fsc_t
                LDA     #128            ; m ∈ [1, 1.4142)
                BRA     flog_setm
flog_hi:
                LDB     FAC1
                CLRA
                SUBD    #127            ; e = exp - 127
                STD     fsc_t
                LDA     #127            ; m ∈ [0.7071, 1)
flog_setm:
                STA     FAC1            ; FAC1 = m
                JSR     fx_st1_fsc_x            ; FAC1 → fsc_x
                JSR     fx_ratio        ; FAC1 = t = (m-1)/(m+1), fsc_x = t
                LDX     #fsc_x
                JSR     fx_mul_s       ; FAC1 = t*t = s          ; s 保存 (fx_hstep の乗数)
                ; Horner: p = (((s/9 + 1/7)s + 1/5)s + 1/3)s + 1
                LDX     #ctab_log
                LDB     #4
                JSR     fx_horner       ; FAC1 = p
                ; ln(m) = 2 * t * p
                JSR     fx_ld2mul_x
                LDA     FAC1
                BEQ     flog_adde       ; 0 は指数を触らない
                INC     FAC1            ; *2 (指数 +1)
flog_adde:
                JSR     fx_st1_fsc_r
                LDD     fsc_t           ; e (16bit 符号付き)
                BSR     fx_int_to_fac   ; FAC1 = float(D) (共通化)
                LDX     #c_ln2
                JSR     fx_ld2mul       ; FAC1 = e*ln2
                LDX     #fsc_r
                JSR     fx_ld2          ; FAC2 = ln(m)
                JMP     fadd_core       ; FAC1 = ln(x) (tail)
flog_err:
                JMP     errc_5         ; Illegal function call (動作観察)

; EXP(x): e^x (単精度)。
;   y = x*log2(e), n = round(y), z = (y-n)*ln2 (|z|<=0.347) と分解し、
;   e^x = 2^n * e^z。e^z は 6 次 Horner (誤差 < 2E-7)、2^n は指数バイト加算。
;   結果指数 >255 は Overflow (ERR 6, 動作観察)、<=0 は 0 (動作観察: EXP(-100)=0)。
impl_fn_exp:
                JSR     eval_p_num
fexp_core:                              ; べき乗用核入口: FAC1=単精度引数
                JSR     fx_ensure
                LDA     FAC1
                BEQ     fexp_one        ; x=0 → 1
                LDX     #c_log2e
                JSR     fx_ld2mul       ; FAC1 = y = x*log2(e)
                ; |y| >= 256 は指数加算前に符号で確定 (正→OV, 負→0)
                LDA     FAC1
                CMPA    #136            ; 指数 136 → |y| >= 256
                BLO     fexp_rng
                LDA     FAC1+5
                BMI    fexp_zero
                BRA    fexp_ovf
; >>FOLD fx_int_to_fac
;------------------------------------------------------------------------------
; fx_int_to_fac — D の 16bit 整数を FAC1 の単精度へ載せる定型。
;   EXP / SIN / COS が同じ 3 命令を並べていたので 1 本にまとめた (容量捻出)。
;   末尾は JMP なので呼出側から見た意味は元の並びと同じ。
;------------------------------------------------------------------------------
fx_int_to_fac:
                STD     FAC1+2
                CLR     FACTYPE
                JMP     impl_int_to_float   ; FAC1 = float(n)
; <<FOLD fx_int_to_fac
; >>FOLD fx_round_half
;------------------------------------------------------------------------------
; fx_round_half — FAC1 の符号を付けた 0.5 を足して切り捨て、D = 四捨五入した整数。
;   SIN/COS と EXP が同じ 7 命令を並べていたので 1 本にまとめた (容量捻出)。
;   末尾は JMP なので呼出側から見た意味は元の並びと同じ。破壊: A,B,X,U (FAC1)。
;------------------------------------------------------------------------------
fx_round_half:
                LDX     #c_half
                JSR     fx_ld2
                LDA     FAC1+5
                ANDA    #$80            ; 値の符号
                STA     FAC2+5          ; 0.5 に符号を付与
                JSR     fadd_core       ; FAC1 = v +/- 0.5
                JMP     float_to_int_trunc  ; D = n (FAC1 破壊)
; <<FOLD fx_round_half
fexp_rng:
                LDX     #fsc_x
                BSR     fx_st1          ; y 保存
                ; n = round(y) = trunc(y + sign(y)*0.5)
                BSR     fx_round_half   ; D = round(v) (共通化)
                STD     fsc_t           ; n 保存 (16bit)
                BSR     fx_int_to_fac   ; FAC1 = float(D) (共通化)
                BSR     fac1_flip_nz    ; n=0 は符号反転不要
fexp_nf0:
                LDX     #fsc_x
                BSR     fx_ld2add       ; FAC1 = f = y-n (|f|<=0.5)
                LDX     #c_ln2
                JSR     fx_mul_s       ; FAC1 = z = f*ln2          ; z 保存 (fx_hstep の乗数)
                ; Horner: e^z = ((((((z/720+1/120)z+1/24)z+1/6)z+1/2)z+1)z+1
                LDX     #ctab_exp
                LDB     #6
                JSR     fx_horner       ; FAC1 = e^z (指数 127/128)
                ; 2^n 倍 = 指数バイトへ n を加算
                LDB     FAC1
                CLRA                    ; D = 現指数 (0..255)
                ADDD    fsc_t           ; + n
                ADDD    #0              ; D = 0 か (CMPD #0 と同じフラグで 1 バイト短い)
                BLE     fexp_zero       ; アンダーフロー → 0
                CMPD    #255
                BGT     fexp_ovf        ; オーバーフロー → ERR 6
                STB     FAC1
                RTS
fexp_one:
                LDX     #c_one
                BRA     fx_ld1          ; FAC1 = 1.0 (FACTYPE=1, tail)
fexp_zero:
                JMP     int_to_fac_zero         ; FAC1 = 0
fexp_ovf:
                JMP     errc_6          ; Overflow (動作観察)

;==============================================================================
; 超越関数 単精度浮動小数実装 (SIN/COS/TAN/SQR)
;   参考書籍の実数超越関数仕様に準拠。ラジアン入力。
;   float コア (fmul_core/fadd_core/fdiv_core) を跨いで値を保持するため
;   RAM 一時 fsc_* (6B 展開形 FAC) を用いる。コア群は X/Y/U 温存。
;==============================================================================

; --- 小ヘルパ ---
; fx_st1: FAC1 (6B 展開形) を X 指す先へ格納。X/Y/U 温存。破壊: D。
fx_st1_fsc_u:                           ; FAC1 → fsc_u
                LDX     #fsc_u
                BRA     fx_st1
fx_st1_fsc_r:                           ; FAC1 → fsc_r
                LDX     #fsc_r
                BRA     fx_st1
fx_st1_fsc_x:
                LDX     #fsc_x          ; 格納先 = fsc_x (呼出側の LDX を畳んだ入口)
fx_st1:
                LDD     FAC1
                STD     ,X
                LDD     FAC1+2
                STD     2,X
                LDD     FAC1+4
                STD     4,X
                RTS
; fx_ld1: X 指す 6B → FAC1, ガードクリア, FACTYPE=1。破壊: D。
fac1_flip_nz:                           ; 値ゼロ (指数 0) のときは符号を触らない
                LDA     FAC1
                BEQ     ffs_ret
fac1_flip_sign:                         ; FAC1 の符号を反転
                LDA     FAC1+5
                EORA    #$80
                STA     FAC1+5
ffs_ret:
                RTS
fx_ld1_x:                               ; fsc_x → FAC1
                LDX     #fsc_x
fx_ld1:
                LDD     ,X
                STD     FAC1
                LDD     2,X
                STD     FAC1+2
                LDD     4,X
                STD     FAC1+4
                JMP     fac1_fin_sng    ; ガード 0 + 型=単精度で復帰 (共通化)

;------------------------------------------------------------------------------
; fx_ld2mul / fx_ld2add — 「X の指す 6 バイト定数を FAC2 へ読み、掛ける/足す」
;   超越関数の Horner 展開でこの 2 命令の並びが多数現れるため
;   1 本にまとめた。末尾は JMP なので呼出側から見た意味は元と同じ。
;   入力: X = 6 バイト定数のアドレス。破壊: 元と同じ。
;------------------------------------------------------------------------------
fx_ld2mul_x:                            ; FAC1 *= fsc_x
                LDX     #fsc_x
fx_ld2mul:
                BSR     fx_ld2
                JMP     fmul_core
fx_ld2add_hp:                           ; FAC1 += π/2
                LDX     #c_halfpi
fx_ld2add:
                BSR     fx_ld2
                JMP     fadd_core
; fx_ld2: X 指す 6B → FAC2, ガードクリア。破壊: D。
fx_ld2_u:                               ; fsc_u → FAC2
                LDX     #fsc_u
                BRA     fx_ld2
fx_ld2_x:                               ; fsc_x → FAC2
                LDX     #fsc_x
fx_ld2:
                LDD     ,X
                STD     FAC2
                LDD     2,X
                STD     FAC2+2
                LDD     4,X
                STD     FAC2+4
                CLR     FAC2+6
                CLR     FAC2+7
                RTS
; fx_arg1: 数学関数の共通引数入口。引数式を評価して単精度へ揃える。
;   (同じ 2 段が 6 個の関数で並んでいたため 1 本にまとめた。X = 本文ポインタは
;    impl_eval_expr_p が前進させ、以降は不変)
fx_arg1:
                JSR     eval_p_num
                ; そのまま fx_ensure へ落ちる (末尾呼び)
; fx_ensure: FAC1 が整数型 (FACTYPE=0) なら float 昇格。
fx_ensure:
                JSR     dbl_demote      ; 倍精度引数は単精度へ (動作観察: SQR(2#)=1.41421)
                LDA     FACTYPE
                BNE     fxe_r
                JMP     impl_int_to_float
fxe_r:
                RTS
; fx_hstep: Horner 1段。FAC1 = FAC1 * fsc_s + [X 指す加算定数]。X=定数ポインタ。
fx_hstep:
                PSHS    X               ; 加算定数ポインタ退避
                LDX     #fsc_s
                BSR     fx_ld2mul       ; FAC1 *= s
                PULS    X
                BSR     fx_ld2          ; FAC2 = 定数
                JMP     fadd_core       ; FAC1 += 定数 (tail)
; fx_mul_s: FAC1 に乗じた結果をホーナーの乗数 fsc_s へ保存する定型 (4 箇所共通)。
fx_mul_s:
                BSR     fx_ld2mul
                LDX     #fsc_s
                BRA     fx_st1          ; 末尾呼び
; fx_horner: 係数表を順に食わせて Horner 展開を回す定型。
;   入力 X = 係数表の先頭 (展開形 6B/項を高次から低次へ連続して並べたもの)、
;        B = fx_hstep を回す回数 (= 表の項数 - 1)。
;   FAC1 に先頭の係数を読み、以降 1 項ずつ FAC1 = FAC1*fsc_s + 係数 を行う。
;   SIN / EXP / LOG / ATN が同じ並びを各々書き下していたので 1 本にまとめた。
fx_horner:
                PSHS    B
                BSR     fx_ld1          ; FAC1 = 先頭の係数 (X は不変)
fh_lp:
                LEAX    6,X             ; 次の係数へ
                PSHS    X
                BSR     fx_hstep        ; FAC1 = FAC1*fsc_s + [X]
                PULS    X
                DEC     ,S
                BNE     fh_lp
                PULS    B,PC
; fx_ratio: FAC1 = (v-1)/(v+1)。入力: FAC1 = v かつ fsc_x = v。
;   結果は fsc_x にも保存。fsc_r/fsc_s を破壊。LOG と ATN の共通部。
; kl_body — KEY LIST の第 2 断片。impl_key_list (7t_statements.inc)
;   から JSR で呼ぶ。サブシステムコマンド $2B (Get String of PF) を発行する。
;   2,S = 呼出元が積んだキー番号 (,S / 1,S は戻りアドレス)。
;   続き (読み戻した文字列の出力) は kl_show (別区画)。
;   (容量均衡のため 7t_expr.inc から移設)
kl_body:
                JSR     kl_hide         ; 制御コードを字形で見せる状態へ倒す
                                        ;   (割当てに含まれる復帰などを 1 桁の絵で出す)
                LDA     #$2B            ; $2B = Get String of PF
                LDB     2,S             ; キー番号
                STD     SUBCMD_CMD      ; $0102=cmd, $0103=キー番号
                JSR     impl_sub_send_cmd
                JMP     kl_show
                include "7t_using_exp2.inc"     ; PRINT USING 指数形式 前処理 (プール、7t_expr.inc から移設)
fx_ratio:
                LDX     #c_one
                JSR     fx_ld2add       ; FAC1 = v+1
                JSR     fx_st1_fsc_r
                JSR     fx_ld1_x
                LDX     #c_mone
                JSR     fx_ld2add       ; FAC1 = v-1
                LDX     #fsc_s
                JSR     fx_st1
                LDX     #fsc_r
                JSR     fx_ld1          ; FAC1 = v+1 (除数)
                LDX     #fsc_s
                JSR     fx_ld2          ; FAC2 = v-1 (被除数)
                JSR     fdiv_core       ; FAC1 = (v-1)/(v+1)
                LDX     #fsc_x
                JMP     fx_st1          ; 結果保存 (tail)

; SIN(x): ラジアン。
impl_fn_sin:
                JSR     fx_arg1         ; 引数式を評価して単精度へ揃える
                ; 角度 FAC1 → sin。以降 fx_sin_core を共有 (COS/TAN から流用)。
fx_sin_core:
                LDA     FAC1            ; 引数=0 → sin=0 (FAC1 は既に 0)
                BEQ    fx_sin_ret
                JSR     fx_st1_fsc_x            ; FAC1 → fsc_x
                ; q = x * (1/pi)
                LDX     #c_invpi
                JSR     fx_ld2mul       ; FAC1 = q
                ; n = round(q) = FIX(q + sign(q)*0.5)
                JSR     fx_round_half   ; D = round(v) (共通化)
                STB     fsc_flag        ; n 下位バイト一時
                JSR     fx_int_to_fac   ; FAC1 = float(D) (共通化)
                LDA     fsc_flag
                ANDA    #1
                STA     fsc_flag        ; 1=奇 (符号反転フラグ)
                ; r = x - n*pi
                LDX     #c_pi
                JSR     fx_ld2mul       ; FAC1 = n*pi
                JSR     fac1_flip_sign
                LDX     #fsc_x
                JSR     fx_ld2add       ; FAC1 = x - n*pi = r
                JSR     fx_st1_fsc_r
                ; s = r*r
                LDX     #fsc_r
                JSR     fx_mul_s       ; FAC1 = r*r = s          ; s 保存
                ; Horner: p = (((((c6*s+c5)*s+c4)*s+c3)*s+c2)*s+c1)*s+1
                LDX     #ctab_sin
                LDB     #6
                JSR     fx_horner        ; FAC1 = p
                ; sin = r * p
                LDX     #fsc_r
                JSR     fx_ld2mul       ; FAC1 = r*p = sin(r)
                ; n 奇 → 符号反転
                LDA     fsc_flag
                BEQ     fx_sin_ret
                JSR     fac1_flip_nz    ; 値0 は放置
fx_sin_ret:
                RTS

; COS(x) = SIN(x + pi/2)
impl_fn_cos:
                JSR     fx_arg1         ; 引数式を評価して単精度へ揃える
                JSR     fx_ld2add_hp
                BRA     fx_sin_core

; TAN(x) = SIN(x) / COS(x)
impl_fn_tan:
                JSR     fx_arg1         ; 引数式を評価して単精度へ揃える
                LDX     #fsc_t
                JSR     fx_st1          ; 角度保存
                BSR     fx_sin_core     ; FAC1 = sin(x)
                JSR     fx_st1_fsc_u
                LDX     #fsc_t
                JSR     fx_ld1          ; FAC1 = 角度
                JSR     fx_ld2add_hp
                BSR     fx_sin_core     ; FAC1 = cos(x)
                ; tan = sin / cos : fdiv_core = FAC2 / FAC1
                JSR     fx_ld2_u
                JMP     fdiv_core       ; FAC1 = sin / cos (tail)

; SQR(x): 平方根 (指数半減初期推定 + ニュートン 6反復)。
impl_fn_sqr:
                JSR     fx_arg1         ; 引数式を評価して単精度へ揃える
                LDA     FAC1            ; 指数0 → 0
                BEQ     fx_sqr_zero
                LDA     FAC1+5
                BMI     fx_sqr_err      ; 負 → Illegal function call
                JSR     fx_st1_fsc_x            ; FAC1 → fsc_x
                ; 初期推定: 指数 e' = (e+128)/2 (仮数・符号=+ そのまま)
                CLRA
                LDB     FAC1
                ADDD    #128
                LSRA
                RORB
                STB     FAC1
                JSR     fx_st1_fsc_r
                LDB     #6
fx_sqr_it:
                PSHS    B
                JSR     fx_ld2_x
                LDX     #fsc_r
                JSR     fx_ld1          ; FAC1 = y
                JSR     fdiv_core       ; FAC1 = x / y
                LDX     #fsc_r
                JSR     fx_ld2add       ; FAC1 = y + x/y
                LDA     FAC1
                BEQ     fx_sqr_st       ; 0 は放置
                DEC     FAC1            ; 指数 -1 = * 0.5
fx_sqr_st:
                JSR     fx_st1_fsc_r
                PULS    B
                DECB
                BNE     fx_sqr_it
                LDX     #fsc_r
                JMP     fx_ld1          ; FAC1 = sqrt(x), FACTYPE=1 (tail)
fx_sqr_zero:
                RTS
fx_sqr_err:
                JMP     errc_5         ; Illegal Function Call

; ATN(x): 逆正接 (単精度)。
;   |x|>1 → atn(x) = pi/2 - atn(1/x) で [0,1] へ、さらに x>tan(pi/8) →
;   atn(x) = pi/4 + atn((x-1)/(x+1)) で |z|<=0.4143 へ縮約し、
;   atn(z) = z*(1 - s/3 + s^2/5 - s^3/7 + s^4/9 - s^5/11 + s^6/13), s=z^2。
;   fsc_flag: bit7=符号, bit0=pi/2 変換, bit1=pi/4 加算。
impl_fn_atn:
                JSR     fx_arg1         ; 引数式を評価して単精度へ揃える
                LDA     FAC1
                BNE     fatn_nz
                RTS                     ; ATN(0) = 0
fatn_nz:
                LDA     FAC1+5
                ANDA    #$80
                STA     fsc_flag        ; 符号保存
                CLR     FAC1+5          ; 以降 |x|
                JSR     fx_st1_fsc_x            ; FAC1 → fsc_x
                ; |x| > 1 → x ← 1/x (pi/2 変換フラグ)
                JSR     fx_ld2_x
                LDX     #c_one
                JSR     fx_ld1          ; FAC1 = 1
                JSR     fcmp_core       ; A = sign(x - 1)
                BLE     fatn_le1
                LDA     fsc_flag
                ORA     #1
                STA     fsc_flag
                JSR     fx_ld1_x
                LDX     #c_one
                JSR     fx_ld2          ; FAC2 = 1 (被除数)
                JSR     fdiv_core       ; FAC1 = 1/x
                JSR     fx_st1_fsc_x            ; FAC1 → fsc_x
fatn_le1:
                ; x > tan(pi/8) → z = (x-1)/(x+1) (pi/4 加算フラグ)
                JSR     fx_ld2_x
                LDX     #c_tan8
                JSR     fx_ld1
                JSR     fcmp_core       ; A = sign(x - tan(pi/8))
                BLE     fatn_small
                LDA     fsc_flag
                ORA     #2
                STA     fsc_flag
                JSR     fx_ld1_x
                JSR     fx_ratio        ; FAC1 = z = (x-1)/(x+1), fsc_x = z
fatn_small:
                ; z = fsc_x (|z| <= 0.4143), s = z*z
                JSR     fx_ld1_x
                LDX     #fsc_x
                JSR     fx_mul_s       ; FAC1 = s          ; s 保存 (fx_hstep の乗数)
                ; Horner: p = (((((s/13-1/11)s+1/9)s-1/7)s+1/5)s-1/3)s+1
                LDX     #ctab_atn
                LDB     #6
                JSR     fx_horner        ; FAC1 = p
                JSR     fx_ld2mul_x
                ; pi/4 加算 (縮約 2 段目の復元)
                LDA     fsc_flag
                BITA    #2
                BEQ     fatn_recip
                LDX     #c_qpi
                JSR     fx_ld2add
fatn_recip:
                ; pi/2 - r 変換 (縮約 1 段目の復元)
                LDA     fsc_flag
                BITA    #1
                BEQ     fatn_sign
                JSR     fac1_flip_nz
fatn_r0:
                JSR     fx_ld2add_hp
fatn_sign:
                ; 元引数の符号を適用
                LDA     fsc_flag
                BPL     fatn_ret
                JSR     fac1_flip_nz
fatn_ret:
                RTS

;==============================================================================
; 超越関数 単精度 内部形式 定数 (展開形 6B: 指数(bias128), 仮数32bit(先頭1明示), 符号)
;==============================================================================
c_pi:           FCB     $81,$C9,$0F,$DA,$A2,$00  ; 3.14159265
c_invpi:        FCB     $7E,$A2,$F9,$83,$6E,$00  ; 1/pi = 0.318309886
c_halfpi:       FCB     $80,$C9,$0F,$DA,$A2,$00  ; pi/2 = 1.57079633
; SIN の級数の係数表 (高次から低次へ。末尾の 1.0 が最終段の加算定数)
ctab_sin:
                FCB     $5F,$B0,$92,$30,$9D,$00  ; 1/6227020800
                FCB     $66,$D7,$32,$2B,$40,$80  ; -1/39916800
                FCB     $6D,$B8,$EF,$1D,$2B,$00  ; 1/362880
                FCB     $73,$D0,$0D,$00,$D0,$80  ; -1/5040
                FCB     $79,$88,$88,$88,$89,$00  ; 1/120
                FCB     $7D,$AA,$AA,$AA,$AB,$80  ; -1/6
c_one:          FCB     $80,$80,$00,$00,$00,$00  ; 1.0 (級数の最終段 兼 汎用)
; EXP/LOG/ATN 用定数は 当区間の空き不足のため 7t_float.inc の
; プール (空きプールの手前) へ配置 (参照はラベル経由のみで位置依存なし)。

;==============================================================================
; init_ram_vars — RAM 移設した作業変数の初期値設定 (コールドスタートから JSR)
;   ROM 内 FCB/FDB の初期値に代わり、ここで RAM ($0180-) を初期化する。
;==============================================================================
init_ram_vars:
                ; RND 状態: コールドスタートの初期値 (本実装が定めた 32bit 定数。
                ;   0 以外であればよい)
                LDD     #$3754
                STD     rnd_state
                LDD     #$2AF1
                STD     rnd_state+2
                CLR     trace_flag
                CLR     prot_flag       ; 保護状態 = 非保護
                LDA     #$1F            ; 表示状態の控え: カーソル表示・オーダ動作・
                STA     disp_state      ;   TAB 動作・ページウェイト・プットウェイト
                LDD     #$0700          ; 文字色 7 (白) / 背景色 0 (隣り合う 2 バイト)
                STD     cur_fg_color
                CLRA
                CLRB
                STD     gcur_x
                STD     gcur_y
                STD     tmpX16
                STD     tmpY16
                STD     tmpX16_2
                STD     tmpY16_2
                STD     ONERROR_LIN
                ; MML 既定値 (参考書籍: T=120, O=4, L=4, V=4)
                LDD     #(120*256)+4
                STD     mml_tempo       ; T=120 / O=4 (隣り合うので 16 ビットで置く)
                STB     mml_length
                STB     mml_volume
                ; RUN 行番号ワークと putchar 捕捉フラグの初期化
                LDD     #$FFFF
                STD     RUNLIN          ; RUN 行番号 = 指定なし
                STD     unlist_ln       ; UNLIST 行番号 = 制限なし
                                        ;   (参考書籍: 解除は $FFFF)
                LDD     #10
                STD     auto_inc        ; AUTO の増分値の省略値 (参考書籍 AUTO)
                CLR     pc_capt         ; putchar は通常のコンソール出力
                CLR     mpage_cur       ; SCREEN の面指定 = 全面
                ; 表示書式の控え (con_cols / con_rows / con_wtop / con_wrows) は
                ;   コンソールイニシャライズを送る impl_sub_init が確定する。
                ;   ここで別途初期化すると 2 か所に既定値が散る。
                RTS

;==============================================================================
; ieh_trap_check — ON ERROR GOTO トラップ判定 (impl_error_handler から JSR)
;   有効化済 (ONERROR_EN=1) かつプログラム実行中なら、RESUME 用コンテキストを
;   保存し、システムスタックを実行ループ基準 (EXEC_SP) へ巻き戻して
;   ハンドラ行へ実行を移す (= 戻らない)。それ以外は RTS で通常のエラー表示へ。
;==============================================================================
ieh_trap_check:
                LDA     ONERROR_EN
                CMPA    #1
                BNE     itc_no                  ; 無効/ハンドラ実行中 → 通常表示
                LDD     CURLIN
                CMPD    #$FFFF
                BEQ     itc_no                  ; direct モードはトラップしない
                ; RESUME 用コンテキスト保存
                LDX     CURIP
                STX     ERRIP
                LDX     CURLINE_PTR
                STX     ERRLPTR
                LDA     #2
                STA     ONERROR_EN              ; ハンドラ実行中 (再トラップ禁止)
                ; スタック巻き戻し → ハンドラ行へ
                LDS     EXEC_SP
                LDD     ONERROR_LIN
                JSR     impl_find_line
                BEQ     irsm_undef
                JSR     impl_jump_to_line       ; CURLINE_PTR/CURIP 更新, X=トークン先頭
                LDY     CURLINE_PTR
                LDD     2,Y
                STD     CURLIN                  ; ハンドラ行番号を現在行に
                JMP     iep_stmt                ; 実行ループへ再入
itc_no:
                ; ここへ来た = トラップせずエラーを表示する経路に確定した。
                ; エラー表示は print_uint16 (out_vec 経由) で番号と行番号を出す。
                ; SAVE などが出力先をテープ側へ向けたまま残っていると表示が
                ; 画面に出ないため、表示前にコンソールへ戻す。
                JMP     out_vec_screen  ; 出力先を画面へ戻す (共通化、末尾呼び)

;==============================================================================
; ion_onerr — ON ERROR GOTO 行番号 (impl_on から分岐)
;   行番号 0 は無効化 (ON ERROR GOTO 0)。それ以外は有効化する。
;
;   エラー処理ルーチンの実行中 (ONERROR_EN=2) の ON ERROR は、参考書籍の F-BASIC
;   参考書籍のとおり捕捉を再開しない。処理ルーチンの中で起きたエラーは捕捉せずに
;   表示して止めるのが仕様であり、RESUME を通るまで捕捉は戻らない。
;     ON ERROR GOTO <行> … 捕捉先行番号だけを差し替える (再有効化は RESUME の役目)
;     ON ERROR GOTO 0    … 捕捉を解除し、処理ルーチンを呼んだ元のエラーを表示して
;                          プログラムを止める (本体 ioe_off = 空きプール)
;==============================================================================
ion_onerr:
                LEAX    1,X                     ; ERROR トークンを消費
                JSR     impl_expect_tok
                FCB     TOK_GO                  ; GO
                JSR     impl_expect_tok
                FCB     TOK_TO                  ; TO
                JSR     skip_sp_x
                JSR     impl_eval_linenum       ; D = 行番号
                STD     ONERROR_LIN             ; ST 命令は格納値で Z を立てる (=0 判定)
                LBEQ    ioe_off                 ; ON ERROR GOTO 0 = 無効化 (別プール)
                LDA     ONERROR_EN
                LSRA                            ; 状態は 0/1/2 のみ。1 ビット右送りで
                BNE     ioe_done                ;   2 (処理ルーチン実行中) だけが残る
                LDA     #1
                STA     ONERROR_EN              ; 有効化
ioe_done:
                RTS

;==============================================================================
; irsm_body — RESUME [0 | NEXT | 行番号] 本体 (impl_resume から JMP)
;   エラーコンテキスト (ONERROR_EN=2) が無ければ Resume Without Error。
;   RESUME/RESUME 0   : エラー文自体を再実行
;   RESUME NEXT       : エラー文の次の文から再開
;   RESUME 行番号     : 指定行から再開
;==============================================================================
impl_resume:
irsm_body:
                LDA     ONERROR_EN
                CMPA    #2
                BEQ     irsm_ok
                ; Resume Without Error (ここへ展開)
;==============================================================================
; 二次関数の記述子 (語数 / 予約語表 / 実行ルーチンの 3 欄)
;   二次コマンド記述子と同じく参照を持たない記述用の並びで、アドレスは固定しない。
;==============================================================================
err_c20:
                LDB     #20             ; Resume Without Error
                JMP     impl_error_handler
irsm_ok:
                LDA     #1
                STA     ONERROR_EN              ; 再有効化
                CLR     ERRNUM                  ; ERR は RESUME で 0 に戻る
                                                ;   ERL (ERRLIN) は保持する。
                                                ;   参考書籍どおり ERR は
                                                ;   「直前に処理したエラー」を示し、
                                                ;   処理を終えた RESUME 時点で解除
                                                ;   される。処理ルーチンの自然終了や
                                                ;   捕捉されないエラーでは戻らない。
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     irsm_same               ; RESUME (行末)
                CMPA    #':'
                BEQ     irsm_same               ; RESUME : ...
                CMPA    #TOK_NEXT               ; NEXT トークン
                BEQ     irsm_next
                JSR     impl_eval_linenum       ; D = 行番号
                ADDD    #0              ; D = 0 か (CMPD #0 と同じフラグで 1 バイト短い)
                BEQ     irsm_same               ; RESUME 0
                JSR     impl_find_line
                BEQ     irsm_undef
                JMP     impl_jump_to_line       ; X=行トークン先頭で復帰
irsm_next:
                BSR     irsm_ctx
                ; エラー文を読み飛ばして次の文へ。行末 ($00) または文の区切り
                ; (':') まで進める規則は引数を読み捨てる文と同じなので、その
                ; 実体 (impl_stub) を末尾呼びで共用する (文字列内の ':' は
                ; 区切りとみなさない扱いも共通)。
                JMP     impl_stub
;------------------------------------------------------------------------------
; irsm_ctx (= irsm_same) — エラー文の行文脈を復元し X = エラー文の先頭を返す。
;   CURLINE_PTR だけでなく CURLIN (現在行番号) も戻す。CURLIN を処理ルーチンの
;     行のまま残すと、復帰先で次のエラーが起きたときに impl_error_handler が
;     ERRLIN へ処理ルーチンの行番号を写してしまい、ERL がエラーの起きた行を
;     指さなくなる。CURLIN は行境界 (iep_loop) でしか更新されないので、同じ行の
;     途中へ戻る RESUME / RESUME NEXT ではここで戻さないと次の行境界まで
;     誤った行番号が残る。
;   行レコードの +2 が行番号 (iep_loop と同じ読み方)。
;------------------------------------------------------------------------------
irsm_ctx:
irsm_same:
                LDX     ERRLPTR
                STX     CURLINE_PTR
                LDD     2,X                     ; 行レコード +2 = 行番号
                JSR     exec_ensure_curlin
                LDX     ERRIP                   ; エラー文の先頭から再実行
                RTS

irsm_undef:
                JMP     errc_8        ; Undefined Line Number

;==============================================================================
; impl_fn_point — POINT(x,y): 表示されていれば -1、そうでなければ 0
;   (参考書籍「POINT 関数」)。
;   「表示されている」の判定は **現在の背景色と異なる色か** である。色 0 との
;   比較ではない。引数の解析だけをここで行い、読み取りと判定は
;   pool_fn_point_rd (7t_startup.inc のプール) が担う。当区間の 当区間の空き
;   では判定の是正分が入らないため。
;==============================================================================
impl_fn_point:
                JSR     skip_sp_lparen
                BNE     ifpt_err
                JSR     eval_to_addr_x1
                PSHS    D                       ; X 座標退避
                JSR     skip_comma
                BCC     ifpt_err1
                JSR     eval_to_int    ; 式評価→整数化 (共通化)
                PSHS    D
                JSR     skip_sp_x
                CMPA    #')'
                BNE     ifpt_err2
                LEAX    1,X
                STX     fn_xsave
                JMP     pool_fn_point_rd        ; 読み取りと判定 (プールへ委譲)
ifpt_err2:
ifpt_err1:
ifpt_err:
                JMP     errc_2         ; Syntax Error

;==============================================================================
; impl_width — WIDTH [1行の文字数][,1画面の行数]
;   参考書籍 WIDTH: 1 行の文字数は 80 / 40、1 画面の行数は 25 / 20。
;   実行すると画面が同時にクリアされる。起動時は 40 × 20。
;   第 1 引数を省いた `WIDTH ,20` は、同時に `CONSOLE 0,20,0,0` (参考書籍) を
;   実行する = スクロール範囲を先頭行から 20 行、機能キー行の表示を 0、
;   単色表示を 0 に戻す。
;   桁数モードの切替は subsys $01 コンソールイニシャライズで行う (画面消去あり)。
;   枠 (CPU スタック): 0,S = 桁数 (0 = 第 1 引数を省いた形) / 1,S = 行数
;==============================================================================
impl_width:
                JSR     skip_sp_x               ; A = 最初の非空白
                CLRB                            ; 桁数 0 = 第 1 引数を省いた印
                CMPA    #','
                BEQ     iw_wok                  ; 第 1 引数の省略 → 桁数は現在値のまま
                JSR     eval_to_int    ; 式評価→整数化 (共通化)
                CMPB    #40
                BEQ     iw_wok
                CMPB    #80
                BNE     iw_fc
iw_wok:
                TFR     B,A                     ; A = 桁数
                LDB     con_rows                ; 行数を省いたときは現在の行数を保つ
                PSHS    A,B                     ;   (行数指定のない WIDTH は桁数だけを変える)
                JSR     skip_comma
                BCC     iw_send
                JSR     eval_to_int    ; 式評価→整数化 (共通化)
                CMPB    #25
                BEQ     iw_hok
                CMPB    #20
                BNE     iw_fc2
iw_hok:
                STB     1,S                     ; 行数上書き
iw_send:
                PULS    A,B                     ; A = 桁数 / B = 行数
                TSTA
                BEQ     iw_omit                 ; 第 1 引数を省いた形
                STA     con_cols                ; 桁数の控えを更新 (CONSOLE が添える現在値)
iw_rset:
                STB     con_rows                ; 行数の控え (省略時は元の値のまま)
                CLRA                            ; 範囲上端 0 = 全画面
                JMP     con_init_send           ; 送出は CONSOLE と共通 (プール)
iw_fc2:
iw_fc:
                JMP     errc_5         ; Illegal Function Call
iw_omit:
                CMPB    #20                     ; `WIDTH ,20` は CONSOLE 0,20,0,0 を伴う
                BNE     iw_rset
                CLR     con_pf                  ; 機能キー行の表示 0 (第 3 引数)
                CLR     con_mono                ; 単色表示 0 (第 4 引数)
                BRA     iw_rset

;------------------------------------------------------------------------------
; impl_locate3 — LOCATE [X],[Y][,S]  (参考書籍: 第3引数 S = カーソル
;   スイッチ。0=消す / 非0=表示)。ジャンプ表 $A3 はここを指す。
;   msg_title と同じ空きプールの余白に置く。impl_locate / impl_color を置き換える
;   のではなく別ルーチンとして起こしてあるので、置き換え対象の綴りは動かない。
;   参照はラベル経由のみで位置依存はない。
;
;   X/Y はコンソール制御コード $12<桁><行> で指定する。桁は 0〜桁数-1、
;   行は 0〜行数-1 (参考書籍。WIDTH 40,20 なら 0〜39 / 0〜19、WIDTH 80,25 なら
;   0〜79 / 0〜24) で、外は Illegal Function Call。第3引数 ,S が
;   有るときのみサブシステム関数コード $0C (カーソル ON/OFF) を発行する。
;   $0C は SH_PARAM をコンソール制御の状態語として受け取り bit0 で表示/非表示を
;   切り替えるので、
;   S を bit0 (非0→1 / 0→0) へ正規化して渡す。S 省略時はカーソル状態を変えない。
;
;   引数の省略は認めない (実行環境の動作観察で確定した文法):
;     ・引数の位置に ',' が来る書き方 (LOCATE ,5 / LOCATE 5,,0 / LOCATE ,,1 /
;       LOCATE 5,5,,1 など) は Syntax Error。
;     ・引数の位置で文が尽きる書き方 (LOCATE / LOCATE 5, / LOCATE 5,5,) は
;       Missing Operand (式評価入口が返す番号をそのまま使う)。
;     ・桁と行の区切り ',' そのものが無い書き方 (LOCATE 5) は Syntax Error。
;   共通の式評価入口は「生の区切り文字を値 0・読み位置据置きで返す」既定を
;   持つ (図形文が空引数 ",," を既定値として扱えるようにするための約束)。
;   LOCATE はその既定に乗ってはならないので、各引数の直前で ',' を自分で
;   弾く (iloc3_arg)。
;
;   適用は「全引数を評価・検査し終えてから」行う。誤った書き方では画面へ
;   一切作用しない (カーソルも動かさない) のが標準の挙動である。位置を先に
;   送ってから第 3 引数を評価すると、誤りなのにカーソルだけ動いてしまう。
;------------------------------------------------------------------------------
impl_locate3:
                BSR     iloc3_arg               ; 桁の位置に ',' は許さない
                JSR     eval_to_int_255 ; 桁は整数化 + 値域 0..255 (外は誤り 5)
                CMPB    con_cols                ; 桁は 0〜桁数-1 (参考書籍。桁数は WIDTH の現在値)
                BHS     iw_fc                   ;   外は Illegal Function Call
                PSHS    B                       ; col を保存
                LDA     ,X+
                CMPA    #','
                BNE     iloc3_e2                ; 区切りが無い (LOCATE 5)
                BSR     iloc3_arg               ; 行の位置に ',' は許さない
                JSR     eval_to_int_255 ; 行は整数化 + 値域 0..255 (外は誤り 5)
                CMPB    con_rows                ; 行は 0〜行数-1 (参考書籍。行数は WIDTH の現在値)
                BHS     iw_fc                   ;   外は Illegal Function Call
                PSHS    B                       ; row を保存
                ; --- 任意の第3引数 ,S (カーソル ON/OFF) ---
                LDB     #$FF                    ; $FF = 第3引数なし (状態不変)
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     iloc3_apply             ; ',' 無し → 位置のみ
                LEAX    1,X                     ; ',' を消費
                BSR     iloc3_arg               ; カーソル指定の位置も同じ規則
                JSR     eval_to_int_1  ; カーソル指定は 0..1 (外は誤り 5)。
                                                ;   検査を通れば B は 0 か 1 なので
                                                ;   そのまま bit0 として使える
iloc3_apply:
                PSHS    B                       ; 0,S=S / 1,S=row / 2,S=col
                LDA     #$12                    ; コンソール制御: カーソル位置指定
                JSR     impl_putraw
                LDA     2,S                     ; A = col
                STA     PRINT_COL               ; PRINT ゾーン桁追跡を同期
                JSR     impl_putraw             ; 桁
                LDA     1,S                     ; A = row
                JSR     impl_putraw             ; 行
                JSR     ic12_send               ; SF: 移した位置から現在の文字色のフィールドを定める
                PULS    B                       ; B = S (0/1) または $FF
                LEAS    2,S                     ; row/col を捨てる
                TSTB
                BMI     iloc3_done              ; 第3引数なし → 状態不変
                ; $0C の引数は「カーソルの入切」ではなく表示状態そのものである。
                ; 0/1 を裸で送ると bit0 以外 (命令動作・桁送り・詰まり待ち等) が
                ; 全て落ちてしまうので、控え (disp_state) の bit0 だけを差し替え、
                ; 表示状態を丸ごと送る。控えも更新して次回の派生元にする。
                LDA     disp_state
                ANDA    #$FE                    ; カーソル有効ビットを抜き
                PSHS    B
                ORA     ,S+                     ; 指定した 0/1 を被せる
                STA     disp_state              ; 控えを更新
                TFR     A,B                     ; B = 表示状態
                JMP     scr_func_set            ; 画面機能設定 (末尾呼出)
iloc3_done:
                RTS
; iloc3_arg: 引数の直前で空引数 (',') を弾く。空白は読み飛ばす
;   (LOCATE 5 , 5 は受理、LOCATE  ,5 は誤り)。'\0' や ':' はここでは弾かず
;   式評価入口へ渡し、Missing Operand を出させる。
iloc3_arg:
                JSR     skip_sp_comma
                BNE     iloc3_done              ; RTS を共有
iloc3_e2:
                JMP     errc_2         ; Syntax Error
