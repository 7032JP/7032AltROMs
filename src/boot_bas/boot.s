; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; AltROMs boot_bas.rom — FM-7 BASIC モード起動 ROM ($FE00-$FFFF、512 バイトのイメージ)
;
; 出典: 参考書籍 (docs/LEGAL.md §6.1 の 4 点) と動作観察 (同 §6.2)。
;
; 動作 (参考書籍):
;   $FE00 → 割込マスク、S・DP 設定 → BREAK キー ON ($FD04 bit1 = 0) なら BASIC ホット
;   スタート → ディスクが無い ($FD05 bit0 EXTDET) なら BASIC コールドスタート (A = 0)
;   → ドライブ 0 のモータ ON → READY になるまで RESTOR を出し直す (上限約 2 秒)
;   → RESTOR → トラック 0 セクタ 1 を $0100 へ DREAD → エラーなら RESTOR からやり直し、
;   5 回失敗で BASIC コールドスタート (A = 0) → 成功なら DP = 0 で $0100 へ。
;   どの経路でも、飛ぶ前に RS-232C の制御レジスタ ($FD07 / $FD25 / $FD27 / $FD29 /
;   $FD2B) を初期化する (ctl_init。参考書籍)。
;
; BASIC の飛び先:
;   当方の BASIC ROM (7tbasic3) は目印 "7T" ($FBF4) と起動ベクタ ($FBF6 コールド /
;   $FBF8 二次) を置く。目印があればそのベクタを、無ければ参考書籍に載るホットスタート
;   $8684 / コールドスタート $848B を使う。
;
; ディスク BIOS:
;   $FE02 RESTOR / $FE05 DWRITE / $FE08 DREAD (参考書籍)。X = RCB で
;   呼ぶ。本体 fdc_rw は RCB+0 のリクエスト番号で分岐する。
;
; 寸法: ブート ROM は $FE00-$FFDF の 480 バイト (参考書籍)。$FFE0-$FFEF は
;   BIOS ワーク (RAM) なのでイメージでは 0、$FFF0-$FFFF は 6809 のベクタ。
;==============================================================================

; --- BASIC の起動ベクタ (当方の BASIC ROM が置く) ---
BASVEC_SIG      EQU     $FBF4           ; 目印 ("7T")
BASVEC_COLD     EQU     $FBF6           ; +0 = コールド入口 / +2 = 二次入口 (A = 0)
; --- 目印が無いときの飛び先 (参考書籍) ---
                IFNDEF  STAGE_ALT_RAM
FBASIC_HOT      EQU     $8684           ; ホットスタート (BREAK キー ON)
                ENDC
                IFNDEF  STAGE_ALT_RAM
FBASIC_COLD     EQU     $848B           ; コールドスタート (A = 0 で ROM モード)
                ENDC

; --- FDC レジスタ (参考書籍。DP=$FD で直接ページ参照) ---
FDC_CMD         EQU     $18             ; $FD18 書込=コマンド / 読出=ステータス
FDC_TRK         EQU     $19             ; $FD19 トラックレジスタ
FDC_SEC         EQU     $1A             ; $FD1A セクタレジスタ
FDC_DATA        EQU     $1B             ; $FD1B データレジスタ
FDC_SIDE        EQU     $1C             ; $FD1C ヘッド (サイド) レジスタ
FDC_MOTOR_DRV   EQU     $1D             ; $FD1D ドライブレジスタ (bit7=モータ, bit1:0=ドライブ)
FDC_DRQ         EQU     $1F             ; $FD1F bit7=DRQ, bit6=IRQ (INTRQ)

; --- BIOS ワーク (参考書籍) ---
BIOS_DRIVE      EQU     $FFE0           ; 最後にアクセスしたドライブ番号
BIOS_TRACK      EQU     $FFE1           ; +ドライブ番号: そのドライブのヘッドのあるトラック番号
BIOS_MOTOR      EQU     $FFE5           ; bit7 = モータ回転中
BIOS_SELDRV     EQU     $FFE8           ; ディスク関係のリクエストで選ぶドライブ番号 (0-3)

; --- BIOS のエラー番号 (参考書籍) ---
ERR_NOTREADY    EQU     10              ; ドライブノットレディ
ERR_TIMEOVER    EQU     15              ; タイムオーバーエラー

