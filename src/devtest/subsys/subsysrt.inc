; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; subsysrt.inc — サブシステム試験の共通ランタイム (6809)
;
; 各試験 (cases/<ID>.s) はこのファイルを INCLUDE し、末尾に 2 つの表を置きます。
;
;   CMDTAB … 共有 RAM のコマンド域へ書き込むコマンドの並び
;   OBSTAB … 発行し終えた画面と応答をどう観測するかの並び
;
; ランタイムは次の順に働きます。
;
;   1. CMDTAB のコマンドを 1 件ずつ発行し、受理された件数を数える
;   2. 指定があれば応答バイト列を控える
;   3. OBSTAB の観測を順に行い、結果を人の読める文字列に組み立てる
;   4. 組み立てた文字列を報告域 (RPTADR) へ置いて呼出元へ戻る
;
; 呼出元は BASIC のローダ (cases/<ID>.bas) で、報告域の文字列をそのまま
; PRINT します。したがって試験の結果は「画面に出る文字列」であり、
; cases/<ID>.expected と 1 行ずつ突き合わせられます。
;
; 観測はすべて**この試験自身**が行います。外部のモニタやランナーは要りません。
;   - 応答バイト列 : 共有領域から読む
;   - 画面         : サブシステムの矩形読出しコマンドで読み戻して数える
;                    (メイン CPU から画面の記憶域を直接読む手立ては機種によって
;                     有無が分かれるため、矩形読出しに揃える)
;
; Type-B の位置の ROM を試す試験は INCLUDE の前に RT_TYPEB を定義する。Type-B の
; 位置の ROM は矩形読出し 1 ($1B) を受け付けず、矩形読出し 2 ($1D) が 1 画素を
; 2 バイト (0000BBBB / RRRRGGGG) で返すので (動作観察)、画面の観測はすべて $1D で
; 行い、B・R・G それぞれの 4 ビットが 0 でなければその色が点いているとみなして
; 色番号 (bit0 = B / bit1 = R / bit2 = G) を求める。
;
; 根拠は参考書籍 (書誌は ../../../docs/LEGAL.md §6.1) と、実行環境の外から観察できる
; 入出力の範囲です。
;==============================================================================

; --- 共有領域 (メイン CPU から見たアドレス) ---
SH_ERR          EQU     $FC80           ; 状態 / ATN
SH_STAT         EQU     $FC81           ; bit7 = 続きチャンクあり
SH_CMD          EQU     $FC82           ; コマンドコード (サブが 0 へ戻すと完了)
SH_PARM         EQU     $FC83           ; パラメータ / 応答領域の先頭
SH_TOP          EQU     $FD00           ; 共有領域の直後
SUBCTL          EQU     $FD05           ; bit7 = HALT 要求 / BUSY

; --- 配置 ---
LOADADR         EQU     $6000           ; 機械語本体 (BASIC の CLEAR で保護する)
WORKADR         EQU     $6C00           ; 作業域
RPTADR          EQU     $6E00           ; 報告域 (BASIC がここを PRINT する)

; --- 作業域の割付 ---
V_RPTR          EQU     WORKADR+0       ; 2: 報告域の書込み位置
V_NCMD          EQU     WORKADR+2       ; 1: 発行した件数
V_NOK           EQU     WORKADR+3       ; 1: 正常に終わった件数
V_IDX           EQU     WORKADR+4       ; 1: 現在のコマンド番号
V_CNT           EQU     WORKADR+5       ; 3: 画素の計数 (24bit)
V_RMODE         EQU     WORKADR+8       ; 1: 応答の受け取り方
V_PLEN          EQU     WORKADR+9       ; 1: レコードのバイト数
V_CMD           EQU     WORKADR+10      ; 1: 発行するコマンドコード
V_RLEN          EQU     WORKADR+11      ; 2: 控えた応答のバイト数
V_RP            EQU     WORKADR+13      ; 2: 応答控えの書込み位置
V_RIDX          EQU     WORKADR+15      ; 1: 応答を控えたコマンド番号
V_HASR          EQU     WORKADR+16      ; 1: 応答を控えたか
V_ZS            EQU     WORKADR+17      ; 1: 10 進出力の先頭 0 抑止
V_SX            EQU     WORKADR+21      ; 2: 走査の作業座標
V_SY            EQU     WORKADR+23      ; 2
V_EX            EQU     WORKADR+25      ; 2
V_EY            EQU     WORKADR+27      ; 2
V_STG           EQU     WORKADR+32      ; 32: 発行レコードの組立て場所
V_RESP          EQU     WORKADR+64      ; 256: 応答の控え
V_OCOL          EQU     WORKADR+320     ; 1: 観測中の色番号

