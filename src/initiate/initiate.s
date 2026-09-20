; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; AltROMs initiate.rom — FM77AV 系 メイン CPU イニシエート ROM (独立実装)
;
; 8192 byte 固定。メイン CPU $6000-$7FFF にオーバレイされ、電源 ON / RESET 直後に
; 最初に実行される ROM。リセット直後 PC=$6000 (ハードがオーバレイのリセットベクタを
; $FE00-$FFFF にミラー供給し $6000 を指す) から本コードが走る。
;
; 役割 (外部から観察できる挙動 (動作観察) に基づく独立実装):
;   1. サウンドサブシステム (PSG/OPN 互換) を既定状態へ初期化。
;   2. アナログパレット LUT ($FD30-$FD34、4096 エントリ) を初期化。
;   3. メモリマッピングレジスタ (MMR) の SEG 0 ($FD80-$FD8F) を初期化。
;   4. 起動モード ($FD04 bit1 / $FD0B bit0) を判定し、resident boot イメージ
;      (bas_image / boot_image) を $FE00-$FFFF へ転送。
;   5. AV ブートイメージ (avboot_image、640 byte) を AVBOOT_ENT (当方が選んだ
;      RAM 上の位置、$3000) へ転送。
;   6. スタックの上に積んだトランポリンからオーバレイを解除 ($FD10 bit1=1) して
;      AVBOOT_ENT へ引渡す。
;      (解除時にハードはサブモニタを FM-7 互換 Type-C へ切替える)
;      AV ブートイメージ側が BASIC の二次入口 / DOS (常駐ブートイメージ = FDC ディスク起動) を
;      最終分岐する。DOS モードで起動できる媒体が無いときはブザーを鳴らして止まる
;      (参考書籍)。BASIC モードで媒体が無いときは F-BASIC へ。
;
; 設計方針: 独自シーケンスで起動を実現する。起動イメージは自プロジェクト
; 独自ビルドを用いる。固定アドレスの根拠は次のとおり分かれる。
;   ・参考書籍に載る事実:
;     オーバレイの範囲 $6000-$7FFF と $FD10 bit1 による有効/無効、
;     サブシステム ROM の型を選ぶ $FD13、MMR $FD80-$FD8F、
;     イメージ内の配置 (音色表 $6C00、起動イメージ $7800 / $7A00)、
;     TTL パレット $FD38-$FD3F とアナログパレット $FD30-$FD34。
;   ・動作観察を根拠とするもの:
;     F-BASIC の起動ベクタの位置 ($FBFC / $FBFE) と A / X の受け渡し、機種識別域
;     $6B00-$6B13、AV ブートイメージの位置 $7C00 (内部の取り決め)。
;
; 出典凡例: 本コードが参照するアドレス・入口 ($8000-$FBFF のコールド
;   入口・二次入口等) は、参考書籍と外部から観察できる挙動 (動作観察) から得た
;   互換要件である。書誌・利用条件・免責は docs/LEGAL.md を参照。
;   どのアドレスがどちらを根拠とするかは上の設計方針のとおり。
;
;------------------------------------------------------------------------------
; 機種別変種 (条件アセンブル。ソースは本ファイル 1 本のみ)
;
;   外部から観察できる挙動 (動作観察) が示すとおり、本 ROM が担う起動処理は
;   世代で異なる。当実装もそれに合わせて 4 通りを作り分ける。
;
;   | 記号       | 生成物        | 対象世代 | AV ブートイメージ | 引渡し先 | 機種区分 |
;   |------------|---------------|----------|---------------|----------|----------|
;   | (未定義)   | initiate.rom  | 上位世代 | 転送する      | AVBOOT_ENT | "40"     |
;   | INIT_BASE  | initbase.rom  | 中位世代 | 転送する      | AVBOOT_ENT | "20"     |
;   | INIT_AV1   | initav1.rom   | 第 1 世代| 持たない      | $FE00      | 提示なし |
;   | INIT_EXSX  | initex.rom    | 拡張世代 | 転送する      | AVBOOT_ENT | "40"     |
;
;   拡張世代 (INIT_EXSX) は上位世代と同じ起動処理を持ち、機種識別域の世代名
;   ("AV4EX") と $6B10 の識別値が異なる。フロッピー以外の起動デバイスからの起動は持たない
;   (参考書籍に無い)。
;
;   第 1 世代 (INIT_AV1) は AV ブートイメージ機構そのものを備えない世代であり、
;   起動用 ROM イメージを resident boot 領域へ複写したうえで **その場で実行** する
;   (複写のみを行い AV ブートイメージへ引渡す上位世代とはここが異なる)。従って
;   INIT_AV1 では file offset $1C00 の AV ブートイメージを持たず、トランポリンの
;   引渡し先も $FE00 になる。
;
;   FM 音源初期化とアナログパレット初期化は動作観察のとおり **全世代で実施**
;   されるため、いずれの変種でも保持する。CRTC 初期化は上位世代のみ必要な処理だが、
;   当実装はもともと CRTC を操作しないので変種間の差は生じない。
;==============================================================================

; --- I/O レジスタ (DP=$FD でダイレクトページ参照) ---
FD_MODE93       EQU     $93             ; メモリモード ($FD93 bit0=1 で $FE00-$FFFF を RAM 化)
FD_SUBCTL       EQU     $15             ; 音源のコマンドレジスタ ($FD15)
FD_SUBDAT       EQU     $16             ; 音源のデータレジスタ ($FD16)
FD_BOOTJP       EQU     $04             ; bit1 = 起動モードジャンパ
FD_BOOTDET      EQU     $0B             ; bit0 = 起動詳細フラグ
FD_AUXMODE      EQU     $0F             ; 補助モードフラグ
FD_OVLCTL       EQU     $10             ; オーバレイ/RAM 切替 (bit1=1 で解除)
FD_PIACTL       EQU     $1E             ; PIA 制御 (起動時に出力モードへ設定する)
FD_MMRSEL       EQU     $90             ; MMR セグメント (バンク) 選択
FD_PALIDX_HI    EQU     $30             ; アナログパレット index 上位
FD_PALIDX_LO    EQU     $31             ; アナログパレット index 下位
FD_PAL_B        EQU     $32             ; パレット 青
FD_PAL_R        EQU     $33             ; パレット 赤
FD_PAL_G        EQU     $34             ; パレット 緑

; --- 絶対アドレス ---
STACK_TOP       EQU     $FC80           ; 起動中スタック (RAM 末端付近)
MMR_BASE        EQU     $FD80           ; MMR レジスタ先頭 (16 セグメント分)

                ORG     $6000

;------------------------------------------------------------------------------
; リセット入口 ($6000)
;------------------------------------------------------------------------------
reset_entry:
                ORCC    #$50            ; IRQ/FIRQ マスク (起動中は割込禁止)
                LDA     #$FD
                TFR     A,DP            ; DP=$FD ($FDxx を直接アドレスで参照)
                LDS     #STACK_TOP

                ; --- 段1: PIA / MMR SEG 0 初期化 ---
                ;   $FD80-$FD8F は MMR (メモリマッピングレジスタ) で、16 個の
                ;   セグメントそれぞれに 4KB 物理ページ番号を保持する。$30..$3F
                ;   の連番は 64KB の論理アドレスを同じ物理アドレスへ対応させる既定値である。
                ;
                ;   ★ $FD80-$FD8F への読み書きは $FD90 (MMR セグメント/バンク
                ;     選択) が選んでいるバンクに対して行われる。従って初期化の
                ;     前に $FD90 を 0 にしてバンク 0 (SEG 0) を選んでおかなければ
                ;     ならない。これを怠ると、リセット前の値が $FD90 に残っている
                ;     再起動 (ホットリセット) で SEG 0 以外のバンクを初期化して
                ;     しまい、SEG 0 は未初期化のまま残る。本 ROM に求められる
                ;     「MMR 初期化 (SEG 0)」は、この先行選択を含めて初めて成立する。
                ;   連番は末尾から先頭へ向けて詰める (値を先に 1 つ減らしてから
                ;     書くので、書く値と書く先が 1 本の下降で揃う)。
                LDA     #$40
                STA     <FD_PIACTL      ; PIA 制御 = CB2 出力モード
                CLR     <FD_MMRSEL      ; $FD90 = 0 (SEG 0 = バンク 0 を選択)
                LDX     #MMR_BASE+16
                LDB     #$40