; --- データ待ちの上限 ---
;   LSI は読み書きの指令を受けるとレコードを探して媒体を 5 回転ぶん見る (毎分 300
;   回転で約 1 秒)。その間は DRQ も INTRQ も来ない。待ちが探索の窓より短いと、
;   媒体に無いセクタを読んだときに必ず 15 (待ち切れ) になり、参考書籍の
;   12 (ハードエラー = レコードノットファウンド) を返せない。そこで内側の繰り返し
;   ($8000 周) を DATA_WAIT_OUT 回まで繰り返し、探索の窓より長く待つ。正常な転送では
;   内側の繰り返しが尽きないので、読み書きの速さは変わらない。
DATA_WAIT_OUT   EQU     4

; --- READY 待ち ---
;   モータ ON の直後の RESTOR はドライブノットレディ (10) で戻る。回転が安定するまでの
;   時間は決め打ちにせず、SPIN_WAIT_N 回の空回し (8 サイクル/回) を挟んで RESTOR を
;   出し直し、正常に戻るまで READY_TRIES 回まで繰り返す (合計で約 2 秒が上限)。
READY_TRIES     EQU     24
SPIN_WAIT_N     EQU     $3000
CTL_RESET       EQU     $40             ; RS-232C 等の制御レジスタへ最後に書く値 (内部リセット)

STACK_TOP       EQU     $FC7F           ; RAM の末尾 (参考書籍)
                IFNDEF  STAGE_ALT_RAM
IPL_ADDR        EQU     $0100           ; ディスクから読む 1 セクタの置き場 = 実行アドレス
                ENDC

                IFDEF   STAGE_ALT_RAM
                INCLUDE "7t_stage_ram.inc"
                ORG     $FE00
boot_entry:     ORCC    #$50
                LDS     #STACK_TOP
                CLRA
                TFR     A,DP
                JMP     [BASVEC_COLD]
                ELSE
                ORG     $FE00

;------------------------------------------------------------------------------
; 先頭の入口
;   $FE00 リセット入口。$FE02 RESTOR / $FE05 DWRITE / $FE08 DREAD はディスク BIOS の
;   処理アドレス (参考書籍)。3 つとも同じ本体 fdc_rw に入り、RCB+0 の
;   リクエスト番号で分岐する。
;------------------------------------------------------------------------------
boot_entry:     BRA     reset_init      ; $FE00
                JMP     fdc_rw          ; $FE02 RESTOR
                JMP     fdc_rw          ; $FE05 DWRITE
                JMP     fdc_rw          ; $FE08 DREAD

;------------------------------------------------------------------------------
; reset_init — リセットルーチン (参考書籍)
;------------------------------------------------------------------------------
reset_init:     ORCC    #$50            ; IRQ/FIRQ マスク
                LDS     #STACK_TOP
                LDB     #$FD
                TFR     B,DP            ; DP=$FD
                LDB     #READY_TRIES
                PSHS    B               ; READY 待ちの残り回数。BIOS は B を壊すので
                                        ; 回数はスタックの 1 バイトで数える (rb_go で捨てる)
                LDB     <$04            ; $FD04 bit1 = BREAK キー (0: ON)
                BITB    #$02
                BEQ     rb_break        ; BREAK キー ON → ホットスタート
                LDB     <$05            ; $FD05 bit0 = EXTDET (0: あり)
                LSRB
                BCS     rb_fail         ; ディスクが無い → コールドスタート
                ; --- ドライブ 0 のモータ ON → READY になるまで RESTOR を出し直す ---
                ;   モータ ON の直後はドライブが READY でなく、RESTOR は 10 (ドライブノット
                ;   レディ) で戻る。少し待って RESTOR を出し直し、正常に戻るまで繰り返す
                ;   (上限 READY_TRIES 回 = 約 2 秒)。上限に達したら (ディスクが無い等)
                ;   そのまま次の段へ進み、そこで読めないことが確定する。
                LDA     #$80
                STA     <FDC_MOTOR_DRV
rb_ready:       JSR     spin_wait       ; 少し待つ
                LDX     #restor_rcb
                JSR     fdc_rw          ; RESTOR (A = エラー番号、Z は A に従う)
                BEQ     rb_load         ; 正常 → READY
                DEC     ,S              ; 残り回数
                BNE     rb_ready
                ; --- RESTOR → トラック 0 セクタ 1 を $0100 へ DREAD。エラーなら 5 回まで ---
rb_load:        LDB     #5
                STB     ,S              ; 同じ枠を読込の残り回数に使う
rb_retry:       LDX     #restor_rcb
                JSR     fdc_rw          ; RESTOR
                LDX     #dread_rcb
                JSR     fdc_rw          ; DREAD (A = エラー番号、Z は A に従う)
                BEQ     rb_ok
                DEC     ,S              ; 残り回数
                BNE     rb_retry