; --- 観測命令 (OBSTAB) ---
OP_END          EQU     0               ; 終端
OP_LIT          EQU     1               ; FDB x0,y0,x1,y1        矩形内の非黒画素数
OP_CNT          EQU     2               ; FCB c / FDB x0,y0,x1,y1 矩形内の色 c の画素数
OP_PROBE        EQU     3               ; FDB x,y                1 画素の色番号 (0-7)
OP_ANY          EQU     4               ; FDB x0,y0,x1,y1        矩形内に非黒画素があれば 1、無ければ 0
OP_ANYC         EQU     5               ; FCB c / FDB x0,y0,x1,y1 矩形内に色 c の画素があれば 1、無ければ 0

                ORG     LOADADR

;==============================================================================
; rt_main — 入口 (BASIC の EXEC から JSR される)
;==============================================================================
rt_main:
                PSHS    CC
                ORCC    #$50            ; 割込みを止める (コマンド発行の途中に割り込ませない)
                LDX     #RPTADR
                STX     V_RPTR
                LDX     #V_RESP
                STX     V_RP
                CLR     V_NOK
                CLR     V_IDX
                CLR     V_HASR
                CLR     V_RLEN
                CLR     V_RLEN+1
                ; --- コマンドを順に発行 ---
                LDX     #CMDTAB
                LDA     ,X+
                STA     V_NCMD
                BEQ     rt_report
rt_cmdlp:
                JSR     rt_post
                INC     V_IDX
                LDA     V_IDX
                CMPA    V_NCMD
                BLO     rt_cmdlp

;==============================================================================
; 報告の組立て
;==============================================================================
rt_report:
                LDX     #str_cmd
                JSR     rt_estr
                LDA     V_NCMD
                JSR     rt_pbyte
                JSR     rt_ecr
                LDX     #str_ok
                JSR     rt_estr
                LDA     V_NOK
                JSR     rt_pbyte
                JSR     rt_ecr
                TST     V_HASR
                BEQ     rt_obs
                ; R<番号>=<長さ>:<16 進>
                LDA     #$52            ; 'R'
                JSR     rt_emit
                LDA     V_RIDX
                JSR     rt_pbyte
                LDA     #$3D            ; '='
                JSR     rt_emit
                LDD     V_RLEN
                JSR     rt_pword
                LDA     #$3A            ; ':'
                JSR     rt_emit
                LDX     #V_RESP
                LDD     V_RLEN
rt_rhex:
                CMPD    #0
                BEQ     rt_rhdone
                SUBD    #1
                PSHS    B,A
                LDA     ,X+
                JSR     rt_ehex
                PULS    A,B
                BRA     rt_rhex
rt_rhdone:
                JSR     rt_ecr

;--- 観測 ---------------------------------------------------------------------
rt_obs:
                LDX     #OBSTAB
rt_obslp:
                LDA     ,X+
                BEQ     rt_fin
                CMPA    #OP_LIT
                LBEQ    rt_o_lit
                CMPA    #OP_CNT
                LBEQ    rt_o_cnt
                CMPA    #OP_PROBE
                LBEQ    rt_o_probe
                CMPA    #OP_ANY
                LBEQ    rt_o_any
                CMPA    #OP_ANYC
                LBEQ    rt_o_anyc
rt_fin:
                ; 観測を終えたら、コンソールの表示制御を戻してから帰る。
                ; 試験の前処理で表示制御 ($0C) を書き換えたままだと、呼出元の
                ; BASIC が改行を字形として描くようになり、報告が読めなくなる。
                ; 戻す値 $17 = カーソル表示 + 制御コードを動作として実行 +
                ; TAB + 書込み待ち (オート LF は行わない)。
                LDA     #$00            ; 応答は受け取らない
                STA     V_STG
                LDA     #$02            ; コマンド 1 + パラメータ 1
                STA     V_STG+1
                LDA     #$0C
                STA     V_STG+2
                LDA     #$17
                STA     V_STG+3
                LDX     #V_STG
                JSR     rt_post
                CLR     V_HASR
                CLRA
                JSR     rt_emit         ; 報告の終端
                PULS    CC,PC

