; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; AltROMs avboot.bin — FM77AV 系 AV ブートイメージ (640 byte、AVBOOT_ORG 基底)
;
; initiate.rom の file offset $1C00 に埋め込まれ、起動時に initiate 本体が
; AVBOOT_ORG から 640 byte の RAM へ転送する。転送先 AVBOOT_ORG ($3000) は当方の
; ROM 同士の取り決めで当方が選んだ位置 (docs/BUILD.md §5.5)。
;
; ★ 世代の適用範囲: 本イメージを取込むのは **中位世代 (20 系) 以降** の変種
;   (initbase / initiate / initex) だけである。ドライブ 1 以降からの起動は
;   中位世代以降の起動仕様であり、第 1 世代 (initav1) は本イメージを持たず、
;   ディスク起動の対象は **ドライブ 0 のみ** となる (resident boot 直行)。
;   第 1 世代の変種へ本イメージを取込んではならない。
;
; 役割:
;   1. BREAK 押下リセット ($FD04 bit1=0) のみ BASIC 強制: F-BASIC コールド
;      コールド入口へ引渡す。FDC には一切触れない。
;   2. それ以外は起動モード (BASIC/DOS) に依らず、**ドライブ 0 → 1 → 2 → 3 の
;      順に**起動 (IPL ロード) を試行する。
;      各ドライブについて
;        (a) モータ ON → スピンアップ待機 → RESTORE 再発行 → Type I ステータス
;            bit7=NotReady 判定 (ドライブ READY / メディア存在のプリフライト)
;        (b) READY なら起動セクタ (トラック 0 / サイド 0 / セクタ 1) を $0100 へ
;            読めるかを試す (= IPL ロード試行)
;      を行い、最初に成立したドライブから起動する:
;        ドライブ 0 で成立 → resident boot ($FE00、initiate がモード別イメージを
;                            転送済) へ引渡す (規約どおりの引渡し経路)。
;        ドライブ 1-3 で成立 → resident boot は起動ドライブを 0 決め打ちで
;                            扱うため引渡せない。本イメージが resident boot の公開
;                            FDC サービス (BOOTSVC_RW) を使って当該ドライブから IPL を
;                            読込み、制御レジスタ / 表示を初期化して $0100 へ
;                            引渡す。
;        全ドライブ不成立 → モータ OFF し、BASIC モードなら BASIC の二次/ウォーム
;                            入口へ (A=0 規約)、DOS モードならブザーを鳴らして
;                            止まる (参考書籍)。
;      ★ IPL の置き場は起動モードに依らず $0100。IPL は $0100〜$02FF の
;        絶対アドレスを読み書きしてよく、置き場を変えると参照先が食い違う。
;      IPL / BASIC へ渡す前に RS-232C 等の制御レジスタを初期化する (参考書籍)。
;      ★ ドライブが READY でない (メディア無し) 状態でセクタ READ を発行しない
;        (動作観察に基づく挙動)。READ を出すのはプリフライトが READY を返した後だけ。
;
;
; アドレスのピン留めについて:
;   本イメージは入口のアドレスを固定しない。FDC は
;   常駐ブートイメージが供給するサービス入口 (BOOTSVC_RW) を呼ぶ。これは当方の ROM
;   同士の取り決めで、当方が選んだアドレスである。
;   常駐ブートイメージは initiate が本イメージより先に必ず転送しているため常に有効。
;==============================================================================

STACK_TOP       EQU     $FC80           ; 起動中スタック (RAM 末端付近)
BOOTIMG_ENT     EQU     $FE00           ; resident boot 入口 (initiate が転送済)
                IFNDEF  STAGE_ALT_RAM
BOOTSVC_RW      EQU     $FE08           ; 起動 ROM の DREAD 入口 (参考書籍)
                ENDC
; --- BASIC の起動ベクタ ---
;   当方の BASIC ROM (7tbasic3) は目印 ("7T") と 2 つの起動ベクタを BASVEC_* に置く
;   (当方が独自に選んだアドレス)。目印が無い BASIC ROM のときは、参考書籍に載る
;   とおり、BREAK キー ON ならホットスタート $8684、ディスク起動の不成立なら
;   コールドスタート $848B (A = 0 で ROM モード) へ直接飛ぶ。
BASVEC_SIG      EQU     $FBF4           ; 目印 ("7T")
BASVEC_COLDVEC  EQU     $FBF6           ; +0 [.]=コールド入口 / +2 [.]=二次/ウォーム入口 (A=0 必須)
                IFNDEF  STAGE_ALT_RAM