rb_fail:        LDB     #2              ; +2 = コールドスタート
                BRA     rb_vec
rb_break:       CLRB                    ; +0 = ホットスタート
rb_vec:         JSR     basic_vec
                BRA     rb_go
rb_ok:          LDX     #IPL_ADDR
                IFNDEF  STAGE_ALT_RAM
                LDY     #$0500          ; IPL へ渡す Y = $0500 (バイト数)
                ENDC
rb_go:          LEAS    1,S             ; 残り回数の枠を捨てる (S を初期値に戻す)
                JSR     ctl_init        ; RS-232C 等の制御レジスタの初期化 (X・Y は保つ)
                CLRA                    ; A = 0 (ROM モード)
                TFR     A,DP            ; DP = 0
                JMP     ,X

;------------------------------------------------------------------------------
; 起動時に使う RCB (参考書籍)。ROM 内の定数なので RCB+1 には書けない。
;   結果は A とキャリーで見る。
;------------------------------------------------------------------------------
restor_rcb:     FCB     $08,$00,$00,$00,$00,$00,$00,$00 ; RESTOR: ドライブ 0
dread_rcb:      FCB     $0A,$00,$01,$00,$00,$01,$00,$00 ; DREAD: $0100 へ、トラック 0 セクタ 1 サイド 0 ドライブ 0

;------------------------------------------------------------------------------
; fdc_rw — ディスク BIOS の本体 (RESTOR / DWRITE / DREAD)
;   出典: 参考書籍 (呼出規約とキャリー、RCB、エラー番号、BIOS ワーク、
;         RESTOR / DREAD・DWRITE の解説)。
;   入力: X = RCB の先頭。
;         RCB+0 リクエスト番号 ($08 RESTOR / $09 DWRITE / $0A DREAD)
;         RCB+1 エラーステータス (BIOS が書く)
;         RCB+2,+3 入出力データエリアの先頭アドレス (256 バイト)
;         RCB+4 トラック番号 0-39 / RCB+5 セクタ番号 1-16 / RCB+6 サイド 0-1 /
;         RCB+7 ドライブ番号 0-3
;   復帰: RCB+1 = エラー番号 (0 = 正常)。キャリー = エラー有無 (1 = エラー)。
;         A にも RCB+1 と同じ値を入れる (Z は A に従う)。X・DP・割込マスク (CC の I/F)
;         は呼出時の状態に戻す。B・Y・U は破壊する。
;   DP: 参考書籍は直接呼出しに DP=$FD を求めるが、本体は自分で DP を
;       退避して $FD にし、復帰時に戻す。
;   割込: 本体の実行中は IRQ/FIRQ をマスクし (参考書籍 DREAD・DWRITE)、復帰時に
;         呼出時の状態へ戻す。
;   BIOS は再試行しない (参考書籍)。再試行は呼出側の責務。
;   リクエスト番号の検査は BIOS の入口 (参考書籍の唯一の入口) が済ませて
;   いる。処理アドレスは、どの処理かが入口の側で決まった後の分岐先なので、ここで番号を
;   検査し直さない。RCB+0 は $08 = RESTOR / $09 = DWRITE / それ以外 = DREAD と読む。
;   エラー番号 (参考書籍 RESTOR):
;     10 ドライブノットレディ / 11 ライトプロテクト / 12 ハードエラー (レコードノット
;     ファウンド、ロストデータ) / 13 CRC エラー / 14 DD マーク検出 /
;     15 タイムオーバー (待ち切れ)。RESTOR が出すのは 10 と 15 だけ。位置決めの誤りは
;     READ / WRITE SECTOR の ID の照合が 12 として返す。
;   命令の配置: 本体 +$21 に RESTORE ($08) の即値、本体 +$3D に SEEK ($1C) の即値を置く。
;   この位置へ同じ値を書き込まれても動作は変わらない (動作観察)。
;   位置合せは無操作命令や未使用バイトを入れずに命令の並び順で取る。
;   FDC のコマンド値とステータスのビット割付は LSI の仕様による
;   (RESTORE $08 / SEEK $1C / READ SECTOR $80 / WRITE SECTOR $A0)。
;------------------------------------------------------------------------------
fdc_rw:         PSHS    CC,DP,X         ; 呼出時の割込マスク・DP・X を退避
                ORCC    #$50            ; IRQ/FIRQ マスク
                LDB     #$FD
                TFR     B,DP            ; DP=$FD
                ; --- ドライブ選択 + モータ ON。BIOS ワークを更新 ---
                LDB     7,X
                ANDB    #$03
                LDU     #BIOS_TRACK
                STB     BIOS_DRIVE      ; $FFE0 = 最後にアクセスしたドライブ
                STB     BIOS_SELDRV-BIOS_TRACK,U ; $FFE8 = このリクエストで選ぶドライブ
                LDA     #$80
                STA     BIOS_MOTOR-BIOS_TRACK,U ; $FFE5 bit7 = モータ回転中
                LEAU    B,U             ; U → $FFE1+ドライブ (ヘッドのトラック)
                ORB     #$80
                STB     <FDC_MOTOR_DRV  ; $FD1D = モータ ON + ドライブ
                ; --- リクエスト番号で分岐 ($08 = RESTOR / $09 = DWRITE / 他 = DREAD) ---
                LDA     ,X
                SUBA    #$08            ; 本体 +$20。0 = RESTOR / 1 = DWRITE
                BNE     fr_rw
                ; --- RESTOR: トラック 0 への復帰 ---