mmr_loop:
                DECB
                STB     ,-X             ; $FD8F..$FD80 へ $3F..$30
                CMPX    #MMR_BASE
                BHI     mmr_loop

                ; --- 段2: メモリモード設定 ($FD93 bit0=1 で $FE00-$FFFF を RAM 化) ---
                ; resident boot イメージを $FE00-$FFFF へ展開し、そこで実行・自己書換する
                ; ため、この領域を RAM 化しておく (bit0=0 のままだと書込が無視される)。
                LDA     #$01
                STA     <FD_MODE93

                ; --- 段3: アナログパレット LUT 初期化 ---
                LBSR    init_palette

                ; --- 段4: サウンドサブシステム初期化 ---
                ;   PIA を整えた後に行う (ストローブ線の向きが定まってから鳴らす)。
                LBSR    init_sound

                ; --- 段5: ブートイメージを展開して起動 ---
                ;   起動モード ($FD04 bit1 / $FD0B bit0) で BASIC/DOS ブートイメージを
                ;   選択して $FE00 へ転送し、さらに AV ブートイメージを AVBOOT_ENT へ転送してから、
                ;   スタックの上に積んだトランポリン経由でオーバレイを解除し
                ;   AVBOOT_ENT へ引渡す。
                LBSR    boot_handoff
                ; ここには戻らない

;------------------------------------------------------------------------------
; boot_handoff — ブートイメージ展開 → オーバレイ解除 → JMP AVBOOT_ENT
;
;   1. 起動モード判定 (動作観察に基づく):
;        DOS = ($FD04 bit1 = 1) かつ ($FD0B bit0 = 1)
;        ($FD04 bit1=0 は BREAK 押下リセット = BASIC 強制。$FD0B bit0 は
;         起動モード設定 0=BASIC / 1=DOS)
;      に従い resident boot イメージを選択して $FE00-$FFFF へ転送する:
;        BASIC → bas_image ($7800 = file offset $1800、boot_bas イメージ)
;        DOS   → boot_image ($7A00 = file offset $1A00、boot_init イメージ)
;      いずれも FDC サービスの実体を持つ 512 byte イメージ。
;      BASIC 選択時は $FD0F を読取り F-BASIC ROM を有効化する (動作観察に基づく)。
;   2. AV ブートイメージ (avboot_image、AVBOOT_ENT 基底 640 byte) を
;      AVBOOT_ENT から AVBOOT_LEN バイトの RAM へ転送する。転送先は当方の ROM 同士の内部の取り決め
;      (docs/BUILD.md §5.5)。
;   3. スタックの上に積んだトランポリンからオーバレイを解除し AVBOOT_ENT へ
;      引渡す。AV ブートイメージ側が
;      起動モードとメディア存在を判定し、$FE00 (ディスク起動) または F-BASIC
;      (二次入口) へ最終分岐する。
;
; ★ 重要 (固定アドレス制約): オーバレイ解除 ($FD10 bit1=1) を行うと、解除した
;   瞬間に $6000-$7FFF (= 本 ROM が走っているオーバレイ領域) が素の RAM に戻る。
;   従って解除命令の直後の命令フェッチはもう本 ROM の命令ではなく RAM の中身に
;   なってしまう。解除と「ブートイメージへのジャンプ」は必ず **オーバレイ領域の外
;   (RAM)** から実行しなければならない。そこで小さなトランポリンを起動中の
;   スタック (STACK_TOP から下へ伸びる RAM) の上に積み、そこへジャンプしてから
;   解除 + JMP を行う。トランポリンの置き場所は固定アドレスにしない (S の値から
;   導く)。
;
; ★ アドレスもコードも固定しない (参考書籍に記載が無いため)。ROM の配置 (起点・
;   寸法・リセットベクタのワード) は固定入口ではないので対象外。
;------------------------------------------------------------------------------
AVBOOT_ENT      EQU     $3000           ; AV ブートイメージの転送先と入口 (当方が選んだ位置。
                                        ;   docs/BUILD.md §5.5。avboot.s の AVBOOT_ORG と揃える)
AVBOOT_LEN      EQU     640             ; AV ブートイメージの長さ (byte)
BOOTIMG_ENT     EQU     $FE00           ; resident boot イメージエントリ (FDC ディスク起動)


boot_handoff:
                ; 表示サブシステム TTL 初期化 (マルチページ表示 + パレット)。
                LBSR    init_display
                ; --- 起動モード判定 → resident boot イメージ選択 ---
                LDU     #bas_image      ; 既定 = BASIC イメージ
                LDA     <FD_BOOTJP      ; $FD04
                BITA    #$02
                BEQ     bh_basic_sel    ; bit1=0 (BREAK) → BASIC
                LDA     <FD_BOOTDET     ; $FD0B
                ANDA    #$01
                BEQ     bh_basic_sel    ; bit0=0 → BASIC
                LDU     #boot_image     ; DOS イメージ
                BRA     bh_img_copy
bh_basic_sel:
                LDA     <FD_AUXMODE     ; $FD0F 読取 = F-BASIC ROM 有効化
bh_img_copy:
                ; 選択イメージを $FE00-$FFFF へワード転送 (512 byte)。
                ; ($FD93 bit0=1 で $FE00-$FFFF を RAM 化済みなので書込が届く)
                LDX     #$FE00
bh_copy:
                PULU    D               ; = LDD ,U++
                STD     ,X++
                CMPX    #$0000          ; $FFFF を超えて 0 に折返したら完了
                BNE     bh_copy
                ; --- 解除トランポリンをスタックの上へ積む ---
                ;   置き場所を固定アドレスにしない。末尾から 1 バイトずつ S の上へ積み、
                ;   積み終えた先頭 (= S) へ飛ぶ。トランポリンは先頭で S を STACK_TOP へ
                ;   戻し、以後 JMP まで push を行わないので、積んだコードは壊れない。
                LDU     #tramp_end
bh_tpush:
                LDA     ,-U
                PSHS    A
                CMPU    #tramp_src
                BHI     bh_tpush
                ; --- AV ブートイメージ転送分岐 (配置固定部) へ ---
                ; $FD93 bit0 (1 で $FE00-$FFFF が書込可) はトランポリン (tramp_src) で
                ; 0 に戻してから引渡す。転送した resident boot イメージはコードと定数の
                ; 置かれる $FE00-$FFDF を書き換えない (RCB は定数、やり直しの回数は
                ; スタックで数える) ので、書込可のままにしておく必要が無い。BIOS ワーク
                ; $FFE0-$FFEF はこのビットに関わらず RAM で、FDC サービスはそこへ書く。
                IFDEF   INIT_AV1
                ; 第 1 世代: AV ブートイメージ機構を持たない。転送は行わず、そのまま
                ; トランポリン (スタック上の RAM) へ進んで resident boot ($FE00) を
                ; 起動する (= 複写した起動用 ROM イメージをその場で実行する世代)。
                JMP     ,S
                ELSE
                BRA     nb_copy
                ENDC

;------------------------------------------------------------------------------
; AV ブートイメージ転送 — 第 1 世代 (INIT_AV1) では非搭載
;   通常配置。アドレスも相対配置も固定しない (参考書籍に記載が無いため)。
;------------------------------------------------------------------------------
                IFNDEF  INIT_AV1
nb_copy:
                LDU     #avboot_image+AVBOOT_LEN ; 転送元末尾
                LDX     #AVBOOT_ENT+AVBOOT_LEN ; 転送先末尾
nbc_loop:
                LDD     ,--U           ; 末尾から先頭へ向けてワード転送
                STD     ,--X
                CMPX    #AVBOOT_ENT    ; 先頭に達したら完了
                BHI     nbc_loop
nb_done:
                ; トランポリン (スタック上の RAM) へ。ここでオーバレイを解除しても
                ; 実行中のコードは RAM 上なので破壊されない。
                JMP     ,S
                ENDC