rt_o_lit:
                JSR     rt_rect         ; 矩形を取り込む (X 前進)
                PSHS    X
                LDA     #7              ; 色 1-7 = 黒以外すべて
                STA     V_STG+11
                LDX     #V_STG+12
                LDB     #1
rt_lit_c:
                STB     ,X+
                INCB
                CMPB    #8
                BLO     rt_lit_c
                JSR     rt_scan1b
                LDU     #str_lit
                JSR     rt_estr2
                JSR     rt_pcnt
                PULS    X
                LBRA    rt_obslp

rt_o_cnt:
                LDA     ,X+             ; 色番号
                STA     V_OCOL
                JSR     rt_rect
                PSHS    X
                LDA     #1
                STA     V_STG+11        ; 色は 1 個
                LDA     V_OCOL
                STA     V_STG+12
                JSR     rt_scan1b
                LDA     #$4E            ; 'N'
                JSR     rt_emit
                LDA     V_OCOL
                ADDA    #$30            ; 色番号は 1-7 の 1 桁
                JSR     rt_emit         ; V_CNT を保ったままラベルを書く
                LDA     #$3D            ; '='
                JSR     rt_emit
                JSR     rt_pcnt
                PULS    X
                LBRA    rt_obslp

; 字形の点の数は ROM ごとに違うので、文字を描く試験は画素数ではなく
; 「矩形に点があるか」(ANY=0/1、ANY<色>=0/1) で観測する。
rt_o_any:
                JSR     rt_rect         ; 矩形を取り込む (X 前進)
                PSHS    X
                LDA     #7              ; 色 1-7 = 黒以外すべて
                STA     V_STG+11
                LDX     #V_STG+12
                LDB     #1
rt_any_c:
                STB     ,X+
                INCB
                CMPB    #8
                BLO     rt_any_c
                JSR     rt_scan1b
                LDU     #str_any
                JSR     rt_estr2
                BRA     rt_any_eq

rt_o_anyc:
                LDA     ,X+             ; 色番号
                STA     V_OCOL
                JSR     rt_rect
                PSHS    X
                LDA     #1
                STA     V_STG+11        ; 色は 1 個
                LDA     V_OCOL
                STA     V_STG+12
                JSR     rt_scan1b
                LDU     #str_any
                JSR     rt_estr2
                LDA     V_OCOL
                ADDA    #$30            ; 色番号は 1-7 の 1 桁
                JSR     rt_emit
rt_any_eq:
                LDA     #$3D            ; '='
                JSR     rt_emit
                LDA     V_CNT           ; 数えた画素が 0 でなければ 1 を出す
                ORA     V_CNT+1
                ORA     V_CNT+2
                BEQ     rt_any_z
                LDA     #1
rt_any_z:
                JSR     rt_pbyte
                JSR     rt_ecr
                PULS    X
                LBRA    rt_obslp

rt_o_probe:
                JSR     rt_point        ; V_SX / V_SY へ座標 (X 前進)
                PSHS    X
                JSR     rt_plabel
                JSR     rt_probe1
                JSR     rt_pbyte
                JSR     rt_ecr
                PULS    X
                LBRA    rt_obslp

;==============================================================================
; rt_post — CMDTAB の 1 レコードを発行する
;   X = レコード先頭 (FCB 応答種別, バイト数, コマンド, パラメータ…)
;   戻り: X = 次のレコード
;
;   共有領域はサブ CPU を HALT ($FD05 bit7) している間だけメイン CPU から
;   安全に読み書きできるので、次の手順を踏む。
;     1. BUSY が落ちるのを待つ
;     2. HALT を要求し、確立を待つ
;     3. パラメータ域を 0 で埋めてから、パラメータ → コマンドコードの順に書く
;     4. RUN させ、$FC82 が 0 に戻る (= 受理して処理し終えた) のを待つ
;     5. 必要なら応答を読み、ATN を立てて RUN し、待機ループ先頭へ戻す
;==============================================================================
rt_post:
                LDA     ,X+
                STA     V_RMODE
                LDA     ,X+
                STA     V_PLEN
rp_bwait:
                LDA     SUBCTL
                BMI     rp_bwait        ; BUSY の間は待つ
                JSR     rt_halt
                LDY     #SH_PARM
                CLRA