fr_restor:      LDA     #$08            ; 本体 +$24。RESTORE (即値は本体 +$21)
                BSR     fr_type1
                BRA     fr_exit
                ; --- DWRITE / DREAD: サイド・セクタ・目標トラックを設定する ---
fr_rw:          LDB     6,X
                ANDB    #$01
                STB     <FDC_SIDE       ; $FD1C
                LDB     5,X
                STB     <FDC_SEC        ; $FD1A
                LDB     ,U              ; このドライブのヘッドのトラックの控え ($FFE1+ドライブ)
                STB     <FDC_TRK        ; $FD19 = 控え。呼出側がトラックレジスタを変えていても、
                                        ; 位置決めと ID の照合が控え (実際のヘッドの位置) から始まる
                CMPA    #$01
                BNE     fr_dread        ; $09 以外は DREAD
                ; --- DWRITE: 目標トラックへ SEEK して WRITE SECTOR ---
                LDA     #$1C            ; 本体 +$3C。SEEK (即値は本体 +$3D)
                BSR     fr_type1
                BNE     fr_exit         ; ノットレディ・待ち切れ (A = エラー番号)
                LDU     2,X             ; 入出力データエリア
                LDA     #$A0
                STA     <FDC_CMD
                LDY     #DATA_WAIT_OUT  ; データ待ちの外側の繰り返し
                ; --- DRQ ごとに 1 バイト送る。INTRQ で完了 ---
fr_wr:          LDX     #$8000          ; 内側の繰り返し (X は退避済なので流用する)
fr_wr_p:        LDB     <FDC_DRQ
                BMI     fr_wr_drq       ; bit7 = DRQ
                ASLB
                BMI     fr_done         ; bit6 = INTRQ → 完了
                LEAX    -1,X
                BNE     fr_wr_p
                LEAY    -1,Y            ; 外側は Y で数える (LEAU は条件コードを変えない)
                BNE     fr_wr
                BRA     fr_timeover
fr_wr_drq:      LDA     ,U+
                STA     <FDC_DATA
                BRA     fr_wr
fr_ok:          CLRA
                ; --- 共通の出口: A = エラー番号 ---
fr_exit:        PULS    CC,DP,X         ; 呼出時の割込マスク・DP・X を復元
                STA     1,X             ; RCB+1 = エラー番号 (Z は A に従う)
                ANDCC   #$FE            ; キャリー = 0
                BEQ     fr_ret
                ORCC    #$01            ; エラー → キャリー = 1
fr_ret:         RTS
fr_timeover:    LDA     #ERR_TIMEOVER
                BRA     fr_exit