FBASIC_HOT      EQU     $8684           ; BASIC ホットスタート (参考書籍)
                ENDC
                IFNDEF  STAGE_ALT_RAM
FBASIC_COLD     EQU     $848B           ; BASIC コールドスタート (同上、A = 0 で ROM モード)
                ENDC
                IFNDEF  STAGE_ALT_RAM
IPL_LOAD        EQU     $0100           ; IPL ロード先 / 実行先 (起動モードに依らず)
IPL_Y           EQU     $0500           ; IPL へ渡す Y (IPL が Y を置かずに
                                        ;   使ってもよいよう、resident boot
                                        ;   と同じ値を渡す)
                ENDC
IPL_SEC_END     EQU     $11             ; 先読み最終セクタ + 1 (S1..S16)
DRIVE_COUNT     EQU     4               ; 探索するドライブ数 (0,1,2,3)
BUZZER_ON       EQU     $81             ; $FD03 書込 bit7 = 連続ブザー + bit0 = スピーカ
                                        ;   (どちらも 1 でオン。参考書籍)

; --- I/O レジスタ (DP=$FD) ---
FD_BUZZER       EQU     $03             ; $FD03 書込 = ブザー
FD_BOOTJP       EQU     $04             ; bit1 = 0:BREAK 押下 (BASIC 強制)
FD_BOOTDET      EQU     $0B             ; bit0 = 起動モード (0=BASIC/1=DOS)
FD_AUXMODE      EQU     $0F             ; 読取 = F-BASIC ROM 有効化
FDC_CMD         EQU     $18             ; $FD18 W=コマンド / R=ステータス
FDC_SIDE        EQU     $1C             ; $FD1C サイド (bit0)
FDC_MOTOR_DRV   EQU     $1D             ; $FD1D bit7=motor, bits1:0=drive
BIOS_TRACK      EQU     $FFE1           ; +ドライブ番号: そのドライブのヘッドのあるトラック番号 (BIOS ワーク)


AVBOOT_ORG      EQU     $3000           ; 転送先 = 入口 (initiate.s の AVBOOT_ENT と揃える)

                ORG     AVBOOT_ORG

;------------------------------------------------------------------------------
; エントリ
;------------------------------------------------------------------------------
                IFDEF   STAGE_ALT_RAM
nb_start:
                ORCC    #$50
                LDS     #STACK_TOP
                LDA     FD_AUXMODE+$FD00
                LBSR    nb_dispinit
                CLRA
                TFR     A,DP
                JMP     [BASVEC_COLDVEC]
nb_dispinit:
                LDU     #$FD40
                LDA     #7
nbd_loop:
                STA     ,-U             ; $FD3F..$FD38 ← 7..0
                DECA
                BPL     nbd_loop
                CLR     $37+$FD00       ; $FD37 = 0 (全プレーン表示)
                RTS

                ELSE
nb_start:
                ORCC    #$50            ; IRQ/FIRQ マスク
                LDA     #$FD
                TFR     A,DP            ; DP=$FD
                LDS     #STACK_TOP
                ; --- BREAK 押下リセットのみ BASIC 強制 (ディスク探査しない) ---
                ;   BREAK 押下時とディスク起動失敗時とで飛び先が異なる (nb_vec)。
                LDA     <FD_BOOTJP
                BITA    #$02
                BEQ     nb_cold         ; BREAK 押下 → BASIC コールド
                ; --- ドライブ 0 → 1 → 2 → 3 の順に起動を試行 (モード共通) ---
                ;   起動モード (BASIC/DOS) に依らず、起動可能メディアが在れば
                ;   そのドライブから起動する。BASIC モードでも起動ディスクが
                ;   入っていればディスクブートが優先される。
                LDA     #$01
                STA     <FDC_SIDE       ; side toggle (媒体の読取り準備)
                CLR     <FDC_SIDE
                CLR     nb_param+7      ; 探索開始ドライブ = 0