;------------------------------------------------------------------------------
; tramp_src — スタックの上へ積まれ RAM 上で実行される解除トランポリン。
;   スタック/DP を起動既定へ設定し、オーバレイ ($FD10 bit1=1) を解除してから
;   AV ブートイメージ (AVBOOT_ENT) へ JMP する。(解除副作用でサブモニタが Type-C へ
;   切替わる。A=0 は F-BASIC 入口規約に合わせた既定値。)
;   引渡しは絶対アドレス形のジャンプで組む (トランポリンは RAM 上で走るため
;   相対分岐の基準が ROM 側と異なる)。
;------------------------------------------------------------------------------
tramp_src:
                LDS     #STACK_TOP      ; スタック既定 ($FC80)
                IFNDEF  STAGE_ALT_RAM
                LDA     <FD_MODE93
                ANDA    #$FE            ; bit0=0 (他ビットは変えない)
                STA     <FD_MODE93
                ENDC
                LDA     #$02
                STA     $FD10           ; オーバレイ解除 (RAM から実行)
                CLRA                    ; A=0 (F-BASIC 入口規約)
                TFR     A,DP            ; DP=$00
                IFDEF   INIT_AV1
                JMP     BOOTIMG_ENT     ; 第 1 世代: resident boot ($FE00) 直行
                ELSE
                JMP     AVBOOT_ENT     ; AV ブートイメージへ
                ENDC
tramp_end:

;------------------------------------------------------------------------------
; init_display — マルチページ表示 + TTL パレット初期化 ($FD37-$FD3F)
;   $FD3F..$FD38 に 7..0 を書き (TTL パレット既定)、$FD37=0 (全プレーン表示)。
;   DP=$FD 前提。A,U 破壊。
;------------------------------------------------------------------------------
init_display:
                LDU     #$FD40
                LDA     #7
id_loop:
                STA     ,-U             ; $FD3F..$FD38 ← 7..0
                DECA
                BPL     id_loop
                CLR     <$37            ; $FD37 = 0 (全プレーン表示)
                RTS

;------------------------------------------------------------------------------
; init_sound — サウンドサブシステム (PSG/OPN 互換) を既定状態へ
;   $FD16 (データ) / $FD15 (コマンド) の手順は参考書籍のとおり:
;     レジスタ番号 → $FD15 に $03 (ラッチアドレス) → $00 (インアクティブ) →
;     データ → $FD15 に $02 (ライトデータ) → $00 (インアクティブ)。
;   参考書籍はデータを書く前にステータスの bit7 (ビジー) を見るが、起動時は
;   前の書込みが無く、ここでは待たない。
;   表は 2 段: 先頭はプリスケーラ (レジスタ番号のラッチだけ。0 で終わる)、続いて
;   (reg, value) の組 ($FF で終わる)。
;   reg=$07 value=$BF (ミキサ全 mute + I/O OUT)、reg=$0F value=$3F (I/O port B)。
;   DP=$FD 前提。A・B・X 破壊。
;------------------------------------------------------------------------------
init_sound:
                CLRA
                STA     <FD_SUBCTL      ; インアクティブ
                LDX     #sound_table
                ; --- 段 1: プリスケーラ (レジスタ番号を指定するだけで選ばれる) ---
is_pre:
                LDA     ,X+             ; プリスケーラのレジスタ番号 (0 = 終端)
                BEQ     is_pair
                BSR     is_latch        ; アドレスラッチだけ (データは書かない)
                BRA     is_pre
                ; --- 段 2: (reg, value) の組 ---
is_pair:
                LDA     ,X+             ; reg 番号
                CMPA    #$FF
                BEQ     is_done
                BSR     is_latch        ; アドレスラッチ
                LDA     ,X+             ; value
                STA     <FD_SUBDAT      ; データ
                LDB     #$02
                STB     <FD_SUBCTL      ; ライトデータ
                CLR     <FD_SUBCTL      ; インアクティブ
                BRA     is_pair
is_done:
                RTS
; is_latch — A のレジスタ番号をラッチする (レジスタ番号 → $03 → $00)。B 破壊。
is_latch:
                STA     <FD_SUBDAT      ; レジスタ番号
                LDB     #$03
                STB     <FD_SUBCTL      ; ラッチアドレス
                CLR     <FD_SUBCTL      ; インアクティブ
                RTS

; ★ プリスケーラの設定は必ず先頭で行うこと。
;   参考書籍: $2D / $2E / $2F にはデータビットが
;   無く、レジスタ番号を指定してアクセスするだけで選ばれる。$2D と $2E をアクセスすると
;   FM 部 1/3・SSG 部 1/2 になり、FM-7 シリーズではこれが標準。
;   参考書籍は音階の分周値を f = f_clock / (16 × D)、f_clock = 1.2288MHz
;   と定めており、本機のデバイス供給クロックでこの f_clock を得るには SSG 部を
;   1/2 に置く必要がある。既定の 1/4 のままだと分周基準が半分になり、同じ D を
;   書いても全音が **1 オクターブ低く**鳴る。
;   この 2 バイトを消してはならない。
;   参考書籍: $21 はテスト用で触らない。$24〜$26 (タイマの周期) と $27 (タイマと
;   チャネル 3 のモード) は内部のタイマを使わなければ触らなくてよい。ここでは $24〜$26 を
;   書かず、$27 は bit5・bit4 (A / B のフラグのリセット) だけを立てる
;   (bit3・bit2 = 0: オーバフローを受け付けない、bit1・bit0 = 0: タイマ B・A は停止)。
;   $28 はキーオフをチャネル 1〜3 (bit1-0 = 00 / 01 / 10、bit7-4 = 0) に書く。
sound_table:
                FCB     $2D,$2E,$00     ; プリスケーラ: $2D → $2E (ラッチだけ) = FM 1/3・SSG 1/2
                FCB     $27,$30         ; タイマ A / B のフラグをリセット (ロード・割込許可は 0)
                FCB     $28,$00         ; キーオフ (チャネル 1)
                FCB     $28,$01         ; キーオフ (チャネル 2)
                FCB     $28,$02         ; キーオフ (チャネル 3)
                FCB     $0F,$3F         ; I/O port B 出力
                FCB     $07,$BF         ; ミキサ = 全 ABC mute + I/O OUT (PSG リセット定型)
                FCB     $FF

;------------------------------------------------------------------------------
; init_palette — アナログパレット LUT ($FD30-$FD34) を全 4096 エントリ初期化
;   index X 自身を RGB 各 nibble に分配して書く決定的初期化。DP=$FD 前提。
;   A,B,X 破壊。X = $0FFF から index 0 まで (0 も同じループで書く)。
;------------------------------------------------------------------------------
init_palette:
                LDX     #0              ; 索引 0 から昇順に 4096 エントリ
ip_loop:
                TFR     X,D
                STD     <FD_PALIDX_HI   ; 索引の上位 (A) と下位 (B) を同時に書く
                STA     <FD_PAL_G       ; G = 索引の bit11-8
                TFR     B,A
                ANDA    #$0F
                STA     <FD_PAL_B       ; B = 索引の bit3-0
                LSRB
                LSRB
                LSRB
                LSRB
                STB     <FD_PAL_R       ; R = 索引の bit7-4
                LEAX    1,X
                CMPX    #$1000
                BNE     ip_loop
                RTS