;------------------------------------------------------------------------------
; fr_type1 — Type I コマンド (RESTORE / SEEK) を発行し、完了を待つ
;   入力: A = コマンド。X = RCB。U → BIOS ワークのトラックの控え。トラックレジスタは
;         fr_rw が控えから書き戻してある (RESTORE はトラックレジスタを使わない)。
;   復帰: A = エラー番号 (0 = 正常。Z は A に従う)。正常ならヘッドのトラックを控えに写す
;         (RESTORE の後は 0)。10 = ノットレディ (ステータス bit7)、15 = 待ち切れ。
;------------------------------------------------------------------------------
fr_type1:       LDB     4,X
                STB     <FDC_DATA       ; $FD1B = 目標トラック (RESTORE では使われない)
                STA     <FDC_CMD
                BSR     fdc_wait        ; BUSY 解除待ち (C=1 なら待ち切れ)
                LDA     #ERR_TIMEOVER
                BCS     ft_ret          ; 待ち切れ → 15
                LDB     <FDC_CMD        ; Type I ステータス
                LDA     #ERR_NOTREADY
                TSTB
                BMI     ft_ret          ; bit7 = ノットレディ → 10
                ; bit4 (シークエラー) では止めない (動作観察。呼び手が自前で RESTORE を出して
                ;   ヘッドを動かした後に DREAD を呼んでも、読出しを続ける)。位置の誤りは READ / WRITE SECTOR の ID の照合が
                ;   レコードノットファウンド (12) として返す。トラックレジスタは SEEK の後は
                ;   目標トラックになっているので控えに写す
                LDA     <FDC_TRK
                STA     ,U              ; $FFE1+ドライブ = ヘッドのトラック
                CLRA
ft_ret:         TSTA
                RTS

                ; --- DREAD: 目標トラックへ SEEK して READ SECTOR ---
                ;   ヘッドが既に目標トラックに在るとき (このドライブのヘッドのトラックの
                ;   控え ($FFE1+ドライブ) が目標と一致) は SEEK を出さずに READ SECTOR に
                ;   入る (トラックレジスタは fr_rw が控えから書き戻してあるので、ID の照合は
                ;   控えで行われる)。同じトラックへ SEEK を毎回出すと、位置決めの完了を待つ
                ;   間に目標セクタがヘッドを通り過ぎ、連続セクタの読出しが 1 セクタごとに
                ;   媒体 1 回転ぶん遅れるため。
fr_dread:       CMPB    4,X             ; B = 控え (fr_rw で設定)。目標トラックと比べる
                BEQ     fr_dgo
                LDA     #$1C            ; SEEK
                BSR     fr_type1
                BNE     fr_exit         ; ノットレディ・待ち切れ (A = エラー番号)
fr_dgo:         LDU     2,X             ; 入出力データエリア
                LDA     #$80
                STA     <FDC_CMD
                LDY     #DATA_WAIT_OUT  ; データ待ちの外側の繰り返し
                ; --- DRQ ごとに 1 バイト受ける。INTRQ で完了 ---
fr_rd:          LDX     #$8000          ; 内側の繰り返し
fr_rd_p:        LDB     <FDC_DRQ
                BMI     fr_rd_drq
                ASLB
                BMI     fr_done
                LEAX    -1,X
                BNE     fr_rd_p
                LEAY    -1,Y            ; 外側は Y で数える (LEAU は条件コードを変えない)
                BNE     fr_rd
                BRA     fr_timeover
fr_rd_drq:      LDA     <FDC_DATA
                STA     ,U+
                BRA     fr_rd

                ; --- 転送完了: BUSY 解除を待ち、Type II ステータスをエラー番号へ写す ---
fr_done:        LDB     <FDC_CMD
                BITB    #$01
                BNE     fr_done
                ANDB    #$FC
                BEQ     fr_ok           ; エラービット無し
                LDA     #ERR_NOTREADY-1
                LDX     #fr_errtab
fr_scan:        INCA
                BITB    ,X+
                BEQ     fr_scan
                BRA     fr_exit
;------------------------------------------------------------------------------
; $FF5E — ディスク BIOS のもう 1 つの入口 (動作観察)
;   X = RCB で、処理アドレス $FE02 / $FE05 / $FE08 を経ずに本体を呼べる。
;   boot_init と同じ位置に置く。fr_errtab・fdc_wait・basic_vec・spin_wait はこの後ろに
;   置き、本体 fdc_rw の並び (+$21 / +$3D) を変えない。
;------------------------------------------------------------------------------
                ZMB     $FF5E-*
                JMP     fdc_rw                  ; $FF5E: X = RCB、種別に従って処理する

fr_errtab:      FCB     $80             ; 10 ドライブノットレディ
                FCB     $40             ; 11 ライトプロテクト
                FCB     $14             ; 12 ハードエラー (レコードノットファウンド / ロストデータ)
                FCB     $08             ; 13 CRC エラー
                FCB     $20             ; 14 DD マーク検出 (レコード種別)

;------------------------------------------------------------------------------
; fdc_wait — FDC の BUSY 解除待ち ($FD18 bit0)。約 1 秒で待ち切れ。
;   復帰: C = 0 解除 / C = 1 待ち切れ。A・B・Y 破壊。
;------------------------------------------------------------------------------
fdc_wait:       ANDCC   #$FE
                LDB     #2