rp_zero:
                STA     ,Y+
                CMPY    #SH_TOP
                BLO     rp_zero
                CLR     SH_ERR
                CLR     SH_STAT
                LDA     ,X+             ; コマンドコード
                STA     V_CMD
                LDB     V_PLEN
                DECB
                BEQ     rp_send
                LDY     #SH_PARM
rp_cp:
                LDA     ,X+
                STA     ,Y+
                DECB
                BNE     rp_cp
rp_send:
                LDA     V_CMD
                STA     SH_CMD
                CLR     SUBCTL          ; RUN
                JSR     rt_wait
                TSTA
                LBEQ    rp_fin          ; 上限まで待っても終わらない → 未完了
                INC     V_NOK
                LDA     V_RMODE
                LBEQ    rp_fin
                CMPA    #$FF
                LBEQ    rp_blk
                CMPA    #$FE
                LBEQ    rp_bits
                CMPA    #$FB
                LBEQ    rp_err
                CMPA    #$FC
                LBEQ    rp_win2
        IFDEF RT_TYPEB
                CMPA    #$FD
                LBEQ    rp_pix
        ENDC
                ; 固定長: $FC83 から N バイト
                JSR     rt_rmark
                LDB     V_RMODE
                LDY     #SH_PARM
                BRA     rp_copy

rp_win2:        ; 単発応答: $FC83 = バイト数、本体は $FC84 から
                JSR     rt_rmark
                LDB     SH_PARM
                LDY     #SH_PARM+1
rp_copy:
                TSTB
                LBEQ    rp_fin
                LDU     V_RP
rp_cp2:
                LDA     ,Y+
                STA     ,U+
                INC     V_RLEN+1
                BNE     rp_cp3
                INC     V_RLEN
rp_cp3:
                DECB
                BNE     rp_cp2
                STU     V_RP
                LBRA    rp_fin

;--- 分割応答をすべて控える ---------------------------------------------------
rp_blk:
                JSR     rt_rmark
rp_blk1:
                LDB     SH_PARM
                LDY     #SH_PARM+1
                TSTB
                BEQ     rp_blk2
                LDU     V_RP
rp_blkc:
                LDA     ,Y+
                STA     ,U+
                INC     V_RLEN+1
                BNE     rp_blkd
                INC     V_RLEN
rp_blkd:
                DECB
                BNE     rp_blkc
                STU     V_RP
rp_blk2:
                LDA     SH_STAT
                BPL     rp_fin
                JSR     rt_cont
                TSTA
                BEQ     rp_fin
                BRA     rp_blk1

;--- 分割応答のビットを数える (矩形読出し 1 の走査) ---------------------------
rp_bits:
                LDB     SH_PARM
                BEQ     rp_bits2
                LDY     #SH_PARM+1
rp_bitl:
                LDA     ,Y+
                JSR     rt_popcnt       ; A,B,X,Y を保つ
                DECB
                BNE     rp_bitl
rp_bits2:
                LDA     SH_STAT
                BPL     rp_fin
                JSR     rt_cont
                TSTA
                BEQ     rp_fin
                BRA     rp_bits

;--- 共有領域の相対 0 (エラーコード) を 1 バイトの応答として控える -------------
;   一覧に無いコマンドコードを試すテストが使う。サブシステムは一覧に無いコードへ
;   $46 (コマンドコードの誤り) を置く。
rp_err:
                JSR     rt_rmark
                LDB     #1
                LDY     #SH_ERR
                BRA     rp_copy

;--- 後始末: 受理済みを清算し、サブを待機ループ先頭へ戻す ---------------------
rp_fin:
                CLR     SH_CMD
                LDA     SH_ERR
                ORA     #$80            ; ATN (応答を受け取ったことを通知)
                STA     SH_ERR
                CLR     SUBCTL          ; RUN
                LDY     #$4000
rp_bw2:
                LDA     SUBCTL
                BPL     rp_bw3
                LEAY    -1,Y
                BNE     rp_bw2
rp_bw3:
                RTS

;--- rt_rmark — この応答が何番目のコマンドのものかを控える -------------------
rt_rmark:
                LDA     V_IDX
                STA     V_RIDX
                LDA     #1
                STA     V_HASR
                RTS