;------------------------------------------------------------------------------
; 機種識別域 $6B00-$6B13 (file offset $0B00-$0B13、20 byte)
;
;   この 20 byte は機種判別用の識別域で、ソフトウェアはここを読んで機種構成を
;   判定できる (動作観察)。
;
;   提示内容は当プロジェクト独自の識別子である。提示するのは「バージョン番号・世代・
;   機種区分」という情報の構造で、バイト列は独自に定義する。
;
;   レイアウト (当プロジェクト定義):
;     $6B00-$6B05  6 byte  "ALTROM"  プロジェクト識別子
;     $6B06-$6B08  3 byte  バージョン番号 3 桁 ASCII ("110" = 本 ROM バージョン番号 1.10)
;     $6B09-$6B0D  5 byte  世代名 5 桁 ASCII ("AV001"/"AV020"/"AV040"/"AV4EX")
;     $6B0E-$6B0F  2 byte  機種区分 2 桁 ASCII ("20"/"40")。持たない世代は $FF
;     $6B10        1 byte  拡張世代の識別値 '1' (STAGE_ALT_RAM は $00)。
;                          他の世代は $00、機種区分を持たない世代は $FF
;     $6B11-$6B13  3 byte  予備。既定 $00 (機種区分を持たない世代は $FF)
;
;   ★ $6B0E-$6B13 の 6 byte は、機種構成に応じて外から書き換えられ得る領域と
;     して空けておく。コードやデータを置いてはならない。本 ROM 自身はこの領域を
;     参照しない (機種分岐は全て実行時のレジスタ判定で行う)。
;
;   機種区分 2 桁の意味 (動作観察):
;     拡張表示モード (26 万色) を備える世代は "40" を提示する。読む側はこの提示で
;     26 万色モードの可否を判定する。
;     中位世代は "20" を提示する (26 万色モードは提供されない)。第 1 世代は
;     機種区分そのものを持たない世代なので全 byte $FF を置く。
;     搭載しない拡張表示モードを提示すると、未搭載のモードが選べてしまい表示が破綻する。
;
;   ROM のバイト列はそのままソフトウェアから見えるので、提示内容は機種ごとに正しく
;   作り分ける。
;
;   条件アセンブルによる作り分け:
;     - 既定 (未定義) : initiate.rom = "AV040" / 機種区分 "40"
;     - INIT_BASE     : initbase.rom = "AV020" / 機種区分 "20"
;     - INIT_AV1      : initav1.rom  = "AV001" / 機種区分なし ($FF x 6)
;     - INIT_EXSX     : initex.rom   = "AV4EX" / 機種区分 "40"
;
;   ★ 拡張世代 (INIT_EXSX) の機種区分は上位世代と同じ "40" である。拡張世代は
;     上位世代の拡張表示モード (26 万色) を備える構成なので、ここを "40" 以外に
;     すると 26 万色モードが不可と判定される。世代の違いは
;     $6B09-$6B0D の世代名 ("AV4EX") で表す。この 2 byte は動作観察で依存が
;     確かめられている唯一の位置であり、値を変えてはならない。
;------------------------------------------------------------------------------
                ZMB     $6B00-*         ; パディング
rom_id:         FCC     "ALTROM"                ; $6B00-$6B05 プロジェクト識別子
                FCC     "110"                   ; $6B06-$6B08 バージョン番号 3 桁 (1.10)
                IFDEF   INIT_AV1
                FCC     "AV001"                 ; $6B09-$6B0D 世代 (第 1 世代)
ver_field:      FCB     $FF,$FF,$FF,$FF,$FF,$FF ; $6B0E-$6B13 機種区分を持たない
                ELSE
                IFDEF   INIT_BASE
                FCC     "AV020"                 ; $6B09-$6B0D 世代 (中位)
ver_field:      FCC     "20"                    ; $6B0E-$6B0F 機種区分 (ASCII)
                FCB     $00,$00,$00,$00         ; $6B10-$6B13 予備
                ELSE
                IFDEF   INIT_EXSX
                FCC     "AV4EX"                 ; $6B09-$6B0D 世代 (拡張)
ver_field:      FCC     "40"                    ; $6B0E-$6B0F 機種区分 (ASCII、上位と同値)
                IFNDEF  STAGE_ALT_RAM
                FCB     $31                    ; $6B10 拡張世代の識別値 '1'
                ELSE
                FCB     $00
                ENDC
                FCB     $00,$00,$00             ; $6B11-$6B13 予備
                ELSE
                FCC     "AV040"                 ; $6B09-$6B0D 世代 (上位)
ver_field:      FCC     "40"                    ; $6B0E-$6B0F 機種区分 (ASCII)
                FCB     $00,$00,$00,$00         ; $6B10-$6B13 予備
                ENDC
                ENDC
                ENDC

;------------------------------------------------------------------------------
; voice_table — FM 音源の音色表 ($6C00-$7639、34 バイト × 77、音色番号 0〜76)
;   位置と個数と 1 エントリの長さは参考書籍 ($6C00 から
;   77 エントリ × 34 バイト、$763A から予備) のとおり。1 エントリの形式:
;     +0〜+3   DT/MUL × 4   +4〜+7   TL × 4      +8〜+11  KS/AR × 4
;     +12〜+15 AM/DR × 4    +16〜+19 SR × 4      +20〜+23 SL/RR × 4
;     +24      FB/ALG (フィードバックと接続)
;     +25 Flags / +26 Key Scale Depth / +27 PMS・AMS / +28 AMD / +29 PMD /
;     +30 LFO frequency / +31 Delay Time / +32 Wave Form / +33 Sync Flag
;   +0〜+24 の 25 バイトが音源のレジスタへ送られる値、+25〜+33 の 9 バイトは LFO
;   関係の値で、当方の表は 9 バイトを全て 0 (使わない) に置く (値は設計)。
;   4 個の値の並びはレジスタのオフセット順 (スロット 1 / 3 / 2 / 4 = 0 / 4 / 8 / C)。
;   演奏の前に $FD10 でイニシエート ROM を $6000-$7FFF に重ね直し、$6C00 + 34 × 音色番号
;   から 25 バイトを読んで音源のレジスタ $30〜$8C と $B0 に書く使い方ができる
;   (位置・長さ・並びは上の記載のとおり)。この位置が空だと音色が全て 0 (AR = 0) になり
;   鳴らない。音色の内容 (各パラメータの値と音色名) は本プロジェクトが独自に作った。
;------------------------------------------------------------------------------
VOICE_LEN       EQU     34              ; 1 音色の長さ (byte)
                ZMB     $6C00-*         ; 音色表の位置