fw_outer:       LDY     #0
fw_lp:          LDA     <FDC_CMD
                BITA    #$01
                BEQ     fw_ret
                LEAY    -1,Y
                BNE     fw_lp
                DECB
                BNE     fw_outer
                ORCC    #$01
fw_ret:         RTS

;------------------------------------------------------------------------------
; basic_vec — BASIC の飛び先を X に得る
;   入力 B = 0 (BREAK キー ON) / 2 (ディスク起動の不成立)。出力 X = 飛び先。
;   目印 "7T" があれば当方の BASIC ROM の起動ベクタの表を、無ければ本 ROM 内の表
;   fbasic_ent (参考書籍に載るホットスタート / コールドスタート) を、同じ添字 B で引く。
;------------------------------------------------------------------------------
basic_vec:      LDX     #fbasic_ent
                LDU     BASVEC_SIG
                CMPU    #$3754          ; "7T"
                BNE     bv_take
                LDX     #BASVEC_COLD
bv_take:        ABX
                LDX     ,X
                RTS
fbasic_ent:     FDB     FBASIC_HOT      ; +0: BREAK キー ON
                FDB     FBASIC_COLD     ; +2: ディスク起動の不成立

;------------------------------------------------------------------------------
; spin_wait — READY 待ちの 1 回ぶんの待ち (SPIN_WAIT_N 回の空回し)。Y 破壊。
;   モータ ON からドライブが READY になるまでの時間はドライブで違う。決め打ちで長く
;   待つ代わりに、この待ちと RESTOR を reset_init が READY_TRIES 回まで繰り返す。
;------------------------------------------------------------------------------
spin_wait:      LDY     #SPIN_WAIT_N
sw_lp:          LEAY    -1,Y
                BNE     sw_lp
                RTS

;------------------------------------------------------------------------------
; ctl_init — RS-232C 等の制御レジスタの初期化 (参考書籍)
;   $FD07 (RS-232C のコマンド。参考書籍) と $FD25 / $FD27 / $FD29 /
;   $FD2B (拡張の制御レジスタ。参考書籍) に $00 を 3 回書いてから $40 を書く
;   (USART の内部リセットの手順。AV ブートイメージ avboot の nb_ctlinit と同じ)。
;   X と Y は保つ (X = 飛び先、Y = IPL へ渡す $0500)。A・B・U 破壊。
;------------------------------------------------------------------------------
ctl_init:       PSHS    Y
                LDY     #ctl_tab
                LDA     #5              ; 表の件数
                LDB     #CTL_RESET
ci_lp:          LDU     ,Y++            ; 制御レジスタのアドレス
                CLR     ,U
                CLR     ,U
                CLR     ,U
                STB     ,U
                DECA
                BNE     ci_lp
                PULS    Y,PC
ctl_tab:        FDB     $FD07,$FD25,$FD27,$FD29,$FD2B

;==============================================================================
; $FFE0-$FFEF は BIOS ワーク (RAM。参考書籍) なのでイメージでは 0。
; $FFF0-$FFFF は 6809 のベクタ。$FFF2-$FFFC は $01D1-$01E2 の割り込みジャンプ領域を
; 指す (参考書籍)。$FFFE = $FE00 (参考書籍)。
;==============================================================================
                ENDC
                ZMB     $FFE0-*                 ; コードは $FFE0 の手前で終える
                ZMB     16                      ; $FFE0-$FFEF BIOS ワーク
                FDB     $0000                   ; $FFF0 (予約)
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+0                   ; $FFF2 SWI3
                ELSE
                FDB     $01D1                   ; $FFF2 SWI3
                ENDC
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+3                   ; $FFF4 SWI2
                ELSE
                FDB     $01D4                   ; $FFF4 SWI2
                ENDC
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+15                   ; $FFF6 FIRQ
                ELSE
                FDB     $01E0                   ; $FFF6 FIRQ
                ENDC
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+12                   ; $FFF8 IRQ
                ELSE
                FDB     $01DD                   ; $FFF8 IRQ
                ENDC
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+6                   ; $FFFA SWI
                ELSE
                FDB     $01D7                   ; $FFFA SWI
                ENDC
                IFDEF   STAGE_ALT_RAM
                FDB     STAGE_IRQ+9                   ; $FFFC NMI
                ELSE
                FDB     $01DA                   ; $FFFC NMI
                ENDC
                FDB     boot_entry              ; $FFFE RESET = $FE00