;--- rt_cont — 続きチャンクを要求する ($64) ----------------------------------
rt_cont:
                LDA     #$64
                STA     SH_CMD
                CLR     SUBCTL
                JMP     rt_wait

;--- rt_halt — HALT を要求し確立を待つ ---------------------------------------
rt_halt:
                LDA     #$80
                STA     SUBCTL
rh_w:
                LDA     SUBCTL
                BPL     rh_w
                RTS

;--- rt_wait — 発行したコマンドの処理が終わるのを待つ (HALT した状態で戻る) ---
;   参考書籍は「メイン CPU は、BUSY
;   信号をチェックして、サブ CPU が BUSY 状態でないことを確認した後に、HALT 信号を
;   0 にして、サブ CPU を停止 (HALT) させてから、共有 RAM をアクセスします」と
;   定める。よって完了の判定はまず BUSY ($FD05 bit7) で行い、BUSY が降りてから
;   HALT を掛けて共有 RAM の相対 2 (SH_CMD) が 0 であることを確かめる。
;   BUSY を見ずに HALT を掛けて相対 2 だけを見ると、コマンド処理の途中の
;   共有 RAM を読むことになる (相対 2 が何時 0 になるかは参考書籍が定めていない)。
;   $FD05 の bit7 は HALT 確立中も 1 を返すので、BUSY を見る間は HALT を掛けない。
;
;   上限まで BUSY が降りなかったときも、**HALT を確立してから**戻る。呼出元
;   (rp_fin) は共有 RAM の相対 0 へ ATN を立ててサブを待機ループの先頭へ戻すが、
;   共有 RAM は HALT 中しか書けないので、HALT を掛けずに戻ると ATN が届かず、
;   サブが待機ループ先頭 (BUSY 落とし) を通らないまま次の発行へ進んでしまう。
;   戻り: A=1 完了 / A=0 上限まで待っても終わらなかった。
rt_wait:
                PSHS    U
                LDU     #$4000
rw_lp:
                CLR     SUBCTL          ; RUN (BUSY を見る間は HALT を掛けない)
                LDY     #$0040
rw_dly:
                LEAY    -1,Y
                BNE     rw_dly
                LDA     SUBCTL          ; $FD05 bit7 = BUSY
                BPL     rw_chk          ; 降りた → 完了を確かめる
                LEAU    -1,U
                CMPU    #0              ; LEAU は条件コードを更新しない
                BNE     rw_lp
                BRA     rw_giveup
rw_chk:
                JSR     rt_halt
                LDA     SH_CMD
                BEQ     rw_ok
                CLR     SUBCTL          ; まだ → RUN して再試行
                LEAU    -1,U
                CMPU    #0
                BNE     rw_lp
rw_giveup:
                JSR     rt_halt         ; 未完でも HALT を確立してから戻る
                CLRA
                PULS    U,PC
rw_ok:
                LDA     #1
                PULS    U,PC

;==============================================================================
; 画面の観測
;==============================================================================
;--- rt_scan1b — 矩形 (V_SX,V_SY)-(V_EX,V_EY) を矩形読出し 1 で走査して
;                V_STG+11 以降に組んだ色リストに一致する画素を数える。
;                結果は V_CNT (24bit)。
;------------------------------------------------------------------------------
rt_scan1b:
                CLR     V_CNT
                CLR     V_CNT+1
                CLR     V_CNT+2
        IFDEF RT_TYPEB
                LDA     #$FD            ; 応答種別: 分割応答の画素を数える
                STA     V_STG
                LDA     #9              ; コマンド + 矩形 8 (色リストは送らない)
                STA     V_STG+1
                LDA     #$1D
                STA     V_STG+2
        ELSE
                LDA     #$FE            ; 応答種別: 分割応答のビットを数える
                STA     V_STG
                LDA     V_STG+11
                ADDA    #10             ; 種別+長さ+コマンド+矩形 8 + 個数 1 + 色
                STA     V_STG+1
                LDA     #$1B
                STA     V_STG+2
        ENDC
                LDD     V_SX
                STD     V_STG+3
                LDD     V_SY
                STD     V_STG+5
                LDD     V_EX
                STD     V_STG+7
                LDD     V_EY
                STD     V_STG+9
                LDX     #V_STG
                JMP     rt_post