nb_drv_lp:
                LDA     nb_param+7
                ORA     #$80
                STA     <FDC_MOTOR_DRV  ; 探索中ドライブ選択 + モータ ON
                ; --- READY 判定: RESTORE を再発行しながら NotReady 解消を待つ ---
                ;   モータ回転安定・メディア認識の遅延で直後の RESTORE は NotReady
                ;   で完了し得る。ステータスはコマンド完了時に確定する (再読取では
                ;   更新されない) ため、待機を挟んで RESTORE 自体を再発行して確認
                ;   する (動作観察に基づく)。セクタ READ はここでは発行しない。
                ;   ★ 再試行回数はドライブ 0 のみ RDY_TRIES0 回 (待機と合わせて
                ;     上限約 2 秒) とし、ドライブ 1-3 は 2 回に抑える。ドライブ 0 の
                ;     試行でモータ回転は既に安定しており、ここを 4 ドライブ分そのまま
                ;     繰返すと全ドライブ不在時のフォールバックが不必要に長時間化するため。
                ;   ★ 待機 → RESTORE の順序はドライブ 0 について変更しないこと。
                ;     待機の長さは固定ではなく、READY になった時点で抜ける (READY に
                ;     なるまで RESTORE を出し直す) ため、直後に resident boot が行う
                ;     起動セクタ読込は常に READY のドライブに対して行われる。resident
                ;     boot 自身も READY になるまで RESTOR を出し直してから読むので、
                ;     ここでの待機の長さに起動セクタ読込の成否は依存しない。
                LDB     #2
                TST     nb_param+7
                BNE     nb_rdy_lp
                LDB     #RDY_TRIES0
nb_rdy_lp:
                LBSR    nb_spinup       ; 待機 (READY 判定 1 回ぶん)
                LDA     #$0A
                STA     <FDC_CMD        ; RESTORE (Type I)
                LBSR    nb_wait         ; BUSY 解除待ち
                LDA     <FDC_CMD        ; Type I ステータス
                BPL     nb_ipl_try      ; bit7=0 → READY = メディア有
                DECB
                BNE     nb_rdy_lp
                BRA     nb_drv_next     ; NotReady のまま → 次ドライブ
nb_ipl_try:
                ; --- 復帰したので、このドライブのヘッドのトラックの控えを 0 にする ---
                ;   起動 ROM の FDC サービスは位置決めの前に控え ($FFE1+ドライブ、
                ;   参考書籍の BIOS ワーク) をトラックレジスタへ書き戻す。
                ;   ここは RESTORE を自前で出しているので、控えを自分で合わせる。
                LDB     nb_param+7
                LDX     #BIOS_TRACK
                CLR     B,X
                ; --- 起動セクタ (T0/side0/S1) を $0100 へ読めるか試す ---
                ;   読めたドライブを起動ドライブとする (= IPL ロード試行)。
                LDX     #IPL_LOAD
                STX     nb_param+2
                LDA     #$01
                STA     nb_param+5
                LBSR    nb_read
                BEQ     nb_drv_ok
nb_drv_next:
                INC     nb_param+7
                LDA     nb_param+7
                CMPA    #DRIVE_COUNT
                BLO     nb_drv_lp
                ; --- 全ドライブ不成立: モータ OFF → BASIC モードは BASIC へ、
                ;     DOS モードはブザーを鳴らして止まる (参考書籍) ---
                CLR     <FDC_MOTOR_DRV
                LDA     <FD_BOOTDET     ; $FD0B bit0 = 1 → DOS モード
                LSRA
                BCC     nb_basic
nb_dos_fail:    LDA     #BUZZER_ON
                STA     <FD_BUZZER      ; $FD03 bit7 = 連続ブザー ON + bit0 = スピーカ ON
nb_stop:        BRA     nb_stop
nb_drv_ok:
                TST     nb_param+7
                LBNE    nb_ipl_load     ; ドライブ 1-3 → 本イメージが IPL をロードする
                JMP     BOOTIMG_ENT     ; ドライブ 0 → resident boot へ規約どおり引渡す

;------------------------------------------------------------------------------
; nb_cold — BREAK 強制時の BASIC 起動 (FDC 非接触)
;   当方の BASIC ROM ならコールド入口、目印が無い BASIC ROM ならホットスタート
;   $8684 (参考書籍に載る BREAK キー ON の経路)。BASIC ROM 有効化 ($FD0F 読取) は
;   initiate 本体が BASIC イメージ選択時に実施済み。
;------------------------------------------------------------------------------
nb_cold:
                CLRB                    ; +0
                BRA     nb_vec
;------------------------------------------------------------------------------
; nb_basic — ディスク起動不成立時の BASIC フォールバック
;   $FD0F 読取で BASIC ROM を有効化し、当方の BASIC ROM なら二次/ウォーム入口、
;   目印が無い BASIC ROM ならコールドスタート $848B (参考書籍に載る読込失敗の経路) へ。
;------------------------------------------------------------------------------
nb_basic:
                LDA     <FD_AUXMODE     ; $FD0F 読取 = BASIC ROM 有効化
                LDB     #2              ; +2