voice_table:
                ; 音色  0: epiano    (ALG 5, FB 6)
                FCB     $01,$02,$01,$03  ; DT/MUL
                FCB     $28,$0C,$0A,$0E  ; TL
                FCB     $5F,$5F,$5F,$5F  ; KS/AR
                FCB     $0A,$08,$08,$09  ; AM/DR
                FCB     $02,$02,$02,$02  ; SR
                FCB     $26,$27,$27,$37  ; SL/RR
                FCB     $35  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色  1: lead      (ALG 5, FB 5)
                FCB     $32,$02,$01,$24  ; DT/MUL
                FCB     $24,$09,$08,$14  ; TL
                FCB     $5F,$5F,$5F,$5F  ; KS/AR
                FCB     $08,$06,$06,$07  ; AM/DR
                FCB     $03,$01,$01,$01  ; SR
                FCB     $38,$18,$18,$28  ; SL/RR
                FCB     $2D  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色  2: brass     (ALG 4, FB 5)
                FCB     $01,$02,$01,$01  ; DT/MUL
                FCB     $20,$1E,$0A,$0A  ; TL
                FCB     $54,$54,$56,$56  ; KS/AR
                FCB     $06,$06,$04,$04  ; AM/DR
                FCB     $02,$02,$01,$01  ; SR
                FCB     $17,$17,$07,$07  ; SL/RR
                FCB     $2C  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色  3: horn      (ALG 4, FB 4)
                FCB     $01,$01,$01,$02  ; DT/MUL
                FCB     $26,$24,$0C,$0E  ; TL
                FCB     $50,$50,$52,$52  ; KS/AR
                FCB     $05,$05,$04,$04  ; AM/DR
                FCB     $01,$01,$01,$01  ; SR
                FCB     $16,$16,$06,$06  ; SL/RR
                FCB     $24  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色  4: trumpet   (ALG 2, FB 6)
                FCB     $01,$01,$03,$01  ; DT/MUL
                FCB     $1E,$1C,$22,$0A  ; TL
                FCB     $58,$58,$58,$58  ; KS/AR
                FCB     $07,$06,$07,$05  ; AM/DR
                FCB     $02,$02,$02,$01  ; SR
                FCB     $18,$18,$18,$08  ; SL/RR
                FCB     $32  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色  5: clarinet  (ALG 1, FB 3)
                FCB     $03,$02,$01,$01  ; DT/MUL
                FCB     $28,$24,$2C,$0A  ; TL
                FCB     $1C,$1C,$1C,$1C  ; KS/AR
                FCB     $05,$05,$05,$04  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $17,$17,$17,$07  ; SL/RR
                FCB     $19  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色  6: organ     (ALG 7, FB 0)
                FCB     $01,$04,$02,$08  ; DT/MUL
                FCB     $0C,$12,$0E,$18  ; TL
                FCB     $1F,$1F,$1F,$1F  ; KS/AR
                FCB     $00,$00,$00,$00  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $09,$09,$09,$09  ; SL/RR
                FCB     $07  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色  7: reedorg   (ALG 7, FB 2)
                FCB     $01,$02,$03,$06  ; DT/MUL
                FCB     $0A,$10,$14,$1C  ; TL
                FCB     $1F,$1F,$1F,$1F  ; KS/AR
                FCB     $02,$02,$02,$02  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $08,$08,$08,$08  ; SL/RR
                FCB     $17  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色  8: flute     (ALG 1, FB 2)
                FCB     $01,$01,$02,$01  ; DT/MUL
                FCB     $30,$28,$32,$0A  ; TL
                FCB     $14,$14,$14,$14  ; KS/AR
                FCB     $04,$04,$04,$03  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $07,$07,$07,$07  ; SL/RR
                FCB     $11  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色  9: oboe      (ALG 3, FB 5)
                FCB     $02,$01,$03,$01  ; DT/MUL
                FCB     $22,$24,$28,$0A  ; TL
                FCB     $5A,$5A,$5A,$5A  ; KS/AR
                FCB     $06,$06,$06,$04  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $17,$17,$17,$07  ; SL/RR
                FCB     $2B  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 10: strings   (ALG 2, FB 6)
                FCB     $21,$61,$02,$01  ; DT/MUL
                FCB     $24,$22,$2A,$0A  ; TL
                FCB     $0E,$0E,$0E,$0C  ; KS/AR
                FCB     $04,$04,$04,$03  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $16,$16,$16,$06  ; SL/RR
                FCB     $32  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 11: cello     (ALG 2, FB 5)
                FCB     $01,$01,$01,$01  ; DT/MUL
                FCB     $26,$24,$28,$0A  ; TL
                FCB     $0C,$0C,$0C,$0C  ; KS/AR
                FCB     $04,$04,$04,$03  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $16,$16,$16,$06  ; SL/RR
                FCB     $2A  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 12: harp      (ALG 5, FB 3)
                FCB     $02,$03,$01,$05  ; DT/MUL
                FCB     $2A,$10,$0A,$16  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0E,$0C,$0C,$0D  ; AM/DR
                FCB     $04,$03,$03,$03  ; SR
                FCB     $37,$38,$38,$48  ; SL/RR
                FCB     $1D  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 13: guitar    (ALG 4, FB 6)
                FCB     $03,$02,$01,$01  ; DT/MUL
                FCB     $24,$22,$0A,$0C  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0C,$0C,$0A,$0A  ; AM/DR
                FCB     $03,$03,$02,$02  ; SR
                FCB     $28,$28,$28,$28  ; SL/RR
                FCB     $34  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 14: bass      (ALG 0, FB 4)
                FCB     $01,$01,$01,$01  ; DT/MUL
                FCB     $1E,$20,$22,$08  ; TL
                FCB     $5F,$5F,$5F,$5F  ; KS/AR
                FCB     $0A,$0A,$0A,$08  ; AM/DR
                FCB     $03,$03,$03,$02  ; SR
                FCB     $28,$28,$28,$18  ; SL/RR
                FCB     $20  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 15: slapbass  (ALG 5, FB 7)
                FCB     $04,$01,$01,$02  ; DT/MUL
                FCB     $2C,$0C,$08,$12  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $10,$0A,$0A,$0B  ; AM/DR
                FCB     $04,$03,$03,$03  ; SR
                FCB     $49,$29,$29,$39  ; SL/RR
                FCB     $3D  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 16: bell      (ALG 4, FB 2)
                FCB     $03,$37,$01,$02  ; DT/MUL
                FCB     $1E,$22,$0C,$10  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0A,$0B,$08,$08  ; AM/DR
                FCB     $02,$02,$01,$01  ; SR
                FCB     $37,$37,$26,$26  ; SL/RR
                FCB     $14  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 17: vibes     (ALG 5, FB 3)
                FCB     $04,$01,$01,$02  ; DT/MUL
                FCB     $26,$0C,$0A,$14  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0C,$09,$09,$0A  ; AM/DR
                FCB     $03,$02,$02,$02  ; SR
                FCB     $38,$28,$28,$38  ; SL/RR
                FCB     $1D  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 18: marimba   (ALG 5, FB 4)
                FCB     $05,$02,$01,$03  ; DT/MUL
                FCB     $2E,$0E,$0A,$16  ; TL
                FCB     $DF,$DF,$DF,$DF  ; KS/AR
                FCB     $12,$0E,$0E,$0F  ; AM/DR
                FCB     $06,$05,$05,$05  ; SR
                FCB     $5A,$4A,$4A,$5A  ; SL/RR
                FCB     $25  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 19: xylo      (ALG 5, FB 5)
                FCB     $07,$03,$01,$04  ; DT/MUL
                FCB     $30,$0E,$08,$18  ; TL
                FCB     $DF,$DF,$DF,$DF  ; KS/AR
                FCB     $14,$10,$10,$11  ; AM/DR
                FCB     $07,$06,$06,$06  ; SR
                FCB     $6B,$5B,$5B,$5B  ; SL/RR
                FCB     $2D  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 20: synlead   (ALG 7, FB 6)
                FCB     $01,$72,$11,$04  ; DT/MUL
                FCB     $08,$10,$0C,$1A  ; TL
                FCB     $5F,$5F,$5F,$5F  ; KS/AR
                FCB     $04,$04,$04,$04  ; AM/DR
                FCB     $01,$01,$01,$01  ; SR
                FCB     $08,$08,$08,$08  ; SL/RR
                FCB     $37  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 21: synbrass  (ALG 3, FB 6)
                FCB     $01,$01,$02,$01  ; DT/MUL
                FCB     $20,$1C,$1E,$08  ; TL
                FCB     $56,$56,$56,$58  ; KS/AR
                FCB     $06,$06,$06,$04  ; AM/DR
                FCB     $02,$02,$02,$01  ; SR
                FCB     $17,$17,$17,$07  ; SL/RR
                FCB     $33  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 22: synbass   (ALG 0, FB 6)
                FCB     $02,$01,$01,$01  ; DT/MUL
                FCB     $1C,$1A,$1E,$08  ; TL
                FCB     $5F,$5F,$5F,$5F  ; KS/AR
                FCB     $0C,$0C,$0C,$0A  ; AM/DR
                FCB     $04,$04,$04,$03  ; SR
                FCB     $39,$39,$39,$29  ; SL/RR
                FCB     $30  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 23: square    (ALG 7, FB 0)
                FCB     $01,$05,$03,$07  ; DT/MUL
                FCB     $0A,$1A,$14,$1E  ; TL
                FCB     $1F,$1F,$1F,$1F  ; KS/AR
                FCB     $00,$00,$00,$00  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $08,$08,$08,$08  ; SL/RR
                FCB     $07  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 24: chime     (ALG 1, FB 5)
                FCB     $03,$01,$07,$02  ; DT/MUL
                FCB     $1E,$20,$22,$0A  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0C,$0C,$0C,$09  ; AM/DR
                FCB     $02,$02,$02,$01  ; SR
                FCB     $37,$37,$37,$27  ; SL/RR
                FCB     $29  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 25: harpsi    (ALG 4, FB 7)
                FCB     $02,$04,$01,$01  ; DT/MUL
                FCB     $20,$24,$0A,$0E  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0C,$0C,$0A,$0A  ; AM/DR
                FCB     $03,$03,$02,$02  ; SR
                FCB     $28,$28,$28,$28  ; SL/RR
                FCB     $3C  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 26: pluck     (ALG 6, FB 4)
                FCB     $03,$02,$01,$01  ; DT/MUL
                FCB     $22,$0E,$0A,$0C  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0E,$0C,$0C,$0C  ; AM/DR
                FCB     $04,$03,$03,$03  ; SR
                FCB     $39,$39,$39,$39  ; SL/RR
                FCB     $26  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 27: koto      (ALG 5, FB 6)
                FCB     $03,$02,$01,$04  ; DT/MUL
                FCB     $28,$0E,$0A,$18  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0F,$0B,$0B,$0C  ; AM/DR
                FCB     $05,$04,$04,$04  ; SR
                FCB     $49,$39,$39,$49  ; SL/RR
                FCB     $35  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 28: drum      (ALG 7, FB 7)
                FCB     $01,$03,$02,$05  ; DT/MUL
                FCB     $0A,$1A,$14,$1E  ; TL
                FCB     $1F,$1F,$1F,$1F  ; KS/AR
                FCB     $12,$12,$12,$12  ; AM/DR
                FCB     $0A,$0A,$0A,$0A  ; SR
                FCB     $6C,$6C,$6C,$6C  ; SL/RR
                FCB     $3F  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 29: noise     (ALG 1, FB 7)
                FCB     $0F,$0B,$0D,$01  ; DT/MUL
                FCB     $14,$1A,$18,$0A  ; TL
                FCB     $1F,$1F,$1F,$1F  ; KS/AR
                FCB     $0E,$0E,$0E,$0C  ; AM/DR
                FCB     $06,$06,$06,$05  ; SR
                FCB     $4A,$4A,$4A,$3A  ; SL/RR
                FCB     $39  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 30: epianob   (ALG 5, FB 1)
                FCB     $01,$02,$01,$04  ; DT/MUL
                FCB     $22,$0C,$0A,$0E  ; TL
                FCB     $5F,$5F,$5F,$5F  ; KS/AR
                FCB     $0C,$0A,$0A,$0B  ; AM/DR
                FCB     $02,$02,$02,$02  ; SR
                FCB     $26,$27,$27,$37  ; SL/RR
                FCB     $0D  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 31: leadb     (ALG 5, FB 0)
                FCB     $32,$02,$01,$25  ; DT/MUL
                FCB     $1E,$09,$08,$0E  ; TL
                FCB     $5F,$5F,$5F,$5F  ; KS/AR
                FCB     $0A,$08,$08,$09  ; AM/DR
                FCB     $03,$01,$01,$01  ; SR
                FCB     $38,$18,$18,$28  ; SL/RR
                FCB     $05  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 32: brassb    (ALG 4, FB 0)
                FCB     $01,$02,$01,$02  ; DT/MUL
                FCB     $1A,$18,$0A,$0A  ; TL
                FCB     $54,$54,$56,$56  ; KS/AR
                FCB     $08,$08,$06,$06  ; AM/DR
                FCB     $02,$02,$01,$01  ; SR
                FCB     $17,$17,$07,$07  ; SL/RR
                FCB     $04  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 33: hornb     (ALG 4, FB 7)
                FCB     $01,$01,$01,$03  ; DT/MUL
                FCB     $20,$1E,$0C,$0E  ; TL
                FCB     $50,$50,$52,$52  ; KS/AR
                FCB     $07,$07,$06,$06  ; AM/DR
                FCB     $01,$01,$01,$01  ; SR
                FCB     $16,$16,$06,$06  ; SL/RR
                FCB     $3C  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 34: trumpetb  (ALG 2, FB 1)
                FCB     $01,$01,$03,$02  ; DT/MUL
                FCB     $18,$16,$1C,$0A  ; TL
                FCB     $58,$58,$58,$58  ; KS/AR
                FCB     $09,$08,$09,$07  ; AM/DR
                FCB     $02,$02,$02,$01  ; SR
                FCB     $18,$18,$18,$08  ; SL/RR
                FCB     $0A  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 35: clarinetb (ALG 1, FB 6)
                FCB     $03,$02,$01,$02  ; DT/MUL
                FCB     $22,$1E,$26,$0A  ; TL
                FCB     $1C,$1C,$1C,$1C  ; KS/AR
                FCB     $07,$07,$07,$06  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $17,$17,$17,$07  ; SL/RR
                FCB     $31  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 36: organb    (ALG 7, FB 3)
                FCB     $01,$04,$02,$09  ; DT/MUL
                FCB     $0C,$12,$0E,$12  ; TL
                FCB     $1F,$1F,$1F,$1F  ; KS/AR
                FCB     $02,$02,$02,$02  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $09,$09,$09,$09  ; SL/RR
                FCB     $1F  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 37: reedorgb  (ALG 7, FB 5)
                FCB     $01,$02,$03,$07  ; DT/MUL
                FCB     $0A,$10,$0E,$16  ; TL
                FCB     $1F,$1F,$1F,$1F  ; KS/AR
                FCB     $04,$04,$04,$04  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $08,$08,$08,$08  ; SL/RR
                FCB     $2F  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 38: fluteb    (ALG 1, FB 5)
                FCB     $01,$01,$02,$02  ; DT/MUL
                FCB     $2A,$22,$2C,$0A  ; TL
                FCB     $14,$14,$14,$14  ; KS/AR
                FCB     $06,$06,$06,$05  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $07,$07,$07,$07  ; SL/RR
                FCB     $29  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 39: oboeb     (ALG 3, FB 0)
                FCB     $02,$01,$03,$02  ; DT/MUL
                FCB     $1C,$1E,$22,$0A  ; TL
                FCB     $5A,$5A,$5A,$5A  ; KS/AR
                FCB     $08,$08,$08,$06  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $17,$17,$17,$07  ; SL/RR
                FCB     $03  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 40: stringsb  (ALG 2, FB 1)
                FCB     $21,$61,$02,$02  ; DT/MUL
                FCB     $1E,$1C,$24,$0A  ; TL
                FCB     $0E,$0E,$0E,$0C  ; KS/AR
                FCB     $06,$06,$06,$05  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $16,$16,$16,$06  ; SL/RR
                FCB     $0A  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 41: cellob    (ALG 2, FB 0)
                FCB     $01,$01,$01,$02  ; DT/MUL
                FCB     $20,$1E,$22,$0A  ; TL
                FCB     $0C,$0C,$0C,$0C  ; KS/AR
                FCB     $06,$06,$06,$05  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $16,$16,$16,$06  ; SL/RR
                FCB     $02  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 42: harpb     (ALG 5, FB 6)
                FCB     $02,$03,$01,$06  ; DT/MUL
                FCB     $24,$10,$0A,$10  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $10,$0E,$0E,$0F  ; AM/DR
                FCB     $04,$03,$03,$03  ; SR
                FCB     $37,$38,$38,$48  ; SL/RR
                FCB     $35  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 43: guitarb   (ALG 4, FB 1)
                FCB     $03,$02,$01,$02  ; DT/MUL
                FCB     $1E,$1C,$0A,$0C  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0E,$0E,$0C,$0C  ; AM/DR
                FCB     $03,$03,$02,$02  ; SR
                FCB     $28,$28,$28,$28  ; SL/RR
                FCB     $0C  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 44: bassb     (ALG 0, FB 7)
                FCB     $01,$01,$01,$02  ; DT/MUL
                FCB     $18,$1A,$1C,$08  ; TL
                FCB     $5F,$5F,$5F,$5F  ; KS/AR
                FCB     $0C,$0C,$0C,$0A  ; AM/DR
                FCB     $03,$03,$03,$02  ; SR
                FCB     $28,$28,$28,$18  ; SL/RR
                FCB     $38  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 45: slapbassb (ALG 5, FB 2)
                FCB     $04,$01,$01,$03  ; DT/MUL
                FCB     $26,$0C,$08,$12  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $12,$0C,$0C,$0D  ; AM/DR
                FCB     $04,$03,$03,$03  ; SR
                FCB     $49,$29,$29,$39  ; SL/RR
                FCB     $15  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 46: bellb     (ALG 4, FB 5)
                FCB     $03,$37,$01,$03  ; DT/MUL
                FCB     $18,$1C,$0C,$10  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0C,$0D,$0A,$0A  ; AM/DR
                FCB     $02,$02,$01,$01  ; SR
                FCB     $37,$37,$26,$26  ; SL/RR
                FCB     $2C  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 47: vibesb    (ALG 5, FB 6)
                FCB     $04,$01,$01,$03  ; DT/MUL
                FCB     $20,$0C,$0A,$0E  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0E,$0B,$0B,$0C  ; AM/DR
                FCB     $03,$02,$02,$02  ; SR
                FCB     $38,$28,$28,$38  ; SL/RR
                FCB     $35  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 48: marimbab  (ALG 5, FB 7)
                FCB     $05,$02,$01,$04  ; DT/MUL
                FCB     $28,$0E,$0A,$10  ; TL
                FCB     $DF,$DF,$DF,$DF  ; KS/AR
                FCB     $14,$10,$10,$11  ; AM/DR
                FCB     $06,$05,$05,$05  ; SR
                FCB     $5A,$4A,$4A,$5A  ; SL/RR
                FCB     $3D  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 49: xylob     (ALG 5, FB 0)
                FCB     $07,$03,$01,$05  ; DT/MUL
                FCB     $2A,$0E,$08,$12  ; TL
                FCB     $DF,$DF,$DF,$DF  ; KS/AR
                FCB     $16,$12,$12,$13  ; AM/DR
                FCB     $07,$06,$06,$06  ; SR
                FCB     $6B,$5B,$5B,$5B  ; SL/RR
                FCB     $05  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 50: synleadb  (ALG 7, FB 1)
                FCB     $01,$72,$11,$05  ; DT/MUL
                FCB     $08,$10,$0C,$14  ; TL
                FCB     $5F,$5F,$5F,$5F  ; KS/AR
                FCB     $06,$06,$06,$06  ; AM/DR
                FCB     $01,$01,$01,$01  ; SR
                FCB     $08,$08,$08,$08  ; SL/RR
                FCB     $0F  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 51: synbrassb (ALG 3, FB 1)
                FCB     $01,$01,$02,$02  ; DT/MUL
                FCB     $1A,$16,$18,$08  ; TL
                FCB     $56,$56,$56,$58  ; KS/AR
                FCB     $08,$08,$08,$06  ; AM/DR
                FCB     $02,$02,$02,$01  ; SR
                FCB     $17,$17,$17,$07  ; SL/RR
                FCB     $0B  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 52: synbassb  (ALG 0, FB 1)
                FCB     $02,$01,$01,$02  ; DT/MUL
                FCB     $16,$14,$18,$08  ; TL
                FCB     $5F,$5F,$5F,$5F  ; KS/AR
                FCB     $0E,$0E,$0E,$0C  ; AM/DR
                FCB     $04,$04,$04,$03  ; SR
                FCB     $39,$39,$39,$29  ; SL/RR
                FCB     $08  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 53: squareb   (ALG 7, FB 3)
                FCB     $01,$05,$03,$08  ; DT/MUL
                FCB     $0A,$14,$0E,$18  ; TL
                FCB     $1F,$1F,$1F,$1F  ; KS/AR
                FCB     $02,$02,$02,$02  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $08,$08,$08,$08  ; SL/RR
                FCB     $1F  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 54: chimeb    (ALG 1, FB 0)
                FCB     $03,$01,$07,$03  ; DT/MUL
                FCB     $18,$1A,$1C,$0A  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0E,$0E,$0E,$0B  ; AM/DR
                FCB     $02,$02,$02,$01  ; SR
                FCB     $37,$37,$37,$27  ; SL/RR
                FCB     $01  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 55: harpsib   (ALG 4, FB 2)
                FCB     $02,$04,$01,$02  ; DT/MUL
                FCB     $1A,$1E,$0A,$0E  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $0E,$0E,$0C,$0C  ; AM/DR
                FCB     $03,$03,$02,$02  ; SR
                FCB     $28,$28,$28,$28  ; SL/RR
                FCB     $14  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 56: pluckb    (ALG 6, FB 7)
                FCB     $03,$02,$01,$02  ; DT/MUL
                FCB     $1C,$0E,$0A,$0C  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $10,$0E,$0E,$0E  ; AM/DR
                FCB     $04,$03,$03,$03  ; SR
                FCB     $39,$39,$39,$39  ; SL/RR
                FCB     $3E  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 57: kotob     (ALG 5, FB 1)
                FCB     $03,$02,$01,$05  ; DT/MUL
                FCB     $22,$0E,$0A,$12  ; TL
                FCB     $9F,$9F,$9F,$9F  ; KS/AR
                FCB     $11,$0D,$0D,$0E  ; AM/DR
                FCB     $05,$04,$04,$04  ; SR
                FCB     $49,$39,$39,$49  ; SL/RR
                FCB     $0D  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 58: drumb     (ALG 7, FB 2)
                FCB     $01,$03,$02,$06  ; DT/MUL
                FCB     $0A,$14,$0E,$18  ; TL
                FCB     $1F,$1F,$1F,$1F  ; KS/AR
                FCB     $14,$14,$14,$14  ; AM/DR
                FCB     $0A,$0A,$0A,$0A  ; SR
                FCB     $6C,$6C,$6C,$6C  ; SL/RR
                FCB     $17  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 59: noiseb    (ALG 1, FB 2)
                FCB     $0F,$0B,$0D,$02  ; DT/MUL
                FCB     $0E,$14,$12,$0A  ; TL
                FCB     $1F,$1F,$1F,$1F  ; KS/AR
                FCB     $10,$10,$10,$0E  ; AM/DR
                FCB     $06,$06,$06,$05  ; SR
                FCB     $4A,$4A,$4A,$3A  ; SL/RR
                FCB     $11  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 60: epianoc   (ALG 5, FB 3)
                FCB     $31,$02,$01,$03  ; DT/MUL
                FCB     $2C,$10,$0E,$12  ; TL
                FCB     $59,$59,$59,$59  ; KS/AR
                FCB     $0B,$09,$09,$0A  ; AM/DR
                FCB     $02,$02,$02,$02  ; SR
                FCB     $34,$35,$35,$45  ; SL/RR
                FCB     $1D  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 61: leadc     (ALG 5, FB 2)
                FCB     $32,$02,$01,$24  ; DT/MUL
                FCB     $28,$0D,$0C,$18  ; TL
                FCB     $59,$59,$59,$59  ; KS/AR
                FCB     $09,$07,$07,$08  ; AM/DR
                FCB     $03,$01,$01,$01  ; SR
                FCB     $46,$26,$26,$36  ; SL/RR
                FCB     $15  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 62: brassc    (ALG 4, FB 2)
                FCB     $31,$02,$01,$01  ; DT/MUL
                FCB     $24,$22,$0E,$0E  ; TL
                FCB     $4E,$4E,$50,$50  ; KS/AR
                FCB     $07,$07,$05,$05  ; AM/DR
                FCB     $02,$02,$01,$01  ; SR
                FCB     $25,$25,$15,$15  ; SL/RR
                FCB     $14  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 63: hornc     (ALG 4, FB 1)
                FCB     $31,$01,$01,$02  ; DT/MUL
                FCB     $2A,$28,$10,$12  ; TL
                FCB     $4A,$4A,$4C,$4C  ; KS/AR
                FCB     $06,$06,$05,$05  ; AM/DR
                FCB     $01,$01,$01,$01  ; SR
                FCB     $24,$24,$14,$14  ; SL/RR
                FCB     $0C  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 64: trumpetc  (ALG 2, FB 3)
                FCB     $31,$01,$03,$01  ; DT/MUL
                FCB     $22,$20,$26,$0E  ; TL
                FCB     $52,$52,$52,$52  ; KS/AR
                FCB     $08,$07,$08,$06  ; AM/DR
                FCB     $02,$02,$02,$01  ; SR
                FCB     $26,$26,$26,$16  ; SL/RR
                FCB     $1A  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 65: clarinetc (ALG 1, FB 0)
                FCB     $33,$02,$01,$01  ; DT/MUL
                FCB     $2C,$28,$30,$0E  ; TL
                FCB     $16,$16,$16,$16  ; KS/AR
                FCB     $06,$06,$06,$05  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $25,$25,$25,$15  ; SL/RR
                FCB     $01  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 66: organc    (ALG 7, FB 5)
                FCB     $31,$04,$02,$08  ; DT/MUL
                FCB     $10,$16,$12,$1C  ; TL
                FCB     $19,$19,$19,$19  ; KS/AR
                FCB     $01,$01,$01,$01  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $17,$17,$17,$17  ; SL/RR
                FCB     $2F  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 67: reedorgc  (ALG 7, FB 7)
                FCB     $31,$02,$03,$06  ; DT/MUL
                FCB     $0E,$14,$18,$20  ; TL
                FCB     $19,$19,$19,$19  ; KS/AR
                FCB     $03,$03,$03,$03  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $16,$16,$16,$16  ; SL/RR
                FCB     $3F  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 68: flutec    (ALG 1, FB 7)
                FCB     $31,$01,$02,$01  ; DT/MUL
                FCB     $34,$2C,$36,$0E  ; TL
                FCB     $0E,$0E,$0E,$0E  ; KS/AR
                FCB     $05,$05,$05,$04  ; AM/DR
                FCB     $00,$00,$00,$00  ; SR
                FCB     $15,$15,$15,$15  ; SL/RR
                FCB     $39  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 69: oboec     (ALG 3, FB 2)
                FCB     $32,$01,$03,$01  ; DT/MUL
                FCB     $26,$28,$2C,$0E  ; TL
                FCB     $54,$54,$54,$54  ; KS/AR
                FCB     $07,$07,$07,$05  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $25,$25,$25,$15  ; SL/RR
                FCB     $13  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 70: stringsc  (ALG 2, FB 3)
                FCB     $31,$61,$02,$01  ; DT/MUL
                FCB     $28,$26,$2E,$0E  ; TL
                FCB     $08,$08,$08,$08  ; KS/AR
                FCB     $05,$05,$05,$04  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $24,$24,$24,$14  ; SL/RR
                FCB     $1A  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 71: celloc    (ALG 2, FB 2)
                FCB     $31,$01,$01,$01  ; DT/MUL
                FCB     $2A,$28,$2C,$0E  ; TL
                FCB     $08,$08,$08,$08  ; KS/AR
                FCB     $05,$05,$05,$04  ; AM/DR
                FCB     $01,$01,$01,$00  ; SR
                FCB     $24,$24,$24,$14  ; SL/RR
                FCB     $12  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 72: harpc     (ALG 5, FB 0)
                FCB     $32,$03,$01,$05  ; DT/MUL
                FCB     $2E,$14,$0E,$1A  ; TL
                FCB     $99,$99,$99,$99  ; KS/AR
                FCB     $0F,$0D,$0D,$0E  ; AM/DR
                FCB     $04,$03,$03,$03  ; SR
                FCB     $45,$46,$46,$56  ; SL/RR
                FCB     $05  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 73: guitarc   (ALG 4, FB 3)
                FCB     $33,$02,$01,$01  ; DT/MUL
                FCB     $28,$26,$0E,$10  ; TL
                FCB     $99,$99,$99,$99  ; KS/AR
                FCB     $0D,$0D,$0B,$0B  ; AM/DR
                FCB     $03,$03,$02,$02  ; SR
                FCB     $36,$36,$36,$36  ; SL/RR
                FCB     $1C  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 74: bassc     (ALG 0, FB 1)
                FCB     $31,$01,$01,$01  ; DT/MUL
                FCB     $22,$24,$26,$0C  ; TL
                FCB     $59,$59,$59,$59  ; KS/AR
                FCB     $0B,$0B,$0B,$09  ; AM/DR
                FCB     $03,$03,$03,$02  ; SR
                FCB     $36,$36,$36,$26  ; SL/RR
                FCB     $08  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 75: slapbassc (ALG 5, FB 4)
                FCB     $34,$01,$01,$02  ; DT/MUL
                FCB     $30,$10,$0C,$16  ; TL
                FCB     $99,$99,$99,$99  ; KS/AR
                FCB     $11,$0B,$0B,$0C  ; AM/DR
                FCB     $04,$03,$03,$03  ; SR
                FCB     $57,$37,$37,$47  ; SL/RR
                FCB     $25  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)
                ; 音色 76: bellc     (ALG 4, FB 7)
                FCB     $33,$37,$01,$02  ; DT/MUL
                FCB     $22,$26,$10,$14  ; TL
                FCB     $99,$99,$99,$99  ; KS/AR
                FCB     $0B,$0C,$09,$09  ; AM/DR
                FCB     $02,$02,$01,$01  ; SR
                FCB     $45,$45,$34,$34  ; SL/RR
                FCB     $3C  ; FB/ALG
                ZMB     VOICE_LEN-25   ; +25〜+33 LFO 関係の 9 欄 (欄名は表の冒頭。値は 0)