;--- rt_probe1 — 1 画素の色番号 (0-7) を A に返す ----------------------------
;   矩形読出し 2 を 1x1 の矩形へ掛けると、青・赤・緑の順に 1 バイトずつ返り、
;   各バイトの最上位ビットがその画素のビットになる。
;------------------------------------------------------------------------------
rt_probe1:
                LDD     V_SX
                STD     V_EX
                LDD     V_SY
                STD     V_EY
                LDA     #$FC            ; 単発応答 ($FC83=計数、$FC84 からデータ)
                STA     V_STG
                LDA     #9
                STA     V_STG+1
                LDA     #$1D
                STA     V_STG+2
                LDD     V_SX
                STD     V_STG+3
                LDD     V_SY
                STD     V_STG+5
                LDD     V_EX
                STD     V_STG+7
                LDD     V_EY
                STD     V_STG+9
                LDX     #V_RESP
                STX     V_RP
                CLR     V_RLEN
                CLR     V_RLEN+1
                CLR     V_HASR
                LDX     #V_STG
                JSR     rt_post
                CLR     V_HASR          ; このコマンドは報告に出さない
        IFDEF RT_TYPEB
                LDY     #V_RESP
                JMP     rt_col2         ; 1 画素 2 バイトから色番号
        ENDC
                CLRB
                LDA     V_RESP
                ANDA    #$80
                BEQ     rp1_1
                ORB     #1
rp1_1:
                LDA     V_RESP+1
                ANDA    #$80
                BEQ     rp1_2
                ORB     #2
rp1_2:
                LDA     V_RESP+2
                ANDA    #$80
                BEQ     rp1_3
                ORB     #4
rp1_3:
                TFR     B,A
                RTS

;--- rt_popcnt — A の 1 のビット数を V_CNT へ足す ----------------------------
rt_popcnt:
                PSHS    A,B,X
                LDX     #rt_nibtab
                TFR     A,B
                ANDB    #$0F
                LDB     B,X
                PSHS    B
                LSRA
                LSRA
                LSRA
                LSRA
                TFR     A,B
                LDA     B,X
                ADDA    ,S+
                JSR     rt_addcnt
                PULS    A,B,X,PC

;--- rt_addcnt — A (0-8) を V_CNT (24bit) へ足す -----------------------------
rt_addcnt:
                ADDA    V_CNT+2
                STA     V_CNT+2
                BCC     rac_d
                INC     V_CNT+1
                BNE     rac_d
                INC     V_CNT
rac_d:
                RTS

;--- rt_rect — OBSTAB から矩形 4 つの座標を取り込む (X 前進) -----------------
rt_rect:
                LDD     ,X++
                STD     V_SX
                LDD     ,X++
                STD     V_SY
                LDD     ,X++
                STD     V_EX
                LDD     ,X++
                STD     V_EY
                RTS

;--- rt_point — OBSTAB から座標 1 つを取り込む (X 前進) ----------------------
rt_point:
                LDD     ,X++
                STD     V_SX
                LDD     ,X++
                STD     V_SY
                RTS

;--- 分割応答の画素を数える (Type-B の位置の矩形読出し 2 の走査) ---------------
;   1 画素 2 バイト。色番号が V_STG+11 以降の色リストにあれば V_CNT に 1 足す。
        IFDEF RT_TYPEB
rp_pix:
                LDB     SH_PARM
                BEQ     rp_pix2
                LDY     #SH_PARM+1
rp_pixl:
                JSR     rt_col2         ; A = 色番号 (Y は 2 進む)
                JSR     rt_inlist
                DECB
                DECB
                BNE     rp_pixl
rp_pix2:
                LDA     SH_STAT
                LBPL    rp_fin
                JSR     rt_cont
                TSTA
                LBEQ    rp_fin
                BRA     rp_pix

;--- rt_col2 — Y が指す 2 バイトの画素から色番号を A に返す (Y は 2 進む) ------
rt_col2:
                PSHS    B
                CLRB
                LDA     ,Y+             ; 0000BBBB
                BEQ     rc2_r
                ORB     #1
rc2_r:
                LDA     ,Y+             ; RRRRGGGG
                BITA    #$F0
                BEQ     rc2_g
                ORB     #2
rc2_g:
                BITA    #$0F
                BEQ     rc2_d
                ORB     #4
rc2_d:
                TFR     B,A
                PULS    B,PC