nb_vec:
                ; 目印 ("7T") が一致すれば当方の BASIC ROM の起動ベクタの表を、
                ;   一致しなければ本イメージ内の表 nb_fbasic_ent を、同じ添字 B で引く。
                LDX     #nb_fbasic_ent  ; 既定 = 目印が無い BASIC ROM の飛び先の表
                LDU     BASVEC_SIG
                CMPU    #$3754          ; "7T" = 当方の BASIC ROM
                BNE     nb_v_take
                LDX     #BASVEC_COLDVEC ; 当方の BASIC ROM の起動ベクタの表
nb_v_take:
                ABX                     ; +0 / +2
                LDX     ,X
nb_go:
                ; --- 制御レジスタ初期化 + 表示初期化 (マルチページ + TTL パレット) ---
                LBSR    nb_ctlinit      ; X は保つ
                LBSR    nb_dispinit
                ; --- BASIC へ ---
                CLRA                    ; A=0 (入口規約: ROM モード / RAM 上限既定)
                TFR     A,DP            ; DP=$00
                JMP     ,X
nb_fbasic_ent:  FDB     FBASIC_HOT      ; +0: BREAK キー ON → ホットスタート $8684
                FDB     FBASIC_COLD     ; +2: ディスク起動不成立 → コールドスタート $848B

;------------------------------------------------------------------------------
; nb_ipl_load — ドライブ 1-3 からの起動 (本イメージが IPL をロードして起動する)
;
;   resident boot ($FE00) の自動起動シーケンスは起動ドライブを 0 として組まれて
;   おり、ドライブ 1-3 を起動元に指定する手段を持たない。そこで本イメージが
;   ドライブ 1-3 の IPL をロードする (ドライブ 0 → 3 の順に試す)。
;
;   起動セクタ (S1) は探索段階で既に $0100 へ読込済なので、ここでは S2 以降を
;   トラック 0 の 16 セクタまで先読みする。ディスク IPL には、先頭セクタだけで
;   なく後続セクタも $0200 以降に置かれたものとして扱えるようにするため
;   (resident boot の自動起動と同じ理由)。読めなくなった時点で打切り、
;   読めた分で起動する。
;
;   FDC アクセスは常駐ブートイメージの FDC サービス (BOOTSVC_RW、パラメータブロック
;   X 渡し) を使う。ドライブ番号はパラメータブロック +7 で渡すため、ドライブ
;   1-3 でも正しいドライブが選択される。
;------------------------------------------------------------------------------
nb_ipl_load:
                LDX     #IPL_LOAD+$0100 ; S2 の転送先 = $0200
                STX     nb_param+2
                LDA     #$02            ; sector = 2
                STA     nb_param+5
nbl_lp:
                LBSR    nb_read
                BNE     nbl_done        ; 読めなくなったら読めた分で起動
                LDD     nb_param+2
                ADDD    #$0100
                STD     nb_param+2
                LDA     nb_param+5
                INCA
                STA     nb_param+5
                CMPA    #IPL_SEC_END
                BLO     nbl_lp
nbl_done:
                ; --- 制御レジスタ初期化 + 表示有効化 → IPL へ ---
                ;   resident boot の自動起動と同じ引渡し状態を作る。
                LBSR    nb_ctlinit
                LBSR    nb_dispinit
                LDY     #IPL_Y          ; IPL へ渡す Y = $0500
                CLRA
                TFR     A,DP            ; DP=$00 (IPL 規約)
                JMP     IPL_LOAD        ; → $0100

;------------------------------------------------------------------------------
; nb_read — nb_param が指すセクタを読む (リトライ 5)。
;   復帰: A=0 / Z=1 成功、A≠0 / Z=0 失敗。B 保存、X 破壊。DP=$FD 前提。
;   FDC アクセスは resident boot の公開 FDC サービス経由。
;   ★ FDC サービスは B を保存しない (復帰時 B にはステータス由来の値が入る)。
;     リトライ回数はサービス呼出のたびにスタックへ退避しなければ、B が毎回
;     同じ値で上書きされて DECB が尽きず無限ループになる。
;------------------------------------------------------------------------------
nb_read:
                PSHS    B
                LDB     #5
                PSHS    B               ; 残り回数。BIOS は B を壊すのでスタックの 1 バイトで数える