;------------------------------------------------------------------------------
; bas_image — BASIC モード resident boot イメージ (512 byte)
;   本プロジェクトの独立ビルド boot_bas.rom (= 自作 $FE00-$FFFF イメージ) を埋め込む。
;   BASIC モード起動時に $FE00-$FFFF へ転送され、F-BASIC 起動 (ディスク自動起動
;   対応) を担う。
;
;   $7800-$79FF (file offset $1800) に配置する。
;   FM-7 互換モードで起動する実行環境は、この位置から 480 バイトを $FE00 へ写して
;   起動する (動作観察に基づく)。転送はラベル経由 (boot_handoff の LDU #bas_image)
;   で、イメージは $FE00 基底で組まれているので転送先 $FE00 で正しく解決される。
;------------------------------------------------------------------------------
                IFNDEF  STAGE_ALT_RAM
                ZMB     $7800-*         ; BASIC モードの起動イメージの位置
                ENDC
bas_image:      INCLUDEBIN "../build/boot_bas.rom"

;------------------------------------------------------------------------------
; boot_image — DOS モード resident boot イメージ (512 byte)
;   本プロジェクトの独立ビルド boot_init.rom (= 自作 $FE00-$FFFF イメージ) を埋め込む。
;   DOS (FDC ディスク) 起動時に $FE00-$FFFF へ転送され、resident boot として
;   ディスク IPL を起動する。割込ベクタ ($FFF2-$FFFF) と FDC ブートを担う。
;
;   このイメージはセクタ R/W 実体ルーチン (FDC レジスタ $FD18-$FD1F を直接駆動
;   して RESTORE / SEEK / READ-SECTOR を実行) を持ち、当方の AV ブートイメージ・BASIC が
;   内部の取り決めの入口 (BOOTSVC_RW) から呼ぶ。引数 X = パラメータブロック
;   (+0 種別 / +2,+3 転送先 / +4 track / +5 sector / +6 side / +7 drive)。
;
;   $7A00-$7BFF (file offset $1A00) に配置する。
;   転送後は $FE00-$FFFF で動作する。
;------------------------------------------------------------------------------
                IFNDEF  STAGE_ALT_RAM
                ZMB     $7A00-*         ; DOS モードの起動イメージの位置
                ENDC
boot_image:     INCLUDEBIN "../build/boot_init.rom"

;------------------------------------------------------------------------------
; avboot_image — AV ブートイメージ (640 byte、AVBOOT_ENT 基底)
;   ★ 第 1 世代変種 (INIT_AV1) はこのイメージを持たない。当該領域は空き
;     (末尾パディングでゼロ埋め) となる。
;   独立ビルド avboot.bin を埋め込む。boot_handoff が AVBOOT_ENT から AVBOOT_LEN バイトへ転送し、
;   トランポリンが AVBOOT_ENT へ引渡す。イメージ内容は avboot.s 参照 (起動モード判定 +
;   メディア存在プリフライト + F-BASIC フォールバック + FDC サービス表)。
;
;   $7C00-$7E7F (file offset $1C00) に配置する。
;   長さは $280。転送はラベル経由。
;------------------------------------------------------------------------------
                IFNDEF  INIT_AV1
                IFNDEF  STAGE_ALT_RAM
                ZMB     $7C00-*         ; AV ブートイメージの位置
                ENDC
avboot_image:   INCLUDEBIN "../build/avboot.bin"
                ENDC

;------------------------------------------------------------------------------
; 末尾リセットベクタワード ($7FFE) = $6000
;   ハードがオーバレイ有効時に $FFFE-$FFFF (= ROM offset $1FFE) から PC を取る。
;------------------------------------------------------------------------------
                ZMB     $7FFE-*         ; パディング
                FDB     reset_entry     ; $7FFE: $6000