;--- rt_inlist — A の色番号が色リストにあれば V_CNT に 1 足す (A,B,X,Y を保つ) --
rt_inlist:
                PSHS    A,B,X
                LDX     #V_STG+12
                LDB     V_STG+11
ril_lp:
                CMPA    ,X+
                BEQ     ril_hit
                DECB
                BNE     ril_lp
                PULS    A,B,X,PC
ril_hit:
                LDA     #1
                JSR     rt_addcnt
                PULS    A,B,X,PC
        ENDC

;==============================================================================
; 文字列の組立て
;==============================================================================
rt_emit:
                PSHS    X
                LDX     V_RPTR
                STA     ,X+
                STX     V_RPTR
                PULS    X,PC

rt_ecr:
                PSHS    A
                LDA     #$0D
                JSR     rt_emit
                PULS    A,PC

rt_estr:                                ; X = NUL 終端文字列 (X は破壊する)
                LDA     ,X+
                BEQ     rte_d
                JSR     rt_emit
                BRA     rt_estr
rte_d:
                RTS

rt_estr2:                               ; U = NUL 終端文字列 (X を保つ)
                LDA     ,U+
                BEQ     rte2_d
                JSR     rt_emit
                BRA     rt_estr2
rte2_d:
                RTS

rt_ehex:                                ; A を 16 進 2 桁で
                PSHS    A
                LSRA
                LSRA
                LSRA
                LSRA
                BSR     rt_nib
                PULS    A
                ANDA    #$0F
rt_nib:
                CMPA    #10
                BLO     rn_d
                ADDA    #7
rn_d:
                ADDA    #$30
                JMP     rt_emit

;--- rt_plabel — "P<x>,<y>=" を出す ------------------------------------------
rt_plabel:
                PSHS    X
                LDA     #$50            ; 'P'
                JSR     rt_emit
                LDD     V_SX
                JSR     rt_pword
                LDA     #$2C            ; ','
                JSR     rt_emit
                LDD     V_SY
                JSR     rt_pword
                LDA     #$3D            ; '='
                JSR     rt_emit
                PULS    X,PC

;--- rt_pbyte / rt_pword / rt_pcnt — 10 進出力 -------------------------------
rt_pbyte:
                PSHS    A
                CLR     V_CNT
                CLR     V_CNT+1
                PULS    A
                STA     V_CNT+2
                JMP     rt_pdec

rt_pword:
                STD     V_CNT+1
                CLR     V_CNT
                JMP     rt_pdec

rt_pcnt:                                ; V_CNT を出して改行する
                JSR     rt_pdec
                JMP     rt_ecr

rt_pdec:
                PSHS    A,B,X
                CLR     V_ZS
                LDX     #rt_pow10
rpd_p:
                CLRB
rpd_sub:
                LDA     V_CNT+2
                SUBA    2,X
                PSHS    A
                LDA     V_CNT+1
                SBCA    1,X
                PSHS    A
                LDA     V_CNT
                SBCA    ,X
                BCS     rpd_neg
                STA     V_CNT
                PULS    A
                STA     V_CNT+1
                PULS    A
                STA     V_CNT+2
                INCB
                BRA     rpd_sub
rpd_neg:
                LEAS    2,S
                TSTB
                BNE     rpd_out
                TST     V_ZS
                BEQ     rpd_next
rpd_out:
                TFR     B,A
                ADDA    #$30
                JSR     rt_emit
                LDA     #1
                STA     V_ZS
rpd_next:
                LEAX    3,X
                CMPX    #rt_pow10e
                BLO     rpd_p
                TST     V_ZS
                BNE     rpd_d
                LDA     #$30
                JSR     rt_emit
rpd_d:
                PULS    A,B,X,PC

;==============================================================================
; 定数
;==============================================================================
rt_pow10:
                FCB     $01,$86,$A0     ; 100000
                FCB     $00,$27,$10     ;  10000
                FCB     $00,$03,$E8     ;   1000
                FCB     $00,$00,$64     ;    100
                FCB     $00,$00,$0A     ;     10
                FCB     $00,$00,$01     ;      1
rt_pow10e:

rt_nibtab:
                FCB     0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4

str_cmd:        FCC     "CMD="
                FCB     0
str_ok:         FCC     "OK="
                FCB     0
str_lit:        FCC     "LIT="
                FCB     0
str_any:        FCC     "ANY"
                FCB     0