nbr_lp:
                LDX     #nb_param
                JSR     BOOTSVC_RW
                TSTA
                BEQ     nbr_done
                DEC     ,S              ; 残り回数
                BNE     nbr_lp
nbr_done:
                LEAS    1,S             ; 残り回数の枠を捨てる (CC は変わらない)
                TSTA
                PULS    B,PC

;------------------------------------------------------------------------------
; nb_param — FDC サービス用パラメータブロック
;   +0 種別 ($0A=READ SECTOR) / +2,+3 転送先 / +4 track / +5 sector /
;   +6 side / +7 drive。+7 は起動ドライブ探索のループ変数を兼ねる。
;   本イメージは RAM 上 (AVBOOT_ORG から 640 byte) へ転送されてから実行されるため書換可。
;------------------------------------------------------------------------------
nb_param:       FCB     $0A,$00,$01,$00,$00,$01,$00,$00

;------------------------------------------------------------------------------
; nb_ctlinit — RS-232C 等の制御レジスタ初期化 ($FD07/$FD25/$FD27/$FD29/$FD2B に
;   $00 を 3 回書いてから $40。参考書籍)
;   resident boot (boot_bas の ctl_init) が IPL / BASIC へ引渡す直前に行うものと同一。
;   X は保つ (飛び先の保持)。A,B,Y,U 破壊。
;------------------------------------------------------------------------------
nb_ctlinit:
                LDY     #nb_ctltab
                LDA     #5              ; 表の件数
                LDB     #$40
nci_lp:
                LDU     ,Y++            ; 制御レジスタのアドレス
                CLR     ,U
                CLR     ,U
                CLR     ,U
                STB     ,U
                DECA
                BNE     nci_lp
                RTS
nb_ctltab:      FDB     $FD07,$FD25,$FD27,$FD29,$FD2B

;------------------------------------------------------------------------------
; nb_dispinit — 表示初期化 (マルチページ + TTL パレット、$FD37-$FD3F)
;   A,U 破壊。X は保存する (引渡し先アドレスの保持に使われるため)。
;------------------------------------------------------------------------------
nb_dispinit:
                LDU     #$FD40
                LDA     #7
nbd_loop:
                STA     ,-U             ; $FD3F..$FD38 ← 7..0
                DECA
                BPL     nbd_loop
                CLR     <$37            ; $FD37 = 0 (全プレーン表示)
                RTS

;------------------------------------------------------------------------------
; nb_wait — Type I コマンド完了 (BUSY=0) 待ち。A,B,Y 破壊 (B は呼出側退避)。
;------------------------------------------------------------------------------
nb_wait:
                PSHS    B
                LDB     #4
nw_outer:
                LDY     #$FFFF
nw_lp:
                LDA     <FDC_CMD
                BITA    #$01            ; BUSY?
                BEQ     nw_done
                LEAY    -1,Y
                BNE     nw_lp
                DECB
                BNE     nw_outer
nw_done:
                PULS    B,PC

;------------------------------------------------------------------------------
; nb_spinup — READY 判定 1 回ぶんの待機 (SPIN_WAIT_N 回の空回し、8 サイクル/回)。X 破壊。
;
;   モータ ON からドライブが READY になるまでの時間はドライブで違う。決め打ちで
;   長く待つ代わりに、この短い待機と RESTORE の再発行を nb_rdy_lp が繰返し、
;   READY になった時点で抜ける (ドライブ 0 は RDY_TRIES0 回、1-3 は 2 回まで)。
;   $FD1D のモータ線はドライブ共通で、探索の 2 台目以降および同一ドライブの
;   再試行の時点では既に回転しているため、待機は毎回同じ長さでよい。
;
;   ★ 待機 → RESTORE の順序は変更しないこと (RESTORE の完了ステータスで READY を
;     判定するため、待機を後にすると判定が 1 回ぶん遅れる)。
;------------------------------------------------------------------------------
SPIN_WAIT_N     EQU     $3000           ; 1 回の待機 (2 MHz で約 50 ms)
RDY_TRIES0      EQU     24              ; ドライブ 0 の READY 判定回数 (上限約 2 秒)

nb_spinup:
                LDX     #SPIN_WAIT_N
ns_lp:
                LEAX    -1,X
                BNE     ns_lp
                RTS

;==============================================================================
; パディング (イメージ長 $280 = 転送プロトコル固定)
;==============================================================================
                ENDC
                ZMB     AVBOOT_ORG+$280-*
