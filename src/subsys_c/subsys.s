; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; AltROMs subsys_c.rom — FM-7 / FM77AV サブシステム ROM (Type-C 相当)
;
; 10240 byte 固定、サブ CPU $D800-$FFFF にマップされる。
; サブ CPU が受け持つ画面表示・文字表示・キー入力・カセット入出力・グラフィック
; 描画・タイマ等のサブシステムコマンド、および BASIC の表示系コンソール処理まで
; 対応する。
;
; レイアウト:
;   $D800-$DFFF : 8x8 ANK フォント (2KB)
;   $E000-$E0FF : リセット入口 + メインループ + コマンドディスパッチ
;   $E100-$E?FF : コマンドハンドラ群 (32 個)
;   $E?00-$FE?? : 描画プリミティブ / フォント描画 / 共通ルーチン
;   $FE??-$FF?? : NMI / FIRQ ハンドラ
;   $FFF0-$FFFF : 6809 ベクタテーブル
;
; 設計方針 (独立実装):
;   - コマンドディスパッチはコマンド一覧に沿う区間表 + 表引き
;   - DP = $D0 を常用 ($D000-$D0FF の作業 RAM をダイレクトページでアクセス。共有 RAM $D380- は拡張アドレス)
;   - 描画は B/R/G 3 プレーン直接 STA
;   - キーボードはサブ CPU の FIRQ で受け、リングバッファに格納
;
; 用語凡例: コメント中の「参考書籍」は docs/LEGAL.md §6.1 に挙げる 4 点を指し、
;   「動作観察」は実行環境の上で動かして確かめた挙動 (同 §6.2) を根拠とする
;   事項を指す。
;==============================================================================

;--- 共有 RAM (絶対アドレス。常用 DP=$D0 のため拡張アドレスで参照する) ------------
; 参考書籍の共有 RAM の形式:
;   $D380 = SH_ERR  (ATN フラグ + エラーコード返却)
;   $D381 = SH_CNT  (データカウント、PRINT 等の文字数)
;   $D382 = SH_CMD  (メイン CPU が書込むコマンドコード)
;   $D383-$D38E = SH_PARAM (コマンドパラメータ 12 byte)
SH_ERR          EQU     $D380     ; $D380
SH_CNT          EQU     $D381     ; $D381
SH_CMD          EQU     $D382     ; $D382 ← コマンドはここ
SH_PARAM        EQU     $D383     ; $D383-$D38E

;--- I/O ポート (絶対アドレス、DP=$D0 範囲外) -----------------------------
;   参考書籍に載る位置だけを持つ ($D400/$D401、
;   $D409/$D40A/$D40E/$D40F)。
IO_KEY_STAT     EQU     $D400   ; キーボードステータス
IO_KEY_DATA     EQU     $D401   ; キーボードデータ
IO_VRAM_GATE    EQU     $D409   ; R=ON (VRAM アクセス可), W=OFF
IO_BUSY         EQU     $D40A   ; R=BUSY クリア, W=BUSY セット
IO_VRAM_OFS_HI  EQU     $D40E   ; VRAM オフセット上位
IO_VRAM_OFS_LO  EQU     $D40F   ; VRAM オフセット下位

;--- システム変数領域 (サブ側ワーク RAM $D000-$D37F) のうち、位置を定めて持つもの ---
; システム変数の位置は Type-A / Type-B と Type-C の 2 通りある。同じ働きの変数は同じ
; 名前で持ち、位置だけを組み立ての条件 (SUBSYS_AV = Type-A / Type-B の位置の ROM) で分ける。
;   ・取り消し (CANCEL) の割込みの印 ($FF = IRQ 発生)
;   ・表示開始位置 ($D40E/$D40F) の控え
;   ・コンソール制御 ($0C) の制御フラグ
;   ・タブストップ表 ($0B TABSET の格納先。10 byte)
;   ・$04 GET の挿入モード ($FF = ON)
;   ・$04 GET の終了キーコード
;   ・機能キー行の表示指定 ($01 の相対 8)
;   ・1 行のバイト数。Type-C の位置に置く
;   ・キー入力バッファのワーク (読出ポインタ 2 byte、書込ポインタ 2 byte、キーの数 1 byte)
;   ・割込みを発生する PF キーの番号 (押されたときに置く)
;   ・タイマ ($3D / $3E) のワーク 18 byte (うち 4 byte が時刻カウンタ)
; キー押下で入る FIRQ のハンドラが $D401 のキーコードを読み、32 文字ぶんの
; リング ($D360-$D37F) に蓄積する (先行入力)。$04 GET / $29 INKEY / $1F GRAPHIC
; CURSOR がここから取り出す。満杯のときの入力は捨てる。先行入力を停止している間
; (Set Unbuffer Mode) は、キーを受けるたびにリングを空にしてから格納するので、キーは
; リングの先頭に入る。
        IFDEF SUBSYS_AV
SUB_ABRT        EQU     $D002   ; 1: 取り消しの割込みの印 ($FF = 発生)
WK_VRAM_OFS     EQU     $D008   ; 2: 表示開始位置 ($D40E/$D40F) の控え
WK_CSCTL        EQU     $D00A   ; 1: コンソール制御 ($0C) の制御フラグ CF
TAB_TABLE       EQU     $D00D   ; 10 byte = 80 桁ぶんのタブストップビットマップ
ED_INS          EQU     $D01B   ; 1: $04 GET の挿入モード (参考書籍。0 / $FF)
GE_K            EQU     $D01C   ; 1: $04 GET の終了キーコード K (参考書籍。$05 も返す)
WK_PF_SHOW      EQU     $D020   ; 1: 機能キー行の表示指定 ($01 相対 8 = FD。非 0 で描く)
WK_LINE_BYTES   EQU     $D023   ; 2: 1 行のバイト数 (8px 行 = 640 / 10px 行 = 800)
KB_COUNT        EQU     $D098   ; 1: バッファ内キー数
KB_RDPTR        EQU     $D099   ; 2: 読出ポインタ
KB_WRPTR        EQU     $D09B   ; 2: 書込ポインタ (FIRQ ハンドラが進める)
KB_PFIRQ        EQU     $D09D   ; 1: 割込みを発生する PF キーの番号 (最後に押されたもの。0 = 無し)
TM_BASE         EQU     $D0A5   ; タイマのワーク 18 byte の基点
                ELSE
SUB_ABRT        EQU     $D000   ; 1: 取り消しの割込みの印 ($FF = 発生)
KB_COUNT        EQU     $D004   ; 1: バッファ内キー数
KB_RDPTR        EQU     $D005   ; 2: 読出ポインタ
KB_WRPTR        EQU     $D007   ; 2: 書込ポインタ (FIRQ ハンドラが進める)
KB_PFIRQ        EQU     $D009   ; 1: 割込みを発生する PF キーの番号 (最後に押されたもの。0 = 無し)
TM_BASE         EQU     $D00A   ; タイマのワーク 18 byte の基点
WK_VRAM_OFS     EQU     $D01F   ; 2: 表示開始位置 ($D40E/$D40F) の控え
WK_CSCTL        EQU     $D021   ; 1: コンソール制御 ($0C) の制御フラグ CF
TAB_TABLE       EQU     $D024   ; 10 byte = 80 桁ぶんのタブストップビットマップ
ED_INS          EQU     $D033   ; 1: $04 GET の挿入モード (参考書籍。0 / $FF)
GE_K            EQU     $D034   ; 1: $04 GET の終了キーコード K (参考書籍。$05 も返す)
WK_PF_SHOW      EQU     $D038   ; 1: 機能キー行の表示指定 ($01 相対 8 = FD。非 0 で描く)
WK_LINE_BYTES   EQU     $D03B   ; 2: 1 行のバイト数 (8px 行 = 640 / 10px 行 = 800)
        ENDC
KB_BUF_LO       EQU     $D360   ; リングバッファ先頭 (32 byte)
KB_BUF_SIZE     EQU     32                      ; 容量 = 先頭の境界でもある (下の畳み込みが成り立つ)
KB_BUF_HI       EQU     KB_BUF_LO+KB_BUF_SIZE   ; 終端+1 (ここで KB_BUF_LO へ wrap)
KB_BUF_MASK     EQU     KB_BUF_SIZE-1           ; 下位バイトの折返しマスク
KB_BUF_ORG      EQU     KB_BUF_LO&$FF           ; 下位バイトの先頭値 (上位は常に KB_BUF_LO>>8)
;--- タイマ ($3D SET TIMER / $3E READ TIMER、参考書籍) のワーク ---
;   相対 1〜17 が READ TIMER の応答 17 byte の並びそのもの (TC、時刻、予約時刻、
;   インターバルカウンタ、再設定値)。相対 0 は周期処理の抑止の印 (設定中は 非 0)。
TM_GUARD        EQU     TM_BASE+0       ; 1: 非 0 の間は周期処理を行なわない (設定・読出の最中)
TM_TC           EQU     TM_BASE+1       ; 1: タイマ制御 TC (bit0 TIM / bit1 INT / bit2 ONE / bit3 0H)
TM_CLK          EQU     TM_BASE+2       ; 4: 時刻カウンタ (時, 分, 秒, フレーム)
TM_T1I          EQU     TM_BASE+6       ; 4: 割込み予約時刻
TM_T2           EQU     TM_BASE+10      ; 4: インターバルカウンタ (上位:下位の 32bit)
TM_T2D          EQU     TM_BASE+14      ; 4: インターバルの再設定値
TM_END          EQU     TM_BASE+18      ; 終端+1
;--- PF (ファンクション) キー定義テーブル ($D2C0-$D35F、参考書籍) ---
; bit8 付きキー (PF1-PF10 = code 1-10) は KB_PF_BASE + code*16 の 16 byte を使う。
;   +0 = bit7: PF キー割込み制御フラグ (1 = 割込みを発生し、文字列を生成しない) /
;        bit0-3: 定義文字列の長さ (0〜15)、+1〜+15 = 定義文字列。
;   PF キー番号は参考書籍で 1〜10 なので 0 番の 16 byte
;   (KB_PF_BASE〜) は使わない (Type-A / Type-B の位置ではその場所に割込みフックテーブルがある)。
KB_PF_BASE      EQU     $D2B0                   ; PF テーブルの計算の基点 (PF1 = $D2C0)
KB_PF_SIZE      EQU     160                     ; PF1〜PF10 × 16 byte
        IFDEF SUBSYS_AV
;--- 拡張コマンドテーブル ($D278-$D2A7、参考書籍) ---
;   命令コード $80〜$8F の 16 件 × 3 byte: +0 = 使用フラグ ($FF = 使用中 / $00 = 未使用)、
;   +1〜+2 = 処理のアドレス (RTS で終わる)。使用中なら JSR で呼ぶ。
XCMD_TABLE      EQU     $D278
;--- 割込みフックテーブル ($D2A8-$D2B9、参考書籍) ---
;   SWI3 / SWI2 / FIRQ / IRQ / SWI / NMI の順に 3 byte ずつ。ベクタ ($FFF2-$FFFD) は
;   この表を指し、リセットで各 3 byte に ROM の処理への JMP を敷く。
HOOK_TABLE      EQU     $D2A8
                ENDC

;--- 範囲消去 ($01/$02/$0C/$0D) の作業域 ---------------------------------------
;   直線ラスタライズの作業域 ($D064-$D06D) を借りる。消去の最中に直線を引くことは
;   無く、直線の最中に消去が入ることも無いので、両者は同時に生きない。
ER_END          EQU     $D064   ; 2: 消去する範囲の終端オフセット (プレーン内)
ER_V            EQU     $D066   ; 6: 3 プレーン (B/R/G) の充填値
ER_SRC          EQU     $D06C   ; 2: 消去する範囲の先頭オフセット

;--- PutBlock パターンストリーム ($D0E2-$D0E6。直接ページ $D0 で参照する) ---
PBS_PTR         EQU     $D0E2   ; 2: パターン読出ポインタ
PBS_CNT         EQU     $D0E4   ; 1: 残データ byte 数 ($D38D 計数)
WK_BGCOL        EQU     $D0E5   ; 1: 背景色 (0-7)。$01 コンソールイニシャライズの
                                ;    検証合格時に確定。描画ファンクション 1 (PRESET)
                                ;    の描画色として参照 (当方の取り決め)。初期値 0 は
                                ;    リセットの init_clear_loop が保証。
PBF_FF          EQU     $D0E6   ; 1: 非0 = PutBlock1 のマスク $FF 直書き可
                                ;    (fn 0/1/5 = 色置換系のみ。コマンド入口で確定)

;--- キャラクタブロック ($06-$09) / Character Line ($20) ワーク ---
; PutBlock 高速経路の固定ワーク (PBF_BASE=$D0C6-$D0E1) と重畳して使う。
; キャラクタブロック系 ($06-$09/$20) と 矩形書込み系 ($1C/$1E) は別コマンドで
; 同時実行されないため干渉しない (PAINT/$04 とも非同時)。
CB_BASE         EQU     $D0C6           ; = PBF_BASE (重畳)
CB_ROW          EQU     CB_BASE+0       ; 1: 現在行 (走査)
CB_COL          EQU     CB_BASE+1       ; 1: 現在桁 (走査)
CB_RMAX         EQU     CB_BASE+2       ; 1: 最大行
CB_CMAX         EQU     CB_BASE+3       ; 1: 最大桁
CB_CMIN         EQU     CB_BASE+4       ; 1: 行頭桁 (行送り時の復帰桁)
CB_FLAG         EQU     CB_BASE+5       ; 1: 0=形式1(文字のみ) / 非0=形式2(属性+文字)
CB_FATR         EQU     CB_BASE+6       ; 1: 形式1 固定アトリビュート ($D387)
CB_RPTR         EQU     CB_BASE+7       ; 2: GET 応答書込ポインタ
CB_SAVE         EQU     CB_BASE+9       ; 3: cb_drawcell カーソル桁/行/ON 退避
CB_SAVCOL       EQU     CB_BASE+12      ; 1: cb_drawcell 現在色退避
CB_DCOL         EQU     CB_BASE+13      ; 1: 描画対象セル 桁 (CB_DROW と隣接、LDD 用)
CB_DROW         EQU     CB_BASE+14      ; 1: 同 行
CL_ATR          EQU     CB_BASE+15      ; 1: $20 アトリビュート
CL_CHR          EQU     CB_BASE+16      ; 1: $20 文字コード
CL_DX           EQU     CB_BASE+17      ; 1: $20 |dx|
CL_DY           EQU     CB_BASE+18      ; 1: $20 |dy|
CL_SX           EQU     CB_BASE+19      ; 1: $20 桁方向 (+1/$FF)
CL_SY           EQU     CB_BASE+20      ; 1: $20 行方向 (+1/$FF)
CL_N            EQU     CB_BASE+21      ; 1: $20 残ステップ
CL_ACC          EQU     CB_BASE+22      ; 1: $20 DDA アキュムレータ
CL_END          EQU     CB_BASE+23      ; 1: $20 塗潰し 終端行 (cl_line 呼出間で保持)

;--- ワーク RAM レイアウト ($C000-$CFFF, 4KB) ----------------------------
WK_BASE         EQU     $D040
WK_SCREEN_W     EQU     WK_BASE+$00     ; 1: 画面幅 (byte 数) = 80 or 40
WK_SCREEN_H     EQU     WK_BASE+$01     ; 1: 画面高 (行数) = 25 or 20
WK_CURSOR_X     EQU     WK_BASE+$02     ; 1: カーソル X 位置 (文字単位)
WK_CURSOR_Y     EQU     WK_BASE+$03     ; 1: カーソル Y 位置 (文字単位)
WK_CURSOR_ON    EQU     WK_BASE+$04     ; 1: いまセルへ反転を掛けているかの控え ($00/$FF)。
                                        ;    カーソルは反転で見せるので、消す側は「掛かって
                                        ;    いるか」を知らないと逆に掛けてしまう。
WK_CURSOR_EN    EQU     WK_BASE+$05     ; 1: カーソルを出す指定かの控え ($00/$FF)。
                                        ;    WK_CURSOR_ON と隣接させ、両者を 16bit で同時に
                                        ;    書けるようにしてある。
WK_COLOR        EQU     WK_BASE+$06     ; 1: 現在描画色 (0-7)
WK_ATTR_MODE    EQU     WK_BASE+$07     ; 1: 描画モード (0=REPLACE, 1=OR, 2=AND, 3=XOR)
WK_VIEW_X0      EQU     WK_BASE+$08     ; 2: クリッピング枠 左上 X
WK_VIEW_Y0      EQU     WK_BASE+$0A     ; 2: 同 Y
WK_VIEW_X1      EQU     WK_BASE+$0C     ; 2: 右下 X
WK_VIEW_Y1      EQU     WK_BASE+$0E     ; 2: 同 Y
WK_LAST_X       EQU     WK_BASE+$10     ; 2: 前回終点 X (LINE 続き用)
WK_LAST_Y       EQU     WK_BASE+$12     ; 2: 同 Y
WK_ESC_STATE    EQU     WK_BASE+$14     ; 1: オーダシーケンスの状態 (0=通常 / $11=SF の属性待ち / $12=SBA:桁待ち / $92=SBA:行待ち / $13=RC:文字数待ち / $93=RC:文字待ち / $1B=ESC の次待ち)
WK_CHAR_DBL     EQU     WK_BASE+$15     ; 1: 文字倍幅フラグ (0=80桁8px / 非0=40桁16px)
GE_FS           EQU     WK_BASE+$16     ; 2: $04/$05 の応答: 変更フィールドの先頭セル
GE_FE           EQU     WK_BASE+$18     ; 2: 同 終り (排他)
WK_ROW20        EQU     WK_BASE+$1A     ; 1: 行ピッチ (0=8px/25行, 非0=10px/20行)
WK_KB_CTL       EQU     WK_BASE+$1B     ; 1: キー入力の制御 (参考書籍)。
                                        ;    bit7 = Lock Keyboard ($1B $23) の間はキー入力を受付けない
                                        ;    bit0 = Set Buffer Mode ($1B $67) で先行入力を行なう
                                        ;    (STAGE_ALT_RAM を定義しない ROM はリセット時に 1 にする。
                                        ;    ワーク RAM を 0 で消すと停止の側になる。動作観察)
WK_MONO         EQU     WK_BASE+$1C     ; 1: 単色表示 (グリーン) の指定 ($01 の相対 10 = GR)。
                                        ;    非 0 なら文字を緑だけで描く (色の控えは変えない)
WK_LOC_COL      EQU     WK_BASE+$1D     ; 1: $12 カーソル位置指定 第1パラメータ (桁) 退避
                                        ;    (機能キー行の表示指定 WK_PF_SHOW は位置を参考書籍が
                                        ;     定めるので冒頭で割り付ける。pf_show 参照)
WK_WIN_TOP      EQU     WK_BASE+$2E     ; 1: スクロール範囲 上端行 (現行ピッチの行単位、全画面=0)
WK_WIN_END      EQU     WK_BASE+$2F     ; 1: スクロール範囲 下端行+1 (排他、全画面=行数)
WK_TXT_COL      EQU     WK_BASE+$1E     ; 1: SF オーダ (参考書籍) で定めた属性 (bit7 を除く)。
                                        ;    SF に続く一連の文字列 (文字・BEL・BS・HT・RC) は
                                        ;    この属性で描く。グラフィック描画色 WK_COLOR とは独立
                                        ;    ($04 GET の挿入モード ED_INS は位置を参考書籍が
                                        ;     定めるので冒頭で割り付ける)
WK_BLINK        EQU     WK_BASE+$1F     ; 1: カーソルの点滅の計時の控え。$04 GET のキー待ちが
                                        ;    最後に見た時刻カウンタの最下位桁 ($D00F)。
                                        ;    読み書きするのはキー待ちの繰り返しだけで、
                                        ;    割り込みの処理はここを読みも書きもしない
WK_SF_ADR       EQU     $D0C0           ; 2: SF オーダを置いたセル (0 = 一連の文字列の外)
GE_PTR          EQU     $D0C2           ; 2: $04/$05 の応答の書込み位置
WK_CELL_COL     EQU     $D0C4           ; 1: いま描くセルの属性 (色は bit0-2)
WK_SYM_INV      EQU     $D0C5           ; 1: SYMBOL のファンクションコード NOT (5) によるパターン反転 ($FF / 0)
;--- 描画用パラメータ受け渡し (内部呼出用) ---
WK_PX_X         EQU     WK_BASE+$30     ; 2: PSET 用 X 座標 (水平線バイトブリット経路が
                                        ;    左端 X として借りる。同経路は打点を呼ばない)
WK_PX_Y         EQU     WK_BASE+$32     ; 2: PSET 用 Y 座標 (低 byte 使用)
WK_LN_X0        EQU     WK_BASE+$34     ; 2: LINE 始点 X
WK_LN_Y0        EQU     WK_BASE+$36     ; 2: LINE 始点 Y
WK_LN_X1        EQU     WK_BASE+$38     ; 2: LINE 終点 X
WK_LN_Y1        EQU     WK_BASE+$3A     ; 2: LINE 終点 Y
WK_TMP          EQU     WK_BASE+$3C     ; 2: テンポラリ (画素のアドレス計算の途中結果)。
                                        ;    周期処理 (NMI) の側から書いてはならない。
                                        ;    画素の読み書きが「置く→足す」の 2 命令に
                                        ;    またがる隙へ割込みが入ると途中結果が壊れ、
                                        ;    その画素だけ別アドレスを指す。割込み側でアドレスを
                                        ;    組む必要があるときはスタックを使うこと。
WK_BIT_MASK     EQU     WK_BASE+$3E     ; 1: PSET 用 bit マスク
                                        ;    (WK_BASE+$3F は空き。$04 GET の終了キーコード GE_K は
                                        ;     位置を参考書籍が定めるので冒頭で割り付ける)
;--- 矩形 (BOX/FILL) 用 4 隅退避 ---
WK_RECT_X0      EQU     WK_BASE+$40     ; 2
WK_RECT_Y0      EQU     WK_BASE+$42     ; 2
WK_RECT_X1      EQU     WK_BASE+$44     ; 2
WK_RECT_Y1      EQU     WK_BASE+$46     ; 2
WK_RECT_CY      EQU     WK_BASE+$48     ; 2: FILL 用 現在行 Y
WK_PAINT_BCOL   EQU     WK_BASE+$4A     ; 1: PAINT 境界色
WK_PAINT_FCOL   EQU     WK_BASE+$4B     ; 1: PAINT 塗色
WK_CC_OLDCOL    EQU     WK_BASE+$4C     ; 1: Change Color 旧色
WK_PB_FN        EQU     WK_BASE+$4D     ; 1: $1E ファンクションコード ($D38C、入口で確定)
WK_TIMER        EQU     WK_BASE+$4E     ; 2: ソフトウェアタイマ ($3D/$3E)
WK_GB_BITACC    EQU     WK_BASE+$50     ; 1: GetBlock1 ビット集約バッファ (番兵ビット方式。
                                        ;    $01 = 端数なし。画素ビットを ROL で下から入れ、
                                        ;    番兵が押し出された時点で 1 byte 確定)
GE_EDIT         EQU     WK_BASE+$51     ; 1: $04 GET の編集中の印 ($40 = 描いたセルの属性に M (変更) を立てる / 0)
WK_GB_TGTCOL    EQU     WK_BASE+$52     ; 1: GetBlock 走査モード (0=色リスト一致 / 非0=プレーンマスク)
WK_GB_NCOL      EQU     WK_BASE+$53     ; 1: GetBlock1 指定色数 (1-8)
;--- PAINT シード座標スクラッチ (描画プリミティブと非干渉な専用域) ---
; pset_internal / get_pixel_color が WK_TMP / WK_BIT_MASK を破壊するため、
; シード座標の受け渡しにはそれらと重ならない COPY-PARAM 退避域を流用する
; (COPY コマンドは PAINT と同時実行されないため安全)。
WK_SEED_X       EQU     WK_BASE+$20     ; 2: pf_push/pf_pop 用 シード X
WK_SEED_Y       EQU     WK_BASE+$22     ; 2: 同 シード Y
WK_SYM_SAVC     EQU     WK_BASE+$54     ; 1: SYMBOL 入口の WK_COLOR 退避 (出口で復元)
;--- SYMBOL (cmd $19) 専用スクラッチ (グラフィック系・他描画と非同時) と PAINT の走査変数 ---
;   Type-A / Type-B の位置 (SUBSYS_AV) では、参考書籍が $D096-$D0B6 に
;   キー入力バッファのワークとタイマのワークを定めるので、この区画の作業変数はシステム変数
;   領域の空き ($D004-$D007、$D025-$D03F、$D0BE-$D0BF) へ移す。並び (WK_SYM_X と WK_SYM_Y の
;   隣接、WK_SYM_PTR 以降の 23 byte の相対) は変えない。
        IFDEF SUBSYS_AV
WK_SYM_X        EQU     $D004           ; 2: 文字基準 X (描画進行で更新)
WK_SYM_Y        EQU     $D006           ; 2: 文字基準 Y (WK_SYM_X と隣接)
WK_SYM_CNT      EQU     $D03D           ; 1: 残文字数
WK_PAINT_SP     EQU     $D03E           ; 2: PAINT シードスタックポインタ
WK_SYM_MAGX     EQU     $D0BE           ; 1: 横倍率
WK_SYM_MAGY     EQU     $D0BF           ; 1: 縦倍率
SYM_B           EQU     $D025           ; WK_SYM_PTR 以降の 23 byte の基点 (= PT_B の実効アドレス)
        ELSE
WK_PAINT_SP     EQU     WK_BASE+$56     ; 2: PAINT シードスタックポインタ
WK_SYM_X        EQU     WK_BASE+$58     ; 2: 文字基準 X (描画進行で更新)
WK_SYM_Y        EQU     WK_BASE+$5A     ; 2: 文字基準 Y (WK_SYM_X と隣接)
WK_SYM_MAGX     EQU     WK_BASE+$5C     ; 1: 横倍率
WK_SYM_MAGY     EQU     WK_BASE+$5D     ; 1: 縦倍率
WK_SYM_CNT      EQU     WK_BASE+$5E     ; 1: 残文字数
SYM_B           EQU     WK_BASE+$60     ; WK_SYM_PTR 以降の 23 byte の基点 (= PT_B の実効アドレス)
        ENDC
WK_SYM_PTR      EQU     SYM_B+0         ; 2: 文字列ポインタ
WK_SYM_GLYPH    EQU     SYM_B+2         ; 2: 現グリフフォント先頭
WK_SYM_ROW      EQU     SYM_B+4         ; 1: 行カウンタ (8→0)
WK_SYM_PAT      EQU     SYM_B+5         ; 1: 現行フォントパターン
WK_SYM_CX       EQU     SYM_B+6         ; 2: 1 文字内ピクセル基準 X
WK_SYM_CY       EQU     SYM_B+8         ; 2: 1 文字内ピクセル基準 Y
WK_SYM_DUPX     EQU     SYM_B+10        ; 1: 横倍化カウンタ
WK_SYM_DUPY     EQU     SYM_B+11        ; 1: 縦倍化カウンタ
WK_SYM_BITM     EQU     SYM_B+12        ; 1: ビット走査マスク
WK_SYM_VEC      EQU     SYM_B+13        ; 2: アングルコードで選んだ進み方向表の位置
                                        ;    (指す 2 byte が行送り DV、その次の 2 byte が
                                        ;    桁送り DU)。GetBlock1 の色リスト / PAINT の
                                        ;    走査変数と重畳するが、いずれも SYMBOL とは
                                        ;    別コマンドで同時に走らない (既存の重畳規約)
WK_GB_COLS      EQU     SYM_B+13        ; 8: GetBlock1 指定色リスト退避 ($1B 実行中のみ使用。
                                        ;    SYMBOL/$04/PAINT のスクラッチと重畳だが別コマンドで非同時)
; --- コンソールバッファ (サブ側 $C000-$CF9F、参考書籍) ---
;   文字コードバッファ $C000-$C7CF = 各座標 (Y*桁数+X) の文字コード。
;   アトリビュートバッファ $C7D0-$CF9F = 同座標のアトリビュート文字
;   (bit7 F=フィールドの先頭 / bit6 M=変更 / bit4 P=保護 / bit3 R=反転 / bit0-2 CL=色。
;   参考書籍)。両者は 1 対 1 に対応する。
CONBUF_CHAR     EQU     $C000
CONBUF_ATTR     EQU     $C7D0           ; = CONBUF_CHAR + $07D0 (固定オフセット)

;--- VRAM 配置 -----------------------------------------------------------
VRAM_B          EQU     $0000   ; B プレーン (16KB) ※ $D409 ゲートで開く
VRAM_R          EQU     $4000   ; R プレーン
VRAM_G          EQU     $8000   ; G プレーン
VRAM_PLANE_SZ   EQU     $4000   ; プレーンサイズ

;==============================================================================
; ROM 配置開始 ($D800)
;
; 条件アセンブル SUBSYS_AV (FM77AV 系のサブシステム ROM ビルド):
;   本ソースは 3 つの ROM を生成する共用体である。
;     - 既定 (SUBSYS_AV 未定義): subsys_c.rom (FM-7 Type-C、10240 byte、
;       $D800-$DFFF フォント + $E000-$FFFF コード)
;     - SUBSYS_AV 定義時: subsys_a.rom / subsys_b.rom (FM77AV 系で $FD13 が
;       Type-A / Type-B として選ぶ位置に置く ROM。8192 byte、$E000-$FFFF コードのみ。
;       subsys_b.rom は SUBSYS_TB も定義する)
;   AV 機の Type-A / Type-B 選択中は $D800-$DFFF がサブ CPU から CG バンク領域として
;   見え、代替 CG ROM のフォント (同一 font.bin) が同じアドレスに供給されるため、
;   フォント参照 (font_base=$D800 起点) はどちらのビルドでも同一に機能する。
;   メイン CPU 側はモニタ選択に依らず同一の共有 RAM コマンド
;   手順 (HALT + $D382=コマンド/$D383-=パラメータ) で受け渡しを行う (動作観察) ため、
;   コマンド処理本体は共通である。SUBSYS_AV 定義時だけ、FM77AV の命令 $45
;   (キーエンコーダ命令)・$7F (共有 RAM 上の処理の実行)・$22 (表示ページ・処理
;   ページの選択)・$39 (描画範囲の設定と枠付きの矩形の塗り) を区間表に加え、$15 LINE・$18 PAINT・
;   $39 を ALU とラインドロワで描く。並びと画面は型で分ける:
;     - SUBSYS_TB 定義時 (subsys_b.rom): 色が B・R・G 各 4 ビットの並び、320×200 の
;       画面。$1B は受け付けず ($46)、$1D は 1 画素 2 バイトで返す
;     - SUBSYS_TB 未定義 (subsys_a.rom): 色が 1 バイトの並び、640×200 の 8 色の画面
;==============================================================================

        IFNDEF SUBSYS_AV
                ORG     $D800

;==============================================================================
; ANK フォント領域 ($D800-$DFFF, 2KB)
; 256 文字 × 8 byte。font.bin 全体をそのまま格納する (subsyscg バンク 0 と同一)。
;
; 【設計制約・厳守】$D800-$DFFF はフォントデータ専用 (実行コード配置禁止)。
;   AV 機ではこの領域が Type-C 選択中でも CGROM バンク領域として供給され、
;   subsys_c ROM 側のこの範囲は不可視になる (動作観察)。ここへ実行コードを
;   置くと、AV 機ではそのコードの代わりにフォントデータが見え、呼出した
;   サブ CPU が即死する。実行コードは必ず $E000 以降に置くこと。
;==============================================================================
font_base:
                INCLUDEBIN "../build/font.bin"

                ; font_base から 2048 byte の範囲がフォント。
                ; font.bin は scripts/genfont.py で生成 (256 文字 × 8 byte = 2048 byte)。
                ; サイズ検証は Makefile の `make verify` で行う。
        ELSE
; AV Type-A ビルド: フォント区画は ROM に含めない ($D800-$DFFF は CG バンク領域)。
; 代替 CG ROM が同一 font.bin を同アドレスレイアウトで供給する (全バンク同内容)。
font_base       EQU     $D800
        ENDC

;==============================================================================
; ROM コード本体 ($E000-)
;==============================================================================

                ORG     $E000

;------------------------------------------------------------------------------
; リセット入口
;------------------------------------------------------------------------------
reset_entry:
                ; IRQ/FIRQ は 6809 のリセットでマスクされた状態から始まる
                ; 起動時に残っているキーとストローブを読み捨てる (参考書籍:
                ; $D400/$D401 の読出しでストローブのフリップフロップが消える)
                LDD     IO_KEY_STAT
                ; ワーク RAM の範囲 $D000-$D37F (参考書籍) と共有 RAM の相対 0〜1 を 0 に。
                ;   S を設定する前に済ませる (6809 の NMI はリセットの後、S をプログラムで
                ;   設定するまで受け付けられない。CPU の仕様)。Type-A / Type-B の位置では
                ;   NMI のベクタが RAM の割込みフックテーブルを指すので、フックを敷き終える
                ;   まで NMI が入らないよう、この順にする
                LDX     #$D000
                LDU     #$0000
init_clear_loop:
                STU     ,X++
                CMPX    #SH_CMD
                BCS     init_clear_loop
                CLR     ,X                      ; 相対 2 (コマンド) も 0 (X = SH_CMD で抜けてくる)
        IFDEF SUBSYS_AV
                ; 割込みフックテーブル ($D2A8-$D2B9。参考書籍) に、
                ;   SWI3 / SWI2 / FIRQ / IRQ / SWI / NMI の順で ROM の処理への JMP を敷く
                LDX     #hook_tmpl
                LDU     #HOOK_TABLE
hook_init:
                LDD     ,X++
                STD     ,U++
                CMPX    #hook_tmpl+18
                BNE     hook_init
        ENDC
                LDS     #$D000          ; SP = $D000 (push 時 $CFFF, $CFFE... と下方に伸びる。
                                                ;   共有 RAM ($D380+) と衝突しない)
                                                ;   S の最初の設定は LDS の即値で行う。NMI は S の
                                                ;   設定が済むまで受け付けられないので、ここで確実に
                                                ;   設定し、周期 NMI (タイマの計時) を入れて
                                                ;   READ TIMER ($3E) の待ちを成り立たせる
                TFR     S,D             ; D = $D000 / A = $D0 (DP 値)
                TFR     A,DP            ; DP = $D0 (= 作業 RAM ダイレクト)
                ; --- 既定の画面パラメータ: 80x25 テキスト / 描画範囲は画面全体 /
                ;     描画色 7 / スクロール範囲は画面全体 [0,25)。直前の
                ;     init_clear_loop が WK_BASE 域を 0 にしているので、0 初期化
                ;     (WK_WIN_TOP / WK_VIEW_X0 / WK_VIEW_Y0) は置かない
        IFDEF SUBSYS_TB
                LDD     #40*256+25              ; Type-B の位置: 40 桁 25 行
        ELSE
                LDD     #80*256+25              ; A=80 (桁), B=25 (行)
        ENDC
                STD     <WK_SCREEN_W            ; WK_SCREEN_W / WK_SCREEN_H (隣接)
                STB     <WK_WIN_END             ; 範囲下端 = 25 (全画面)
        IFDEF SUBSYS_TB
                LDX     #320                    ; 1 行のバイト数 (8 ライン × 40 バイト)
        ELSE
                LDX     #640                    ; 1 行のバイト数 (80x25 = 8px 行)
        ENDC
                STX     <WK_LINE_BYTES
        IFDEF SUBSYS_AV
                JSR     av_view_full            ; 描画範囲 = 画面全体 ($39 が変える)
        ELSE
                LDD     #639
                STD     <WK_VIEW_X1
                LDD     #199
                STD     <WK_VIEW_Y1
        ENDC
                LDA     #7
                STA     <WK_COLOR
                STA     <WK_TXT_COL             ; 文字色も白で初期化 (既定の文字属性 7)
        IFDEF   SUBSYS_AV
                ; --- VRAM の 2 ページ (96 KB) を 0 にする (動作観察)。ALU を PSET
                ;     (色 0、マスクなし) にして、処理ページごとに $0000-$3FFF へ
                ;     PSHU で 8 バイトずつ書く (書いた値は ALU が捨て、3 面に色 0 が入る)
                LDA     #$80
                STA     >IO_ALU_CMD             ; ALU = PSET
                CLR     >IO_ALU_COL             ; 色 0
                CLR     >IO_ALU_MSK             ; 全ビットを書く
                LDA     #$08
                STA     >IO_ALU_DIS             ; 3 面とも書く
                LDB     #$20                    ; 処理ページ 1 → 0 の順
avc_pg:
                STB     >IO_AV_MISC
                LDU     #$4000
avc_lp:
                PSHU    CC,A,B,DP,X,Y           ; 8 バイトずつ
                CMPU    #0
                BNE     avc_lp
                SUBB    #$20
                BCC     avc_pg                  ; $20 → 0 → (借り) 終わり
                CLR     >IO_ALU_CMD             ; ALU を止める
        ENDC
                ; コンソール制御 ($D021) の RESET 時の既定 (参考書籍:
                ;   CF = $23 = オート LF (bit5) / オーダ動作 (bit1) / カーソル表示 (bit0))
                LDA     #$23
                STA     <WK_CSCTL
                ; --- 画面全体を消す。参考書籍の RESET 時の
                ;     パラメータは ERS = 1 (初期設定の後に画面を消去する) である。
                ;     コンソールバッファ (文字 = Null、属性 = WK_TXT_COL = 7)、VRAM
                ;     (背景色 WK_BGCOL = 0)、表示開始位置、SF の一連の終了までを
                ;     erase_rows がまとめて行う。ここまでに画面幅・行数・1 行の
                ;     バイト数・背景色・文字属性は確定している
                CLRA                            ; 上端行 = 0
                LDB     <WK_SCREEN_H            ; 下端行+1 = 行数 (= 画面全体)
                JSR     erase_rows
                JSR     f_head                  ; 画面の先頭のセルは F (動作観察)
                ; --- カーソルを出す。RESET 時の CF は $23 (参考書籍
                ;     ) で bit0 = カーソル表示なので、その指定どおりに
                ;     原点のセルへ反転を掛ける
                LDA     <WK_CSCTL
                JSR     con_cur_apply
                LDU     #KB_BUF_LO              ; キー入力バッファは空 (U はメインループの先頭で作り直す)
                STU     <KB_RDPTR
                STU     <KB_WRPTR
                IFNDEF  STAGE_ALT_RAM
                INC     <WK_KB_CTL              ; リセット時は先行入力 (bit0 = 1。動作観察)
                ENDC
                ; 表示を点灯する (動作観察)。リセット
                ;   直後の表示は消灯で、サブ CPU 側の操作で点灯・消灯する仕組みは
                ;   参考書籍に載る。$D408 を読むと点灯し、書くと消灯する。
                ;   画面の初期化を終え、メイン CPU へ準備完了を返す前に 1 回読む。
                ;   STAGE_ALT_RAM を定義した ROM にも入れる ($D402 と同じ扱い)。
                TST     >$D408                  ; 読みで表示を点灯する
                ; BUSY はここでは落とさない。最初に落とすのはコマンド待ち (sh_rereq) で、
                ;   共有 RAM にコマンドが置かれていないことを確かめてからにする
                ; キーボード FIRQ (参考書籍) を受ける。IRQ (COMMAND ABORT、
                ; 参考書籍) は命令の実行中だけ受ける (dispatch で解除する)
                ANDCC   #$BF                    ; F (FIRQ) を解除

;------------------------------------------------------------------------------
; メインループ
;   メイン CPU からの ATN を待ち、コマンドを受信してディスパッチ。
;   応答後はループ先頭に戻る。
;------------------------------------------------------------------------------
main_loop:
                LDD     #$D000                  ; D = SP 初期値 / A = DP 値 (1 命令で両方を用意)
                TFR     D,S                     ; SP リセット
                ; DP=$D0 を毎サイクル再確立 (防御)。コマンド待ち (wait_cmd/sh_rereq)
                ;   や各ハンドラは DP=$D0 で $D0xx の作業 RAM をダイレクトアクセスする前提。
                ;   $3F の CALL が実行するアップロードコードが DP を変えたまま RTS で戻らない
                ;   等で DP がリークすると、作業 RAM の参照が別アドレスへ化けてコマンドを
                ;   永久に見落とす。ここで必ず
                ;   $D0 に戻し、ループが壊れた DP に影響されないようにする。
                TFR     A,DP                    ; A = $D0 (上の LDD で用意済み)
                ; コマンド待ちと命令の受付・完了の共通部は、ワーク RAM ($D000-$D37F) のうち
                ;   WK_PB_FN の 1 byte にしか書かない。$3F / $7F でサブ CPU へ処理を転送する
                ;   プログラムは、ワーク RAM のどこにでも自分のコードやデータを置いてよく、
                ;   転送された処理から戻るたびにここを通るからである
                LDU     #SH_PARAM               ; U = 共有 RAM 相対 3 ($D383)。コマンド待ちと各ハンドラは
                                                ;   U を変えるまで共有 RAM を n,U で参照する
                                                ;   (相対 0 = -3,U / 相対 1 = -2,U / 相対 2 = -1,U /
                                                ;   相対 3+n = n,U)

                ; コマンド待ち: $D380 の ATN ビットを落として応答の受領とし、$D382 (SH_CMD)
                ;   が 0 でなければ HALT の間に置かれたコマンドとして BUSY のまま受ける。
                ;   0 なら $D40A を読んで BUSY を落とし (= サブ レディ)、$D382 が 0 でなく
                ;   なるまで読み直す。その間に $D380 bit7 (ATN) が立てば先頭からやり直す。
wait_cmd:
                ; この待ちは分割転送の待ち (sh_rereq) と共用する。
                ;   戻り: A = コマンドコード、BUSY はセット済。
                JSR     sh_rereq
dispatch_cmd2:                                  ; BUSY セット済の再ディスパッチ入口
                CLR     -1,U                    ; 受理した時点で消費済にする
                ; 共有 RAM 相対 0 は ATN (MSB) とエラーコード。
                ;   受理の時点で 0 (= 正常) に初期化し、以降コマンド処理が誤りを
                ;   見つけた時だけ参考書籍の値を置く。応答完了 (dispatch_done)
                ;   では触らないので、置かれた値はメインが読むまで残る。
                CLR     -3,U
                ; 参考書籍のファンクションコードは PSET (0) を既定とし、
                ;   これを指定するコマンドだけが上書きする。
                CLR     <WK_PB_FN
        IFDEF SUBSYS_AV
                TSTA                            ; $00 は何もしない正常応答
                BEQ     dispatch_done
        ENDC
                ; 参考書籍のコマンド一覧に載る区間だけを表へ通す。
                LDX     #cmd_rangetab
dsp_scan:
                TST     ,X                      ; 下限 0 = 区間表の終り
                BEQ     dsp_badcode
                CMPA    1,X                     ; 上限を超える?
                BHI     dsp_next
                CMPA    ,X                      ; 下限以上?
                BCC     dsp_hit
dsp_next:
                LEAX    3,X                     ; 次の区間へ
                BRA     dsp_scan
dsp_hit:
                SUBA    ,X                      ; 区間内の位置
                ADDA    2,X                     ; + 区間の索引起点 = 表の索引
                LDX     #cmd_dispatch_table
                ASLA                    ; *2 (1 エントリ 2 byte。索引は 36 以下で A は正)
                ; 取り消し (COMMAND ABORT、参考書籍) の IRQ を、命令の
                ;   実行中だけ受ける。受理より前に届いていた要求は $D402 の読出しで消してから
                ;   解除する (残っていると次の命令が直ちに取り消される)
dsp_abrt:                                       ; X = 表 / A = 変位。拡張コマンドも同じ入口を使う
                TST     >$D402
                ANDCC   #$EF            ; I (IRQ) を解除
                JSR     [A,X]                   ; 表の [索引*2] のハンドラを呼ぶ
                BRA     dispatch_done
dsp_badcode:
                CMPA    #$64                    ; 継続コマンド (参考書籍) は一覧のうち。
                BEQ     dispatch_done           ;   受取るデータが無ければ正常応答のみ
        IFDEF SUBSYS_AV
                ; 拡張コマンド $80〜$8F (参考書籍): 拡張コマンド
                ;   テーブル ($D278-) の 3 byte (使用フラグ、処理のアドレス) を引き、使用中なら
                ;   JSR で呼ぶ。未使用は何もせず正常に戻る。処理は RAM の上のプログラムなので、その
                ;   実行中に届いた取り消しは、転送された処理と同じく印を置いて戻るだけになる
                SUBA    #$80            ; $80 未満は借りが出て $0F より大きくなる
                CMPA    #$0F
                BHI     dsp_notx
                LDB     #3
                MUL
                LDX     #XCMD_TABLE
                ABX
                TST     ,X+             ; 使用フラグ
                BEQ     dispatch_done
                CLRA                    ; X = 処理のアドレスの位置 / A = 0 で共通の入口へ
                BRA     dsp_abrt
dsp_notx:
        ENDC
                LDA     #$46                    ; 一覧に無いコード = コマンドコードの
                STA     -3,U                    ;   誤り (参考書籍)
                                                ; ↓ dispatch_done へ落ちる

dispatch_done:
                ORCC    #$10            ; 命令の外では取り消しの IRQ を受けない

                ; 応答完了通知:
                ;   - $D381 (SH_CNT) / $D382 (SH_CMD) / $D380 (SH_ERR/ATN) を 0 へ戻す。
                ;   - $D381 はコマンド応答のデータ計数と、周期 NMI が公開するイベント
                ;     状態の両方に使われる位置である。応答側の計数を残したままにすると、
                ;     次のコマンドを出したメインが前回の計数を読んで二重に取り込む。
                ;   - ただし周期 NMI が立てるタイマの事象 (bit4 TIMER / bit5 INTERVAL /
                ;     bit6 0 時。参考書籍) は残す。予約時刻の一致は
                ;     1 フレームだけ立つので、コマンドの処理中に立ったものをここで 0 へ戻すと
                ;     メインへ届かない。0 にするのはメイン CPU (参考書籍)。
                LDA     >SH_CNT                 ; $D381 ← タイマの事象ビットだけ残す
                ANDA    #$70
                STA     >SH_CNT
                CLR     >SH_CMD                 ; $D382 ← 0
                                                ; $D380 (相対 0) はここでは触らない。
                                                ;   エラーコード (参考書籍) を置いた
                                                ;   場合、メインが読むまで残す必要がある。
                                                ;   ATN ビットは wait_cmd 先頭で落とす。
                BRA     main_loop

;------------------------------------------------------------------------------
; cmd_rangetab / cmd_dispatch_table — 参考書籍の一覧に沿う区間ディスパッチ
;   cmd_rangetab は「下限 / 上限 / 表の索引起点」の 3 byte を区間ぶん並べ、
;   下限 0 で終端する。参考書籍のコマンドコードは連続していないので、
;   一覧に載る区間だけを並べ、どの区間にも入らないコードはコマンドコードの
;   誤り ($46) として返す。
;   継続コマンド $64 (参考書籍) は区間表の外で個別に受ける。
;   $3F は本実装が意図して持つ入口として区間に含める。
;------------------------------------------------------------------------------
cmd_rangetab:
                FCB     $01,$0D,0               ; コンソール ($01-$0D)
                FCB     $15,$20,13              ; グラフィック + 文字直線 ($15-$20)
                FCB     $29,$2C,25              ; キーボード ($29-$2C)
                FCB     $3D,$3F,29              ; タイマ ($3D/$3E) + $3F
        IFDEF SUBSYS_AV
                FCB     $45,$45,32              ; FM77AV のキーエンコーダ命令 ($45)
                FCB     $7F,$7F,33              ; FM77AV の共有 RAM 上の処理の実行 ($7F)
                FCB     $22,$22,34              ; FM77AV の表示ページ・処理ページの選択 ($22)
                FCB     $39,$39,35              ; FM77AV の描画範囲の設定と枠付きの矩形の塗り ($39)
                FCB     $36,$36,36      ; FM77AV の LINE 2 ($36。参考書籍)
                ; $41 SET RTC / $42 READ RTC は持たない (区間表に入れないので $46 を返す)。
                ;   RTC はキーエンコーダの配下にある部品で (参考書籍)、
                ;   サブ CPU から届く道は KEYBOARD CONTROL ($45) だけだが、その副命令は
                ;   $00〜$05 の 6 つで、RTC に触れるものが無い。よって実装しない
        ENDC
                FCB     $00                     ; 区間表の終り

;------------------------------------------------------------------------------
;--- 時刻カウンタの桁上限表 ------------------------------------------------------
;   周期処理の桁送り経路だけが参照する 4 byte。
nmi_climits:
                FCB     $32,$3C,$3C,$18         ; 桁上げ上限 (フレーム/秒/分/時)

cmd_dispatch_table:
                ; 索引 0-12 = コマンド $01-$0D (参考書籍)
                FDB     c01_ext           ; $01 INIT
                FDB     c02_ext                 ; $02 ERASE
                FDB     cmd_03_console          ; $03 PUT (コンソール文字出力)
                FDB     cmd_04_get              ; $04 GET (コンソール行入力 / 行エディタ)
                FDB     cmd_05_getc             ; $05 GETC (参考書籍)
                FDB     cmd_06_cget1            ; $06 Get Character Block 1 (文字)
                FDB     cmd_07_cput1            ; $07 Put Character Block 1 (文字)
                FDB     cmd_08_cget2            ; $08 Get Character Block 2 (属性+文字)
                FDB     cmd_09_cput2            ; $09 Put Character Block 2 (属性+文字)
                FDB     c0a_ext        ; $0A Get Buffer Address
                FDB     cmd_0b_tabset           ; $0B TAB SET
                FDB     cmd_0c_cursor           ; $0C CONSOLE CONTROL
                FDB     c0d_ext             ; $0D ERASE 2
                ; 索引 13-24 = コマンド $15-$20 (参考書籍)
                FDB     line15_entry             ; $15 LINE
                FDB     chain16_entry            ; $16 CHAIN
                FDB     point_entry            ; $17 POINT (点列描画)
                FDB     paint_entry            ; $18 PAINT 本体
                FDB     symbol_entry            ; $19 SYMBOL (本体は ROM 末尾)
                FDB     chgcol_entry           ; $1A CHANGE COLOR (矩形内色置換)
        IFDEF SUBSYS_TB
                FDB     c_nocode                ; $1B Type-B の位置では受け付けない ($46)
                FDB     pb1_fast        ; $1C PUT BLOCK 1
                FDB     gb2b_entry              ; $1D Type-B の位置の矩形読出し 2
        ELSE
                FDB     getblock1_entry        ; $1B GET BLOCK 1
                FDB     pb1_fast        ; $1C PUT BLOCK 1
                FDB     getblock2_entry        ; $1D GET BLOCK 2
        ENDC
        IFDEF SUBSYS_TB
                FDB     tb1e_entry              ; $1E Type-B の位置の矩形書込み 2
        ELSE
                FDB     pb2_fast        ; $1E PUT BLOCK 2
        ENDC
                FDB     gcursor_entry           ; $1F GRAPHIC CURSOR (本体は ROM 末尾)
                FDB     chline_entry           ; $20 CHARACTER LINE
                ; 索引 25 以降 = コマンド $29-$2C (参考書籍)
                FDB     cmd_29_inkey            ; $29 INKEY
                FDB     funkey_entry            ; $2A Define String of PF
                FDB     getpf_entry             ; $2B Get String of PF
                FDB     intctl_entry           ; $2C Interrupt Control
                ; 末尾 3 件 = コマンド $3D/$3E (参考書籍) と $3F
                FDB     settimer_entry          ; $3D SET TIMER
                FDB     readtimer_entry        ; $3E READ TIMER
                FDB     cmd_3f_bytecode         ; $3F バイトコード試験用の入口
        IFDEF SUBSYS_AV
                ; 索引 32-36 = FM77AV の Type-A / Type-B の位置に置く ROM だけが持つ命令
                FDB     c45_entry               ; $45 キーエンコーダ命令
                FDB     c7f_entry       ; $7F 共有 RAM 相対 3 ($D383) からの処理を JSR で実行
                FDB     c22_entry               ; $22 表示ページ・処理ページの選択
                FDB     c39_entry               ; $39 枠付きの矩形の塗り
                FDB     line15_entry    ; $36 LINE 2 ($15 と同じ経路で引く)
;--- hook_tmpl — 割込みフックテーブルの初期値 (リセットで $D2A8- へ写す) ---------------
hook_tmpl:
                JMP     hdlr_SWI3       ; $D2A8 SWI3
                JMP     hdlr_SWI2       ; $D2AB SWI2
                JMP     hdlr_FIRQ       ; $D2AE FIRQ
                JMP     hdlr_IRQ        ; $D2B1 IRQ
                JMP     hdlr_SWI        ; $D2B4 SWI
                JMP     hdlr_NMI        ; $D2B7 NMI
;--- c7f_entry — $7F 共有 RAM 相対 3 からの処理の実行 -----------------------
;   転送された処理の中では取り消しの IRQ を受けない (受けるかどうかは転送された処理が
;   決める。受けたときは $D402 を読み印を置いて戻るだけ)。
;   渡す前にワーク RAM へは何も書かない
c7f_entry:
                ORCC    #$10
                JMP     SH_PARAM
        ENDC

;--- pt_fill — スパン塗り: D=塗れる区間左端 → [D..右境界-1] を塗る -------------
;   戻り値 D = 右境界 X。水平 1 行は draw_line の hline_blit 高速経路で一括。
;   draw_line は自前で直接ページを確立・復元するため、塗りつぶし側の直接ページ
;   ($D0) を退避するラッパは要らない。SUBSYS_AV の組み立てでは、ラインドロワで
;   引く avd_nc で塗る (Type-B の位置は 4 回、Type-A の位置は 1 回。区間は描画範囲の中
;   なので切り取らない)。
pt_fill:
                STD     <WK_LN_X0
                JSR     pt_scan_rb              ; 右境界
                PSHS    A,B
                SUBD    #1
                BSR     pt_hln
                PULS    A,B,PC          ; D = 右境界

;--- pt_hln — D を右端、PT_CY を Y として水平の区間を 1 本引く --------------------
;   pt_fill と pt_lov が共用する。
pt_hln:
                STD     <WK_LN_X1
                LDD     <PT_CY
                STD     <WK_LN_Y0
                STD     <WK_LN_Y1
        IFDEF SUBSYS_AV
                JMP     avd_nc          ; 区間は描画範囲の中 (切り取り不要)
        ELSE
                BRA     draw_line
        ENDC

        IFNDEF SUBSYS_AV
;------------------------------------------------------------------------------
; do_rect_pre — 矩形描画 (枠/塗潰し)。WK_RECT_X0/Y0/X1/Y1 と WK_COLOR を設定済で呼ぶ
;   (line15_entry 用)。入力: A = モード (0=枠、1=塗潰し)
;   フレームは A (モード) のみ退避。本体は A/B しか使わず、呼出元はレジスタ保存に
;   依存しない。描画中に NMI が重なった際のスタック最深部を浅くする (常駐データ域の保全)。
;------------------------------------------------------------------------------
do_rect_pre:
                PSHS    A
dr_core:
                ; mode 判定 (入口 A、スタック先頭に保存済)
                LDA     ,S
                TSTA
                BEQ     dr_outline

                ;--- 塗潰し: Y0 から Y1 まで各行で水平ライン ---
                LDD     <WK_RECT_Y0
                STD     <WK_RECT_CY
dr_fill_loop:
                ; 1 行 (X0,CY)-(X1,CY) を draw_line で描画 (辺表の 1 行と同じ形)
                LDU     #dr_filltab
                BSR     dr_edge1
                ; CY == Y1 なら終了。そうでなければ Y1 の側へ 1 進める
                ;   (Y0<=Y1 / Y0>Y1 の両方に対応。比較結果はそのまま向きに使う)
                LDD     <WK_RECT_CY
                CMPD    <WK_RECT_Y1
                BEQ     dr_end
                BHI     dr_fill_dec             ; CY > Y1 → 減算方向
                ADDD    #1
                BRA     dr_fill_store
dr_fill_dec:
                SUBD    #1
dr_fill_store:
                STD     <WK_RECT_CY
                BRA     dr_fill_loop

dr_outline:
                ;--- 枠: 4 辺を draw_line で描画 ---
                ;   4 辺はどれも「4 隅のうち 2 点を線分の端点にする」だけの違いしか
                ;   ないので、辺ごとの変位を表 (dr_edgetab) に置いて 1 本のループで
                ;   引く。描く順序は上辺 → 下辺 → 左辺 → 右辺。
                LDU     #dr_edgetab
dr_edge:
                BSR     dr_edge1
                CMPU    #dr_edgetab+16          ; 4 辺ぶん
                BCS     dr_edge
dr_end:
                PULS    A,PC

;--- dr_edge1 — 表の 1 行ぶんの線分を引く (U = 表の位置、4 byte 消費) ---------
;   4 隅 (WK_RECT_X0 からの並び) のどのワード (2 バイト) を線分の X0,Y0,X1,Y1 に使うかを
;   表の 4 byte が指す。枠の 4 辺も塗潰しの 1 行も同じ形なので共用する。
dr_edge1:
                LDX     #WK_RECT_X0             ; 4 隅の並び (X0,Y0,X1,Y1,CY)
                LDY     #WK_LN_X0               ; 線分の端点 (X0,Y0,X1,Y1)
dr_word:
                LDB     ,U+                     ; 表の変位 = どの隅のワードを使うか
                LDD     B,X
                STD     ,Y++
                CMPY    #WK_LN_Y1+2             ; 4 ワードそろうまで
                BCS     dr_word
        ENDC

;------------------------------------------------------------------------------
; draw_line — 直線 (参考書籍)
;   入力: WK_LN_X0, WK_LN_Y0, WK_LN_X1, WK_LN_Y1, WK_COLOR
;   主軸 (長い方の軸) に沿って 1 ドットずつ進め、誤差の累積が従軸の長さを
;   下回るたびに従軸を 1 つ進める。水平線は hline_blit へ。
;------------------------------------------------------------------------------
; 直線のワーク (直接ページ $D0 の空き変位に割当。実効アドレス $D064-$D06D)
;   直接ページ $D3 の変位 $40- は機能キー定義表 (1 件 16 byte × 11、キー番号 9 の
;     枠) の実体である。定義表は利用者が設定した内容を保持し続ける器であり、
;     描画中だけ生きるスクラッチとは時間排他が成り立たない (線を 1 本引くだけで
;     定義が壊れる)。よって描画スクラッチは定義表の外 = 直接ページ $D0 の空き
;     変位へ置く。
LN_DX           EQU     $64             ; 2: |X1-X0|
LN_DY           EQU     $66             ; 2: |Y1-Y0|
LN_SX           EQU     $68             ; 2: X の進み (+1 / -1)
LN_SY           EQU     $6A             ; 2: Y の進み (+1 / -1)
LN_ERR          EQU     $6C             ; 2: 誤差の累積

        IFNDEF SUBSYS_AV                        ; SUBSYS_AV の組は $15 / $16 / $18 をラインドロワで引く
draw_line:
                PSHS    A,B,DP,X,Y,U
                ; 水平線 (Y0==Y1) はバイト単位ブリットの高速経路へ (hline_blit)。
                LDD     <WK_LN_Y0
                CMPD    <WK_LN_Y1
                LBEQ    hline_blit
dl_general:
                LDD     <WK_LN_X1
                SUBD    <WK_LN_X0
                BSR     dl_abs                  ; D = |D|、X = 進み (±1)
                STD     <LN_DX
                STX     <LN_SX
                LDD     <WK_LN_Y1
                SUBD    <WK_LN_Y0
                BSR     dl_abs
                STD     <LN_DY
                STX     <LN_SY
                LDD     <WK_LN_X0
                STD     <WK_PX_X
                LDD     <WK_LN_Y0
                STD     <WK_PX_Y
                LDD     <LN_DX
                CMPD    <LN_DY
                BCS     dl_ymaj
                ; --- X 主軸: 打点数 = |dx|+1、誤差の初期値 = |dx|/2 ---
                LSRA
                RORB
                STD     <LN_ERR
                LDX     <LN_DX
                LEAX    1,X
dlx_lp:
                JSR     pset_internal
                LDD     <LN_ERR
                SUBD    <LN_DY
                BCC     dlx_e                   ; 誤差が従軸の長さ以上 → Y は据置き
                ADDD    <LN_DX
                STD     <LN_ERR
                LDD     <WK_PX_Y
                ADDD    <LN_SY
                STD     <WK_PX_Y
                BRA     dlx_x
dlx_e:
                STD     <LN_ERR
dlx_x:
                LDD     <WK_PX_X
                ADDD    <LN_SX
                STD     <WK_PX_X
                LEAX    -1,X
                BNE     dlx_lp
                BRA     dl_end
dly_e:
                STD     <LN_ERR
dly_y:
                LDD     <WK_PX_Y
                ADDD    <LN_SY
                STD     <WK_PX_Y
                LEAX    -1,X
                BNE     dly_lp
dl_end:
                ; 終点座標を記録 (LINE 続き $16 のため)
                LDD     <WK_LN_X1
                STD     <WK_LAST_X
                LDD     <WK_LN_Y1
                STD     <WK_LAST_Y
                PULS    A,B,DP,X,Y,U,PC
dl_ymaj:
                ; --- Y 主軸: 打点数 = |dy|+1、誤差の初期値 = |dy|/2 ---
                LDD     <LN_DY
                LSRA
                RORB
                STD     <LN_ERR
                LDX     <LN_DY
                LEAX    1,X
dly_lp:
                JSR     pset_internal
                LDD     <LN_ERR
                SUBD    <LN_DX
                BCC     dly_e
                ADDD    <LN_DY
                STD     <LN_ERR
                LDD     <WK_PX_X
                ADDD    <LN_SX
                STD     <WK_PX_X
                BRA     dly_y
;--- dl_abs — D を絶対値にし、X に符号 (+1 / -1) を返す -------------------------
dl_abs:
                LDX     #1
                TSTA
                BPL     dla_ret
                LEAX    -2,X                    ; X = -1
                NEGA                            ; D = -D (NEGB の借りを上位へ)
                NEGB
                SBCA    #0
dla_ret:
                RTS
        ENDC

;------------------------------------------------------------------------------
; getpf_entry — Get String of PF ($2B, GSTRPF)。
;   $D383=NO。PFキー定義テーブル (KB_PF_BASE+NO*16) から 16byte を $D384〜$D393 へ転送。
;   先頭バイト = 文字数。
;------------------------------------------------------------------------------
getpf_entry:
                BSR     pf_chk_no               ; NO の範囲検査 (参考書籍)
                BSR     pf_tbl_x                ; FIRQ マスク + X = 定義テーブル先頭
                LDA     ,X+             ; 先頭バイト = bit7 割込み制御フラグ | N (文字数、X→[1])
                ANDA    #$0F            ; N だけを返す
                STA     1,U                     ; → $D384
                LEAU    2,U             ; 転送先 ($D385〜)
                LDB     #15                     ; 残り 15byte
gp_cpy:
                LDA     ,X+
                STA     ,U+
                DECB
                BNE     gp_cpy
                ANDCC   #$BF                    ; FIRQ マスク解除
                RTS

;------------------------------------------------------------------------------
; pf_chk_no / pf_chk_len — PF キーコマンドのパラメータ範囲検査
;   $2A Define String of PF / $2B GET String of PF は
;   PF キー番号 NO = 1〜10、定義文字数 N = 0〜15。範囲を外れた時は
;   参考書籍の該当コードを共有 RAM 相対 0 へ置き、呼出元のコマンド処理を
;   実行せずにディスパッチへ戻す (応答は必ず返るので待ちは解ける)。
;------------------------------------------------------------------------------
pf_chk_no:
                LDA     #$44                    ; PF キー番号の誤り (参考書籍)
                LDB     0,U                     ; NO
                DECB
                CMPB    #9                      ; 1〜10 か
                BHI     pf_chk_ng
                RTS
pf_chk_len:
                LDA     #$42                    ; 文字数の誤り (参考書籍)
                LDB     1,U                     ; N
                CMPB    #15                     ; 0〜15 か
                BLS     pf_chk_ok
pf_chk_ng:
                LEAS    2,S                     ; 呼出元のコマンド処理を中止
                STA     -3,U
pf_chk_ok:
                RTS

;------------------------------------------------------------------------------
; funkey_entry — Define String of PF ($2A, FUNKEY)。
;   $D383=NO (PFキー番号 1-10), $D384=N (文字数 0-15), $D385〜=定義文字列。
;   PFキー定義テーブル先頭 = KB_PF_BASE + NO*16 (16byte/キー)。先頭バイトは
;   文字数 N (参考書籍の N = 0〜15)。テーブルをクリア後 N 文字を格納。
;   割込みの許可 (+0 の bit7) は保つ。設定中は FIRQ マスク。
;------------------------------------------------------------------------------
;--- pf_tbl_x — FIRQ をマスクし、X = 機能キー定義テーブルの先頭 ---------------
;   $D383 の NO 番の 16 byte の枠を指す ($2A / $2B 共用)。A,B 破壊。
pf_tbl_x:
                ORCC    #$40                    ; FIRQ マスク
                LDB     0,U                     ; NO
                LDA     #16
                MUL                             ; D = NO*16
                ADDD    #KB_PF_BASE
                TFR     D,X                     ; X = PF 定義テーブル先頭
                RTS

funkey_entry:
                BSR     pf_chk_no               ; NO の範囲検査 (参考書籍)
                BSR     pf_chk_len              ; N の範囲検査 (参考書籍)
                BSR     pf_tbl_x                ; FIRQ マスク + X = 定義テーブル先頭
                LDA     ,X
                ANDA    #$80            ; +0 の bit7 (割込み制御フラグ) は保つ
                ORA     1,U             ;   (参考書籍)
                PSHS    X                       ; テーブル先頭退避
                LDB     #16
fk_clr:
                CLR     ,X+                     ; 16byte クリア
                DECB
                BNE     fk_clr
                PULS    X                       ; テーブル先頭復帰
                STA     ,X+             ; テーブル[0] = フラグ | 文字数、X→[1]
                ANDA    #$0F
                BEQ     fk_done
                TFR     A,B             ; B = N (文字数)
                LEAU    2,U             ; 定義文字列ソース
fk_cpy:
                LDA     ,U+
                STA     ,X+
                DECB
                BNE     fk_cpy
fk_done:
                ANDCC   #$BF                    ; FIRQ マスク解除

;==============================================================================
; CONSOLE の機能キー行表示 (CONSOLE 文の第 3 引数。
; 参考書籍の相対 8 (FD))
;==============================================================================

;--- pf_show — 機能キー行 (画面の下から 2 行) を描く --------------------------------
;   $01 の相対 8 (FD) が非 0 のときだけ描き、0 なら何もしない (参考書籍)。参考書籍が定めない
;   細部は次のとおり定める:
;     ・上の行に 1〜5、下の行に 6〜10 を置く。1 個の欄は桁数の 1/5 (80 桁: 16、
;       40 桁: 8) で、定義文字列を欄の左端から欄幅まで置き、余りは空白で埋める
;       (前の定義が残らない)。欄幅を超える文字は描かない。
;     ・文字は現在の文字色。定義中の制御コードは字形表のまま描く。
;   呼出: $01 (消去の有無を問わず)、$2A (定義変更)、全画面消去 (vram_fill_bg) の出口。
;   A,B,X,Y,U を保存する。
pf_show:
                TST     <WK_PF_SHOW
                BEQ     cwk_ret                 ; 表示しない指定なら何もせず戻る
                PSHS    A,B,X,Y,U
                LDB     #1                      ; B = 機能キー番号 (1〜10)
pfs_key:
                PSHS    B
                LDA     <WK_SCREEN_H
                SUBA    #2                      ; 上の行 = 行数-2
                DECB                            ; B = 番号-1 (0〜9)
                CMPB    #5
                BLO     pfs_row
                INCA                            ; 6〜10 は下の行
                SUBB    #5
pfs_row:
                STA     <CB_DROW
                LDA     #16                     ; 欄幅 (80 桁)
                TST     <WK_CHAR_DBL
                BEQ     pfs_wid
                LSRA                            ; 40 桁は 8
pfs_wid:
                PSHS    A                       ; ,S = 欄幅 / 1,S = 番号
                MUL                             ; B = 欄幅 × 欄位置 = 開始桁
                STB     <CB_DCOL
                LDB     1,S                     ; 番号
                LDA     #16
                MUL
                ADDD    #KB_PF_BASE
                TFR     D,U             ; U → 定義 (先頭 = bit7 割込み制御フラグ | 文字数)
                LDA     ,U+
                ANDA    #$0F            ; A = 残り文字数
                LDB     ,S                      ; B = 残り欄幅
pfs_cell:
                PSHS    A,B
                LDA     #' '
                TST     ,S                      ; 文字が残っていれば
                BEQ     pfs_put
                DEC     ,S
                LDA     ,U+                     ;   その文字を
pfs_put:
                LDB     <WK_TXT_COL              ; 属性 = 現在の文字色
                JSR     cb_drawcell             ; (CB_DCOL, CB_DROW) へ描く。A/B/X 保存
                INC     <CB_DCOL
                PULS    A,B
                DECB
                BNE     pfs_cell
                PULS    A,B                     ; B = 番号
                INCB
                CMPB    #11
                BLO     pfs_key
                PULS    A,B,X,Y,U,PC

;--- con_page_wait / con_wait_key — 表示動作の停止と、その解除待ち ------------
;   ページウェイト (コンソール制御の CF bit3) が有効なとき、画面の最終位置への
;     出力と最終行での LF でベルを鳴らして止まり、[BREAK] と [PF] 以外のキーで
;     再開する。
;   [ESC] によるプットウェイトも解除条件は同じなので、解除待ちだけを
;     con_wait_key として共用する。
;   [BREAK] はキーバッファへは入らず、[PF] キーも文字列の生成か割込みの発生の
;     どちらかになり、PF のキーコードそのものはバッファへ入らない。よって
;     バッファから取り出せたキーはいずれも解除キーである。停止するときは
;     ベルを鳴らす ($D403 の読出し)。
;   呼出元 (putchar 経路) の B / X を壊さないこと。kb_dequeue は X を触らない。
con_page_wait:
                LDA     <WK_CSCTL       ; 表示制御ワーク (CF の現在値)
                BITA    #$08                    ; bit3 = ページウェイト
                BEQ     cwk_ret
                TST     >$D403          ; ベルを鳴らして停止する
con_wait_key:
                PSHS    B
cwk_lp:
                BSR     kb_dequeue              ; A = キー / B = 有無
                TSTB
                BEQ     cwk_lp                  ; まだ押されていない → 待つ
                PULS    B
cwk_ret:
                RTS


;==============================================================================
; 割込ハンドラ + キーバッファ初期化
;==============================================================================

;------------------------------------------------------------------------------
; kb_flush — キーバッファを空にする (count=0, ptr=KB_BUF_LO)
;   DP=$D0 の文脈 (コマンド処理) から呼ぶ。A,B を破壊。
;------------------------------------------------------------------------------
kb_flush:
                CLR     <KB_COUNT
                LDD     #KB_BUF_LO
                STD     <KB_RDPTR
                STD     <KB_WRPTR
                RTS

;------------------------------------------------------------------------------
; cmd_0a_getbufadr — Get Buffer Address ($0A)
;   参考書籍: コンソール入力位置 (カーソル座標) を返す。
;   $D383 = カーソル列 (X), $D384 = カーソル行 (Y)。メイン側はこの戻り値で
;   コンソールカーソルを追跡し、後続のカーソル制御に用いる。
;------------------------------------------------------------------------------
                ; 本体は c0a_ext (コード域)。行はスクロール範囲相対で返す。
                ; この区画は $E1FF ピンまでで独立しており、他の区画へは影響しない。

;------------------------------------------------------------------------------
; cmd_0b_tabset — TABSET ($0B、動作観察)
;   $D383〜 10 byte = 80 桁ぶんのタブストップビットマップを、TAB_TABLE
;   (サブ側ワーク RAM の配置) へ複写する (メイン CPU 側の BASIC が起動時に発行する)。
;------------------------------------------------------------------------------
cmd_0b_tabset:
                TFR     U,X             ; X = 共有 RAM 相対 3 ($D383)
                LDU     #TAB_TABLE      ; U はメインループの先頭で作り直す
tabset_loop:
                LDD     ,X++                    ; 2 byte ずつ 5 回 (10 byte)
                STD     ,U++
                CMPX    #$D383+10
                BNE     tabset_loop
                RTS

;------------------------------------------------------------------------------
; cmd_29_inkey — $29 INKEY (参考書籍)
;   入力 : 相対 3 = CF (bit0 = ウェイト / bit1 = RESET)
;   出力 : 相対 3 = キーコード、相対 4 = 有無 ($00=無 / $01=有)
;------------------------------------------------------------------------------
cmd_29_inkey:
                LDA     0,U                     ; CF
                BITA    #$02                    ; bit1 = KEY レジスタを空にする
                BEQ     i29_get
                BSR     kb_flush
i29_get:
                BSR     kb_dequeue              ; A=キー, B=有無
                TSTB
                BNE     i29_done
                LDA     >SH_PARAM
                LSRA                            ; C = bit0 = ウェイトフラグ
                BCS     i29_get                 ; 1 = キーが押されるまで待ち続ける
i29_done:
                STD     >SH_PARAM               ; $D383 = キーコード / $D384 = 有無
                RTS

;==============================================================================
; line15_entry — cmd $15 LINE 本体
;   入口のパラメータ配置:
;     $D383 = 色 (下位 3bit), $D384 = 描画ファンクション (0-4。1=PRESET は背景色で
;             描画、>4 は不正として描かない。2-4 の論理合成は PSET 扱いの簡略),
;     $D385-6 = X0, $D387-8 = Y0, $D389-A = X1, $D38B-C = Y1,
;     $D38D = 形状 (0=直線 / 1=矩形枠 / それ以外=塗潰し矩形)
;   範囲外座標の扱い: 端点座標が 1 ワードでも画面外なら、この命令は
;   何も描かずに正常終了する (応答フラグは通常どおり落とす)。画面内は X が
;   0-639、Y が 0-199 で、負数は符号無し比較で巨大値となるため同じ判定で外れる。
;   直線・矩形枠・塗潰し矩形の 3 形状に共通で、折線 ($16)・点列 ($17)・
;   塗りつぶしの種点 ($18) も同じ規則で揃える。画面内へ丸めて描く実装は
;   端点を境界へ寄せる分だけ線の傾きが変わり、画面外へ伸びる線を引く
;   プログラムに本来無い線が現れるため採らない。座標が範囲内に限られるので、
;   VRAM 外アドレスへの打点流出 (共有 RAM $C0xx 破壊) は構造的に起きない。
;==============================================================================
;--- dbl_nibble — 4bit ニブルを各ビット倍化した 8bit へ変換するテーブル -------
;   bit3→bit7,6 / bit2→bit5,4 / bit1→bit3,2 / bit0→bit1,0
;   40桁 (16px倍幅) 文字描画 (dc_dbl_loop) で使用する。参照は絶対アドレスなので
;   置き場所を選ばない。
        IFNDEF SUBSYS_TB                        ; Type-B の位置の 40 桁は tb_glyph が描く
dbl_nibble:
                FCB     $00,$03,$0C,$0F,$30,$33,$3C,$3F
                FCB     $C0,$C3,$CC,$CF,$F0,$F3,$FC,$FF
        ENDC

;--- pb1_fntbl — $1C 描画ファンクション別 (A,D) 定数表 (4 byte/fn × 5) ---------
;   1 行 = [A1,D1,A0,D0] (色ビット 1 の面 / 0 の面)。pb1_decode が描画色の
;   各プレーンビットで選択し PBF_CB../PBF_DB.. へ展開する。
;   書込式は new = old ^ (((old & A) ^ D ^ old) & m) (m = パターン∧端マスク):
;     (A,D)=(00,FF)=セット / (00,00)=クリア / (FF,00)=保持 / (FF,FF)=反転
pb1_fntbl:
                FCB     $00,$FF,$00,$00         ; fn0 PSET (色置換)
                FCB     $00,$FF,$00,$00         ; fn1 PRESET (色は背景色へ差替済)
                FCB     $00,$FF,$FF,$00         ; fn2 OR (色ビット 0 の面は保持)
                FCB     $FF,$00,$00,$00         ; fn3 AND (色ビット 1 の面は保持)
                FCB     $FF,$FF,$FF,$00         ; fn4 XOR (色ビット 1 の面のみ反転)

;------------------------------------------------------------------------------
; kb_dequeue — リングバッファから 1 キー取り出す ($29 INKEY / $04 GET 共用)
;   出力: B!=0 かつ A=キーコード (キーあり) / B=0 (キー無し)。U 破壊。
;------------------------------------------------------------------------------
kb_dequeue:
                LDB     <KB_COUNT
                BEQ     kbd_none                ; count==0 → キー無し
                DEC     <KB_COUNT
                LDU     <KB_RDPTR
                LDA     ,U+                     ; A = キーコード、読込ポインタ前進
                CMPU    #KB_BUF_HI
                BLO     kbd_nowrap              ; 終端+1 に届いていなければそのまま
                LDU     #KB_BUF_LO              ; wrap
kbd_nowrap:
                STU     <KB_RDPTR
                LDB     #$01                    ; B=1 (キーあり)
                RTS
kbd_none:
                CLRA
                CLRB                            ; A=0, B=0 (キー無し)
                RTS

;--- gc_arrow — 移動キー 1 個ぶんの移動 (A = $1C〜$1F) ------------------------
;   $1C = → / $1D = ← / $1E = ↑ / $1F = ↓ (参考書籍のキャラクタコード)。
;   bit1 が軸 (0 = X / 1 = Y)、bit0 と bit1 の一致が向き (一致 = 増加) を表す。
;   画面の外へ出る指示は動かさない (参考書籍の座標範囲を保つ)。
gc_arrow:
                LDX     #WK_SYM_X
                LDY     #640
                BITA    #$02
                BEQ     gca_axis
                LDX     #WK_SYM_Y
                LDY     #200
gca_axis:
                ANDA    #$03
                DECA
                CMPA    #1
                BHI     gca_pos                 ; $1C (→) / $1F (↓) = 増加方向
                LDA     #$FF                    ; $1D (←) / $1E (↑) = 減少方向
                BRA     gca_go
gca_pos:
                CLRA
gca_go:
                STY     <WK_TMP                  ; 画面内の上限 (640 / 200)
                LDB     <WK_SYM_MAGX             ; 1 ステップの移動ドット数
                TSTA
                BPL     gca_add
                NEGB                            ; D = -ステップ (A = $FF が符号拡張)
gca_add:
                ADDD    ,X
                BMI     gca_ret                 ; 0 未満へは動かさない
                CMPD    <WK_TMP
                BCC     gca_ret                 ; 上限以上へは動かさない
                STD     ,X
gca_ret:
                RTS

;==============================================================================
; gcursor_entry — $1F GRAPHIC CURSOR (参考書籍)
;
;   オペレータの指示した座標を読取る。
;   コマンド & パラメータ (相対値):
;     3      CL カーソルの表示色 (0〜7)
;     4      N  要求する座標数 (1〜10)
;     5, 6   X  初期座標 X (0〜639)
;     7, 8   Y  初期座標 Y (0〜199)
;   応答データ (相対値):
;     0            E  エラーコード
;     1, 2         —  不定
;     3+4(n-1) …   Xn 指定座標 X (16 ビット)
;     5+4(n-1) …   Yn 指定座標 Y (16 ビット)
;
;   オペレータ制御:
;     ・移動キー: カーソルを 1 ステップ (1〜10 ドット) 指定方向へ動かす
;     ・数字キー: 1 ステップの移動ドット数を指定する。0 は 10 ドット、
;       1〜9 はその数字のドット数。初期値は 1
;     ・改行キー: 座標を読取る。指定された数の座標を読取ると終了する
;   [SHIFT] 併用は未対応 (1 ステップの移動として扱う)。
;
;   カーソルは＋印 (縦横 13 ドットの十字) で表示する。表示前に下地の色を退避し、
;   移動・終了の前に退避した色へ戻すので、画面は本コマンドの前後で変わらない。
;   画面外へ掛かるドットは打たない (座標範囲は参考書籍のとおり)。
;   ワークは SYMBOL のスクラッチを流用する (別コマンドで同時実行しない)。
;==============================================================================
GC_SAVE         EQU     $D100           ; 25: ＋印の下地の色 (退避と復元)。塗りの種スタック
                                        ;    ($D100-$D263) の下部と重畳するが別コマンドで同時に走らない

gcursor_entry:
                LDA     1,U                     ; 相対 4 = 要求する座標数 N
                BEQ     gc_nerr
                CMPA    #10
                BHI     gc_nerr                 ; 参考書籍の N は 1〜10
                STA     <WK_SYM_MAGY             ; 残りの座標数
                LDA     0,U                     ; 相対 3 = カーソルの表示色
                ANDA    #$07
                STA     <WK_SYM_BITM
                LEAX    2,U             ; 相対 5 以降 = 初期座標 (X, Y)
                LDY     #WK_SYM_X
                JSR     gr_xy                   ; 画面内判定つき転記
                LBCC    gr_err_coord            ; 参考書籍の座標範囲
                LDA     #1
                STA     <WK_SYM_MAGX             ; 1 ステップの移動ドット数 (初期値 1)
                TFR     U,Y             ; 応答データは相対 3 から 4 byte/点
                STY     <WK_SYM_PTR
gc_loop:
                BSR     gc_show                 ; ＋印を表示
gc_wait:
                BSR     kb_dequeue              ; A = キーコード / B ≠ 0 で取得
                TSTB
                BEQ     gc_wait
                PSHS    A
                BSR     gc_hide                 ; ＋印を消す (下地の色へ戻す)
                PULS    A
                CMPA    #$0D                    ; 参考書籍 改行キー = 座標の読取り
                BEQ     gc_take
                CMPA    #$1C                    ; 参考書籍 移動キー ($1C〜$1F)
                BCS     gc_digit
                CMPA    #$1F
                BHI     gc_digit
                BSR     gc_arrow
                BRA     gc_loop
gc_nerr:
                LDA     #$41                    ; 座標数の誤り (参考書籍)
                JMP     cmd_err
gc_digit:
                SUBA    #$30                    ; 参考書籍 数字キー = ステップ数
                BCS     gc_loop
                CMPA    #9
                BHI     gc_loop
                BNE     gc_step
                LDA     #10                     ; [0] は 10 ドット
gc_step:
                STA     <WK_SYM_MAGX
                BRA     gc_loop
gc_take:
                LDY     <WK_SYM_PTR              ; 指定座標を応答データへ積む
                LDD     <WK_SYM_X
                STD     ,Y++
                LDD     <WK_SYM_Y
                STD     ,Y++
                STY     <WK_SYM_PTR
                DEC     <WK_SYM_MAGY             ; 要求された数だけ読取ったら終り
                BNE     gc_loop
                RTS

;--- gc_show / gc_hide — ＋印の表示と消去 -------------------------------------
;   横 (GC_ARMX*2+1) ドットと縦 (GC_ARMY*2+1) ドット (中心は共有) の十字。
;   画面は横 640 ドット・縦 200 ドットで 1 ドットの縦横比が 2 対 1 に近いので、
;   横の腕を縦の倍に取ると画面上では縦横が同じ長さに見える。表示のときは各ドットの
;   下地の色を GC_SAVE へ退避してからカーソル色で打ち、消去のときは退避した色で打つ。
GC_ARMX         EQU     8                       ; ＋印の腕 (横方向、中心から)
GC_ARMY         EQU     4                       ; 同 (縦方向)
gc_show:
                CLRA
                BRA     gc_plus
gc_hide:
                LDA     #$FF
gc_plus:
                STA     <WK_SYM_PAT              ; 0 = 表示 / $FF = 消去
                LDY     #GC_SAVE
                LDD     <WK_SYM_X                ; 横棒 (X-8, Y) から 17 ドット
                SUBD    #GC_ARMX
                STD     <WK_PX_X
                LDD     <WK_SYM_Y
                STD     <WK_PX_Y
                LDA     #GC_ARMX*2+1
                STA     <WK_SYM_CNT
gcp_h:
                BSR     gc_dot
                INC     <WK_PX_X+1              ; X+1 (16 ビット)
                BNE     gcp_hn
                INC     <WK_PX_X
gcp_hn:
                DEC     <WK_SYM_CNT
                BNE     gcp_h
                LDD     <WK_SYM_Y                ; 縦棒 (X, Y-4) から 9 ドット
                SUBD    #GC_ARMY
                STD     <WK_PX_Y
                LDD     <WK_SYM_X
                STD     <WK_PX_X
                LDA     #GC_ARMY*2+1
                STA     <WK_SYM_CNT
gcp_v:
                LDA     <WK_SYM_CNT
                CMPA    #GC_ARMY+1
                BEQ     gcp_vskip               ; 中心は横棒で打ってある
                BSR     gc_dot
gcp_vskip:
                INC     <WK_PX_Y+1              ; Y+1 (16 ビット)
                BNE     gcp_vn
                INC     <WK_PX_Y
gcp_vn:
                DEC     <WK_SYM_CNT
                BNE     gcp_v
                RTS

;==============================================================================
; パターン合成ルーチン群 (通常配置。アドレスは固定しない)
;   コマンドディスパッチ経由の描画プリミティブから使う、ワーク RAM のパターンを
;   VRAM へ 1 byte 単位で合成する共通処理。
;   呼出規約: X=VRAM 先頭, U=対象アドレス, カウントとパターンは DP=$D0 のワーク,
;   $D409=VRAM プレーン選択ゲート。呼出側が DP=$D0 を整えて呼ぶ。
;==============================================================================
;--- 端バイトマスク表 (put-block ブリット共用) ---------------------------------
;   参照は `LDX #pbf_tbl_l` の即値のみで位置に依存しない。
pbf_tbl_l:                                      ; 左端: $FF >> (X0&7)
                FCB     $FF,$7F,$3F,$1F,$0F,$07,$03,$01
pbf_tbl_r:                                      ; 右端: 上位 (X1&7)+1 bit
                FCB     $80,$C0,$E0,$F0,$F8,$FC,$FE,$FF

;------------------------------------------------------------------------------
; chgcol_entry — Change Color 本体 (cmd_1a_chgcol から JMP)
;------------------------------------------------------------------------------
chgcol_entry:
                BSR     rect_from_param         ; 参考書籍の相対 3〜10 = 対角線座標
                LDA     8,U                     ; 相対 11 = 変更するカラーの数 N
                DECA
                CMPA    #7
                LBHI    gr_err_ncol             ; 参考書籍の N は 1〜8
                LEAX    9,U             ; 相対 12 以降 = (旧, 新) の組が N 組
cc_pair:
                LDA     ,X+                     ; 相対 12+2(n-1) = 旧カラーコード
                STA     <WK_CC_OLDCOL
                LDA     ,X+                     ; 相対 13+2(n-1) = 新カラーコード
                ANDA    #$07
                STA     <WK_COLOR
                BSR     cc_scan                 ; 枠内の旧カラーを新カラーへ置換
                DEC     >SH_PARAM+8             ; 残りの組数
                BNE     cc_pair
                RTS

;--- gc_dot — ＋印の 1 ドット (Y = 下地の色の退避位置。呼出ごとに 1 進む) ------
gc_dot:
                LDD     <WK_PX_X
                CMPD    #640
                BCC     gcd_skip                ; 画面の外へは打たない
                LDD     <WK_PX_Y
                CMPD    #200
                BCC     gcd_skip
                LDA     <WK_SYM_PAT
                BNE     gcd_restore
                BSR     get_pixel_color         ; A = 下地の色
                STA     ,Y
                LDA     <WK_SYM_BITM             ; カーソルの表示色
                BRA     gcd_put
gcd_skip:
                LEAY    1,Y
cc_done:
                RTS
gcd_restore:
                LDA     ,Y                      ; 退避した下地の色
gcd_put:
                STA     <WK_COLOR
                LEAY    1,Y
                JMP     pset_internal

;------------------------------------------------------------------------------
; rect_from_param — 矩形コマンドの共通前段。共有 RAM のパラメータ 4 組
;   ($D383-$D38A) を作業域 WK_RECT_X0/Y0/X1/Y1 へ写す。
;   矩形を扱うコマンド (色置換・図形の取出し 2 形式・矩形描画) はいずれも
;   同じ 4 組を同じ順で受けるので共用する。
;   破壊: A,B (D = 最後に読んだ $D389-$D38A の値)。
;------------------------------------------------------------------------------
rect_from_param:
                LDD     0,U
                STD     <WK_RECT_X0
                LDD     2,U
                STD     <WK_RECT_Y0
                LDD     4,U
                STD     <WK_RECT_X1
                LDD     6,U
                STD     <WK_RECT_Y1
                RTS

;==============================================================================
; 追加コマンド実装 ($1A Change Color / $19 PAINT / 共通 get_pixel_color)
;   dispatch table は前段の cmd_19_paint / cmd_1a_chgcol stub から JMP で飛ぶ。
;==============================================================================

;------------------------------------------------------------------------------
; get_pixel_color — VRAM の現 (WK_PX_X, WK_PX_Y) のドット色を A に返す
;   入力: WK_PX_X (16bit), WK_PX_Y (16bit, low byte 使用)
;   出力: A = 色コード (0-7)
;   破壊: B, X, Y, U, CC
;------------------------------------------------------------------------------
get_pixel_color:
                PSHS    X,Y,U
                JSR     gr_pixaddr              ; U = 面内オフセット / PBF_MASK = マスク
                LDA     IO_VRAM_GATE            ; gate ON (読出で開く)
                ; G→R→B の順に「マスク位置のドット有無」をキャリーへ変換し
                ; ROLB で B に集約する (ADDA #$FF: A≠0 のときのみ C=1)。
                ; 結果 B = G<<2 | R<<1 | B面 = 色コード 0-7。
                CLRB
                LDA     $8000,U                 ; G プレーン
                ANDA    <PBF_MASK
                ADDA    #$FF
                ROLB
                LDA     $4000,U                 ; R プレーン
                ANDA    <PBF_MASK
                ADDA    #$FF
                ROLB
                LDA     ,U                      ; B プレーン
                ANDA    <PBF_MASK
                ADDA    #$FF
                ROLB
                STB     IO_VRAM_GATE            ; gate OFF (書込で閉じる)
                TFR     B,A                     ; A = 色コード
                PULS    X,Y,U,PC

;--- cc_scan — 1 組ぶんの色置換 (枠内を走査。X は保存される) -------------------
cc_scan:
                LDD     <WK_RECT_Y0
                STD     <WK_PX_Y
cc_y_loop:
                LDD     <WK_PX_Y
                CMPD    <WK_RECT_Y1
                BHI     cc_done
                LDD     <WK_RECT_X0
                STD     <WK_PX_X
cc_x_loop:
                LDD     <WK_PX_X
                CMPD    <WK_RECT_X1
                BHI     cc_next_y
                BSR     get_pixel_color
                CMPA    <WK_CC_OLDCOL
                BNE     cc_next_x
                JSR     pset_internal
cc_next_x:
                INC     <WK_PX_X+1              ; X+1 (16 ビット)
                BNE     cc_x_loop
                INC     <WK_PX_X
                BRA     cc_x_loop
cc_next_y:
                INC     <WK_PX_Y+1              ; Y+1 (16 ビット)
                BNE     cc_y_loop
                INC     <WK_PX_Y
                BRA     cc_y_loop

;------------------------------------------------------------------------------
; readtimer_entry — READ TIMER ($3E、参考書籍)。$D00B-$D01B (17byte) を $D384+ へ。
;------------------------------------------------------------------------------
readtimer_entry:
                COM     <TM_GUARD
                LEAY    1,U
                LDX     #TM_TC
rt_copy:
                LDD     ,X++                    ; 2 byte ずつ 8 回 ($D00B-$D01A)
                STD     ,Y++
                CMPX    #TM_END-1
                BLO     rt_copy
                LDA     ,X                      ; 残り 1 byte ($D01B)
                STA     ,Y
                ; fall through

;------------------------------------------------------------------------------
; timer_complete — SET / READ TIMER の完了 (周期処理を 1 回反映する)。
;------------------------------------------------------------------------------
timer_complete:
                COM     <TM_GUARD
                BEQ     tc_done
                ORCC    #$40                    ; FIRQ マスク
                JSR     nmi_subwork             ; 周期処理 (DP=$D0 のまま戻る = 常用 DP)
                ANDCC   #$BF                    ; FIRQ マスク解除
                CLR     <TM_GUARD
tc_done:
                RTS

;------------------------------------------------------------------------------
; settimer_entry — SET TIMER ($3D、参考書籍)
;   $D383=ビットマスク, $D384=フラグ値, $D385+=4byte フィールド群。マスク bit0 →
;   $D00B(フラグ), bit1-4 → $D00C/$D010/$D014/$D018 (RTC比較/アラーム/インターバル
;   カウンタ/リロード) を選択コピー。設定後 timer_complete で NMI 作業を反映。
;   タイマ作業域 $D00B-$D01B は絶対アドレス (DP 不定の文脈から入るため)。
;------------------------------------------------------------------------------
settimer_entry:
                COM     <TM_GUARD       ; NMI 側の更新を一旦止める
                TFR     U,X             ; X → 選択マスク
                LDD     ,X++                    ; A=選択マスク, B=フラグ値, X→$D385
                LDY     #TM_CLK         ; Y → 最初の 4 バイト群 (時刻カウンタ)
                LSRA                            ; bit0: フラグ値を採るか
                BCC     st_group
                STB     <TM_TC          ; TC = フラグ値
st_group:
                LDB     #$04                    ; bit1-bit4 = 4 バイト群 4 個
st_loop:
                LSRA                            ; その群を採るか
                BCC     st_next
                LDU     2,X                     ; 群の後半 2 バイト
                STU     2,Y
                LDU     ,X                      ; 群の前半 2 バイト
                STU     ,Y
st_next:
                LEAY    4,Y                     ; 次の群へ (転送先)
                LEAX    4,X                     ; 次の群へ (パラメータ欄)
                DECB
                BNE     st_loop
                BRA     timer_complete

;------------------------------------------------------------------------------
; intctl_entry — Interrupt Control ($2C)。参考書籍。
;   相対 3,4 = IC (16 ビットの割込制御フラグ)。下位 10 ビットが PF1〜PF10 の
;   割込みの許可 (bit0=PF1 … bit9=PF10)。
;   受けとったビットパターンを PF キー定義テーブルの各枠の +0 の bit7 (PF キー割込み制御
;   フラグ。参考書籍) へ展開する。bit0-3 (文字数) は保つ。
;   設定中は F フラグで FIRQ を止める (FIRQ 側がフラグを読むため)。
;   応答データ 相対 0 = エラーコードは常に 0 (参考書籍)。
;------------------------------------------------------------------------------
intctl_entry:
                ORCC    #$40                    ; F フラグ: 設定中は FIRQ を止める
                LDD     0,U                     ; 相対 3,4 = IC (16bit)
                STD     2,U                     ; 走査用の作業複写 (本命令が使わない欄)
                ; 枠の前進は「周の頭で B ぶん進める」形にする。起点は PF1 の 1 枠手前、
                ;   終りは最終枠 (PF10) の位置で、枠に触れるのは前進した後だけなので
                ;   起点の 16 byte (PF 番号 0 = 枠が無い) は読み書きしない。
                LDB     #16                     ; 枠の間隔
                LDX     #KB_PF_BASE             ; X → PF1 の 1 枠手前
ic_loop:
                ABX                             ; 次の枠へ
                LSR     2,U                     ; 作業複写を 1 ビット右へ送り、
                ROR     3,U                     ;   押し出したビット = この PF の可否
                LDA     ,X
                ANDA    #$7F
                BCC     ic_sto
                ORA     #$80            ; bit7 = 割込みを発生する
ic_sto:
                STA     ,X
                CMPX    #KB_PF_BASE+KB_PF_SIZE  ; PF10 の枠まで展開したか
                BLO     ic_loop
                ANDCC   #$BF                    ; FIRQ 再開
                RTS
pely_err:
                LDB     #$3D                    ; コンソール座標値の誤り
                STB     >SH_ERR
                PULS    B,PC
pe_locy:
                ; A = 行。本体は ROM 末尾コード域 (pely_ext)。復帰 (PULS B,PC) は
                ; pely_ext 側で行う。

;--- pely_ext — SBA オーダ (参考書籍) の Y 座標の本体 ----------
;   $12 X Y。座標の上限は X が桁数-1、Y が行数-1 (画面の 1 行の文字数と行数の
;   設定による)。上限を越える指定はコンソール座標値の誤り ($3D、参考書籍) と
;   してバッファアドレスを変えない。pe_locy から JMP。
;   スタックに B (呼出元保存) が積まれた状態で入る (PULS B,PC で復帰)。
pely_ext:
                CMPA    <WK_SCREEN_H
                BCC     pely_err
                LDB     <WK_LOC_COL
                CMPB    <WK_SCREEN_W
                BCC     pely_err
                STA     <WK_CURSOR_Y
                STB     <WK_CURSOR_X
                PULS    B,PC
;--- SF オーダ (参考書籍): $11 <アトリビュート文字> -----------
;   現在のバッファアドレスのセルをフィールドの先頭 (F=1) にし、そこから次の
;   フィールドの先頭 (F=1) またはモード画面の終りまでの属性を at にする。
;   バッファアドレスは変えない。以後の一連の文字列は at で描く (sf_seq_chk)。
pe_sf:
                ANDA    #$7F
                STA     <WK_TXT_COL              ; 一連の文字列の属性 (F を除く)
                PSHS    X                       ; 出力中の文字列のポインタを守る
                                                ;   (con_cell が X を作り替えるため。
                                                ;    putchar の契約は B と X の双方)
                JSR     con_cell                ; X = 現在のセル (A,B 破壊)
                STX     <WK_SF_ADR
                LDA     <WK_TXT_COL
                ORA     #$80
                STA     CONBUF_ATTR-CONBUF_CHAR,X ; 先頭 = F + at
                JSR     sf_fill1                ; 次のセルからフィールドの終りまで at
                PULS    X
                PULS    B,PC
pe_locx:
                LDB     #$92                    ; 次は行を待つ
                BRA     pe_p1
;--- エスケープパラメータ受信: 種別に応じて処理し状態クリア ---
;   B (呼出元 cmd_03_console の文字数カウンタ) を破壊しないこと。
;   本体は ROM 末尾の空き領域 (pc_esc_ext) に配置。
pc_esc_param:

;==============================================================================
; コンソール制御コード拡張
;==============================================================================

;--- pc_esc_ext — オーダシーケンスの 2 バイト目以降の処理 ----------------------
;   入口 pc_esc_param (putchar 内) から入る。A = 受信したデータバイト。
;   WK_ESC_STATE 種別: $11=SF のアトリビュート文字 / $12=SBA の X / $92=SBA の Y
;                      / $13=RC の文字数 / $93=RC の文字コード
;                      / $1B=Lock Keyboard 等の 2 バイト目 (読み捨てる)。
;   X/B (呼出元 cmd_03_console のポインタ/文字数カウンタ) を破壊しないこと。
pc_esc_ext:
                PSHS    B                       ; B 保存 (ループカウンタ)
                LDB     <WK_ESC_STATE
                CLR     <WK_ESC_STATE
                CMPB    #$11                    ; $11 = SF
                BEQ     pe_sf
                CMPB    #$12                    ; $12 第1パラメータ (桁)
                BEQ     pe_locx
                CMPB    #$92                    ; $12 第2パラメータ (行)
                BEQ     pe_locy
                CMPB    #$13                    ; $13 RC 第1パラメータ (文字数)
                BEQ     pe_rcn
                CMPB    #$93                    ; $13 RC 第2パラメータ (文字コード)
                BEQ     pe_rcc

;--- pe_esc2 — $1B で始まるオーダシーケンス (参考書籍) ---
;   入口 pe_esc_ext から LBRA。A = ESC の次のバイトで、B は退避済 (自由に使える)。
;   参考書籍の一覧に無い 2 バイト目は読み捨てる。共有 RAM 相対 0 は
;   0 のまま (STAGE_ALT_RAM はオーダシーケンスの誤り $3E (参考書籍) を置く)。
;   先行入力の許可 / 停止 ($67 / $68) は状態を保つ。停止したときにキーバッファの
;   キーコードは捨てない (捨て去るのは Erase Key Buffer ($39) と $29 INKEY の
;   RESET フラグである)。
pe_esc2:
                CMPA    #$39                    ; Erase Key Buffer
                BEQ     pe_es_erase
                LDB     #$80                    ; Lock / Unlock Keyboard のビット
                CMPA    #$23                    ; Lock Keyboard
                BEQ     pe_es_set
                CMPA    #$22                    ; Unlock Keyboard
                BEQ     pe_es_clr
                LDB     #$01                    ; 先行入力のビット
                CMPA    #$67                    ; Set Buffer Mode
                BEQ     pe_es_set
                CMPA    #$68                    ; Set Unbuffer Mode
                BEQ     pe_es_clr
        IFDEF STAGE_ALT_RAM
                LDA     #$3E                    ; オーダシーケンスの誤り
                STA     >SH_ERR
        ENDC
                PULS    B,PC
pe_es_set:
                ORB     <WK_KB_CTL
pe_es_sto:
                STB     <WK_KB_CTL
                PULS    B,PC
pe_es_erase:
                JSR     kb_flush                ; キーバッファのキーコードを捨て去る (A,B 破壊。B は下で戻す)
                PULS    B,PC
pe_es_clr:
                COMB
                ANDB    <WK_KB_CTL
                BRA     pe_es_sto
;--- RC オーダ (参考書籍): $13 <文字数> <文字コード> の 3 バイト列 -------
;   現在のバッファアドレスから指定個数分、指定文字を表示する。文字数は 0〜255。
;   1 文字ずつ通常の表示経路 (pc_print) へ流すので、折返し・行送りの規則は
;   普通に書いた文字列と同一になる。
pe_rcn:
                LDB     #$93                    ; 次は文字コードを待つ
pe_p1:
                STA     <WK_LOC_COL              ; 第1パラメータを退避し、次を待つ
                STB     <WK_ESC_STATE
                PULS    B,PC
pe_rcc:
                LDB     <WK_LOC_COL              ; 文字数
                BEQ     pe_rcx
pe_rcl:
                PSHS    A,B
                BSR     pc_print                ; 1 文字表示して 1 桁進める
                PULS    A,B
                DECB
                BNE     pe_rcl
pe_rcx:
                PULS    B,PC


                ; $16 CHAIN — N 点ポリライン。本体は chain16_entry。
                ; 1 点でも範囲外の座標があれば描かない。

;--- ptk_ch — pbb_take n<8 用 16bit 左シフトチェーン (空き領域) ----------------
;   入口 = ptk_ch + (7-n)*2 (ptk_slow が計算)。D=ACC を n 回左シフトして書戻し、
;   スタックの戻り値 (取り出しビット) を A へ復帰して RTS。
ptk_ch:
                ASLB
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
                STD     <PBB_ACC
                PULS    A,PC

;------------------------------------------------------------------------------
; putchar — 1 文字描画 (A=コード、現在カーソル位置に)
;   カーソル位置を進める。
;   簡易実装: ASCII 印字可能範囲のみ対応、改行/タブ等は無視
;------------------------------------------------------------------------------
putchar:
                ; --- エスケープパラメータ待ち中か? ---
                TST     <WK_ESC_STATE
                BNE     pc_esc_param
                ; --- 表示状態: 制御コードを「動作」で扱うか「字形」で描くか ---
                ;   コンソール制御 ($0C) のパラメタ bit1 が 0 の間は、$00-$1F も
                ;   フォントの字形として 1 桁ぶん描く (参考書籍の表示状態切替)。
                ;   判定は区画の予算の都合で ctl_order_act (空きのある区画) に置き、
                ;   字形表示中は同ルーチンが戻りアドレスを捨てて pc_print へ跳ぶ。
                CMPA    #$20
                BCC     pc_print                ; $20 以上は常に字形
                BSR     ctl_order_act           ; 字形表示中なら戻らず pc_print へ
                BSR     sf_seq_chk              ; SF の一連の文字列に入らないオーダなら一連を終える
                ; --- オーダ (副指令) の判定 — 参考書籍のオーダ一覧 ---
                ;   ここでは近くに本体がある分だけを判定し、残り (EL / BEL / HT /
                ;   HOME / カーソル移動) は ROM 末尾の区画 (pc_ord_ext) で受ける。
                CMPA    #$0D                    ; CR (参考書籍)
                BEQ     putchar_cr
                CMPA    #$0A                    ; LF (参考書籍)
                BEQ     putchar_lf
                CMPA    #$08                    ; BS (参考書籍)
                BEQ     pc_bs
                CMPA    #$0C                    ; EA = 画面消去 (参考書籍)
                BEQ     pc_cls
                CMPA    #$1B                    ; ESC 始まりの 2 バイト列 (参考書籍)
                BEQ     pc_begin_esc
                CMPA    #$11                    ; SF / SBA / RC (参考書籍)
                BCS     pc_ord_far
                CMPA    #$13
                BLS     pc_begin_esc
pc_ord_far:
                JMP     pc_ord_ext
                ; --- 印字可能文字 ($20-): フォント描画 → カーソル進める ---
pc_print:
                JSR     draw_char_at_cursor
                INC     <WK_CURSOR_X
                LDA     <WK_CURSOR_X
                CMPA    <WK_SCREEN_W
                BCS     putchar_done
                CLR     <WK_CURSOR_X
                ; fall through to LF
putchar_lf:
                JMP     pc_lf_ext               ; 行送り (本体は別の区画)
;--- エスケープ開始: 種別 ($11/$1B) を覚えて次バイトを待つ ---
pc_begin_esc:
                STA     <WK_ESC_STATE
putchar_done:
coa_ret:
                RTS

;--- ctl_order_act — 制御コードを動作として扱うか字形で描くかの分岐 ------------
;   putchar から JSR で呼ぶ。A ($20 未満の制御コード) は保存する。
;   コンソール制御 ($0C) のパラメタ bit1 が
;     1 = 動作として実行 (通常)      → そのまま RTS。呼出元が制御コード判定へ進む
;     0 = 字形として表示             → 戻りアドレスを捨てて pc_print へ跳ぶ
;   B は呼出元 cmd_03_console の文字数カウンタなので保存する (putchar 契約)。
;     PSHS/PULS は CC を変えないので、ANDB の Z は PULS を跨いで生き残る。
;   putchar が在る区画は残り byte が乏しいため、判定本体は別の区画へ置き、
;   呼出側は JSR 3 byte だけを置く。参照はラベル経由のみで位置依存はない。
ctl_order_act:
                PSHS    B
                LDB     <WK_CSCTL       ; 表示制御ワーク (コンソール制御の現在値)
                ANDB    #$02                    ; bit1 = order action
                PULS    B
                BNE     coa_ret                 ; bit1=1 → 動作として実行
                LEAS    2,S                     ; 戻りアドレスを捨てる
                BRA     pc_print                ; 字形として 1 桁描く
;--- CR (Carriage Return) オーダ (参考書籍) ------------------------------
;   バッファアドレスを行の先頭へ移し、オート LF (コンソール制御の CF bit5) が
;   有効なら LF オーダの動作も行なう。
putchar_cr:
                CLR     <WK_CURSOR_X
                LDA     <WK_CSCTL       ; 表示制御ワーク (CF の現在値)
                BITA    #$20                    ; bit5 = オート LF
                BEQ     putchar_done
                BRA     putchar_lf              ; LF オーダの動作も同時に行なう
pc_bs:
                ; BS オーダ (参考書籍): バッファアドレスがフィールドの先頭でなければ
                ;   1 つ前へ戻し、先頭なら何もしない。桁の先頭ではなくフィールドの
                ;   先頭が境界なので、前の行の末尾へも戻る。
                ;   呼出元 cmd_03_console の文字数カウンタ (B) と文字列ポインタ (X) は
                ;   壊さない (putchar 契約)。
                PSHS    B,X,U
                JSR     at_fld_start
                BEQ     pcb_ret
                JSR     cell_prev
pcb_ret:
                PULS    B,X,U,PC
;--- pc_cls — EA (Erase All) オーダ (参考書籍) ---------------
;   モード画面を Null コード ($00) で埋め、バッファアドレスをその先頭へ移す。
;   B (呼出元 cmd_03_console の文字数カウンタ) と
;   X (文字列ポインタ) を破壊しないこと (putchar 契約)。
pc_cls:
                PSHS    B,X,Y,U                 ; 呼出元の計数・ポインタを守る
                LDA     <WK_CURSOR_Y
                JSR     ms_rows                 ; A = モード画面の上端行 / B = 下端行+1
                JSR     erase_rows
                JSR     pc_home                 ; バッファアドレスはその先頭へ
                PULS    B,X,Y,U,PC

;--- sf_seq_chk — SF オーダに続く一連の文字列 (参考書籍) の判定 ---
;   一連の文字列は SF に続く文字データ、BEL、BS、HT、RC の並び。それ以外の
;   オーダが来たら一連は終わり、以後の文字はセルのアトリビュートで描く。
;   A = オーダコード (保存)。
sf_seq_chk:
                CMPA    #$11                    ; SF (新しい一連の始まり)
                BEQ     ssc_keep
                CMPA    #$13                    ; RC
                BEQ     ssc_keep
                CMPA    #$07                    ; BEL / BS / HT ($07〜$09)
                BCS     ssc_end
                CMPA    #$09
                BLS     ssc_keep
ssc_end:
                CLR     <WK_SF_ADR
                CLR     <WK_SF_ADR+1
ssc_keep:
                RTS

pset_cont:
                ; 参考書籍のファンクションコード (PSET / PRESET / OR / AND / XOR)
                ; を 1 ドットにも適用する。面ごとのパターンは「色ビット 1 = ドット
                ; マスク / 色ビット 0 = 全 0」で、合成そのものは矩形書込みと同じ
                ; 1 本 (pb1e_op) に集約してある (ファンクションコードは WK_PB_FN)。
                LDA     <WK_COLOR
                STA     <WK_TMP+1                ; 色ビットの送り出し用 (下位から 3 面)
                LDX     #3                      ; B → R → G の 3 面
                LDA     IO_VRAM_GATE            ; VRAM gate ON (読出で開く)
ps_plane:
                CLRA                            ; 色ビット 0 → パターン 0
                LSR     <WK_TMP+1                ; C = この面の色ビット
                BCC     ps_pzero
                LDA     <PBF_MASK               ; 色ビット 1 → パターン = ドットマスク
ps_pzero:
                JSR     pb1e_op                 ; [U] ← 合成結果 (マスク外は保存)
                LEAU    $4000,U                 ; 次のプレーンへ
                LEAX    -1,X
                BNE     ps_plane
                STA     IO_VRAM_GATE            ; gate OFF (書込で閉じる)
                PULS    A,B,X,Y,U,PC

;------------------------------------------------------------------------------
; cmd_20_chline — Character Line (LINE@ 文字形式、動作観察)
;   $D383=属性, $D384=文字, $D386/$D388/$D38A/$D38C = X0,Y0,X1,Y1 (文字座標)。
;   本体は ROM 末尾空き領域 (chline_entry)。
;   クリッピング枠 (WK_VIEW_*) は FM-7 の組ではどの描画経路からも参照されないため、
;   このコードにクリッピング枠設定は持たせない (SUBSYS_AV の組では $39 が設定し、
;   $15 / $16 / $18 の描画範囲になる)。
;------------------------------------------------------------------------------

;==============================================================================
; 描画プリミティブ
;==============================================================================

;------------------------------------------------------------------------------
; vram_clear_all — B/R/G 全 16KB × 3 を 0 で埋める
;------------------------------------------------------------------------------
; pset_internal — 内部用 1 dot 描画
;   入力: WK_PX_X (X 座標 2B), WK_PX_Y (Y 座標 2B), WK_COLOR (色 0-7)
;   仕様: 640×200×8 色固定、クリッピング無視
;------------------------------------------------------------------------------
pset_internal:
                PSHS    A,B,X,Y,U
                BSR     gr_pixaddr              ; U = 面内オフセット / PBF_MASK = ドットマスク
                BRA     pset_cont

;--- pbf_setup — 固定ワーク (PBF_*) 初期化 ----------------------------------
pbf_setup:
                LDB     1,U                     ; X0 下位
                ANDB    #$07
                STB     <PBF_PHASE
                LDX     #pbf_tbl_l
                LDA     B,X
                STA     <PBF_LMASK
                LDB     5,U                     ; X1 下位
                ANDB    #$07
                ADDB    #pbf_tbl_r-pbf_tbl_l    ; X は pbf_tbl_l のまま
                LDA     B,X
                STA     <PBF_RMASK
                LDD     0,U                     ; X0>>3 (VR0 の計算でも使う)
                BSR     d_lsr3
                STB     <PBF_PREV
                PSHS    B
                LDD     4,U                     ; X1>>3
                BSR     d_lsr3
                SUBB    ,S+
                INCB                            ; N = (X1>>3)-(X0>>3)+1
                STB     <PBF_NB
                LDD     4,U                     ; C = (X1-X0+8)>>3
                SUBD    0,U
                ADDD    #8
                BSR     d_lsr3
                STB     <PBF_C
                LDD     6,U                     ; h = Y1-Y0+1
                SUBD    2,U
                ADDD    #1
                STB     <PBF_H
                STB     <PBF_HH
                LDA     3,U                     ; VR0 = Y0下位*80 + X0>>3
                LDB     #80
                MUL
                ADDB    <PBF_PREV
                ADCA    #0
                STD     <PBF_VR0
                STD     <PBF_ROWVR
                LEAX    11,U            ; パターン先頭
                STX     <PBF_ROWST
                RTS

;--- d_rsh8 — D を k (0-7) ビット右へずらす (JSR d_rsh8+14-2k で入る) ---------
;   1 段 = LSRA+RORB の 2 byte なので、入口を後ろから 2k byte 戻した位置に
;   取れば k 段だけ実行して RTS で戻る (ループ・分岐なしの可変シフト)。
;   ビット FIFO の詰め込みと行頭位相合せが共用する。
d_rsh8:
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                RTS
gpa_done:
                STA     <PBF_MASK
                RTS

gr_pixaddr:
                LDA     <WK_PX_Y+1               ; row_offset = Y * 80
                LDB     #80
                MUL
                STD     <WK_TMP
                LDD     <WK_PX_X                 ; byte_offset = X / 8
                BSR     d_lsr3
                ADDD    <WK_TMP
                TFR     D,U                     ; U = B プレーン内 byte オフセット
gpa_mask:                                       ; 画素のビットだけを作る入口 ($1D が使う)
                LDB     <WK_PX_X+1               ; bit mask = $80 >> (X & 7)
                ANDB    #$07
                LDA     #$80
gpa_shift:
                TSTB
                BEQ     gpa_done
                LSRA
                DECB
                BRA     gpa_shift

;------------------------------------------------------------------------------
; gr_pixaddr — WK_PX_X / WK_PX_Y から 1 ドットのアドレスとマスクを求める共通前段
;   出力: U = B プレーン内 byte オフセット / PBF_MASK = $80 >> (X & 7)
;   破壊: A, B, WK_TMP。X / Y は保つ。打点 (pset_internal) と画素色の読出し
;   (get_pixel_color) が共用する。
;------------------------------------------------------------------------------
;--- d_lsr3 — D を 3 ビット右へずらす (座標 → バイト単位) ----------------------
;   図形・矩形書込み・塗り潰しが共用する。D 以外のレジスタは触らない。
;   3 段ぶんの展開列は d_rsh8 の後ろ 3 段がそのまま使えるので、その入口の別名にする
;   (d_rsh8 + 14 - 2*3 = +8 から入ると LSRA/RORB を 3 回行なって RTS で戻る)。
d_lsr3          EQU     d_rsh8+8

;==============================================================================
; hline_blit — draw_line の水平線 (Y0==Y1) 専用バイト単位ブリット高速経路
;   水平線はバイト単位で書く。
;   左端/右端は部分マスクの read-modify-write、中間バイトはプレーン充填値の
;   直書きで 1 行を一括処理する。$15 (塗潰し/枠上下辺)・$06/$07 (do_rect)・
;   $16 の水平線分すべてが本経路を通る。
;
;   入口: draw_line 先頭 (PSHS A,B,DP,X,Y,U 済) から LBEQ で遷移。
;   出口: WK_LAST_X/Y を更新し PULS で全レジスタ復元。
;   範囲外座標 (X>=640 / Y>=200) は汎用の Bresenham 経路へ戻す。
;   BSR/PSHS を一切使わない (スタック追加消費 0)。描画中のスタックを浅く保つため。
;   ワークは直線 DDA 用スクラッチ ($D064-) を流用 (水平経路では未使用)。入口の
;   draw_line が直接ページ $D0 を確立済みなので、そのまま直接アドレッシングで届く。
;   左端 X の 1 ワード (2 バイト) だけは PSET 用 X 座標 ($D070-$D071) を借りる。本経路は 1 dot
;   打点ルーチンを一度も呼ばず (hline_blit には JSR/BSR が 1 つも無い)、かつ
;   汎用経路 (dl_general) の側が同じ PSET 用座標を走査位置として無条件に
;   上書きするため、呼出元は draw_line を跨いで PSET 用座標を生かしておけない。
;   よって本経路が借りている間 PSET 用座標はどちらの経路から見ても死んでいる。
;==============================================================================
HL_XL           EQU     $70             ; 2: 左端 X (昇順正規化後)。後半は XL>>3 (16bit)
HL_XR           EQU     $64             ; 2: 右端 X
HL_DB           EQU     $66             ; 1: B プレーン充填値 ($FF/$00)
HL_DR           EQU     $67             ; 1: R プレーン充填値
HL_DG           EQU     $68             ; 1: G プレーン充填値
HL_LM           EQU     $69             ; 1: 左端バイトマスク
HL_RM           EQU     $6A             ; 1: 右端バイトマスク
HL_NB           EQU     $6B             ; 1: 行内出力バイト数
HL_T            EQU     $6C             ; 1: テンポラリ (XR>>3 / 現マスク / 中間カウンタ)
HL_MI           EQU     $6D             ; 1: 反転マスク

        IFNDEF SUBSYS_AV
hline_blit:
                TST     <WK_PB_FN                ; 合成指定 (参考書籍の OR/AND/XOR)
                BNE     hl_fall                 ;   はバイト直書きに乗らない → 汎用経路
                                                ; 範囲検査 (1 つでも外れたら汎用経路)。入口の D は
                CMPD    #200                    ;   draw_line が比べた WK_LN_Y0 のまま (TST は D を変えない)
                BCC     hl_fall
                LDD     <WK_LN_X1
                CMPD    #640
                BCC     hl_fall
                LDD     <WK_LN_X0
                CMPD    #640
                BCC     hl_fall
                CMPD    <WK_LN_X1                ; X を昇順 (XL <= XR) へ正規化 (D = X0)
                BLS     hl_ns
                LDD     <WK_LN_X1
                STD     <HL_XL
                LDD     <WK_LN_X0
                STD     <HL_XR
                BRA     hl_msk
hl_fall:
                JMP     dl_general              ; 範囲外: 汎用経路が描く
hl_ns:
                STD     <HL_XL
                LDD     <WK_LN_X1
                STD     <HL_XR
hl_msk:
                LDB     <HL_XL+1                ; 端マスク (PutBlock 用テーブル共用)
                ANDB    #$07
                LDX     #pbf_tbl_l
                LDA     B,X
                STA     <HL_LM
                LDB     <HL_XR+1
                ANDB    #$07
                ADDB    #pbf_tbl_r-pbf_tbl_l    ; X は pbf_tbl_l のまま
                LDA     B,X
                STA     <HL_RM
                LDA     <WK_COLOR                ; 充填値: 色 bit k → $FF / $00
                LDX     #$D000+HL_DB            ; 面別充填値の並び (B → R → G)
hl_cl:
                CLRB
                LSRA                            ; C = この面の色ビット
                BCC     hl_c0
                COMB
hl_c0:
                STB     ,X+
                CMPX    #$D000+HL_DG+1
                BCS     hl_cl
                LDD     <HL_XR                  ; XR>>3
                JSR     d_lsr3
                STB     <HL_T
                LDD     <HL_XL                  ; XL>>3
                JSR     d_lsr3
                STD     <HL_XL                  ; HL_XL 再利用: XL>>3 (16bit, A=0)
                LDA     <WK_LN_Y0+1
                LDB     #80
                MUL                             ; D = Y * 80
                ADDD    <HL_XL
                TFR     D,U                     ; U = B プレーン内 byte アドレス
                LDA     <HL_T
                SUBA    <HL_XL+1
                INCA
                STA     <HL_NB                  ; 出力バイト数 N
                LDB     IO_VRAM_GATE            ; VRAM gate ON (行で 1 回)
                ; --- 左端バイト: マスク付き RMW (N=1 なら左右マスク合成) ---
                LDA     <HL_LM
                LDB     <HL_NB
                CMPB    #1
                BNE     hl_l8
                ANDA    <HL_RM
hl_l8:
                STA     <HL_T                   ; マスク
                COMA
                STA     <HL_MI                  ; 反転マスク
                BSR     hl_edge                 ; 左端 1 バイトを 3 面ぶん RMW
                LDA     <HL_NB
                CMPA    #1
                BEQ    hl_done                 ; 1 バイトのみで完結
                LEAU    1,U
                ; --- 中間バイト (N-2 個): 充填値を直書き ---
                LDA     <HL_NB
                SUBA    #2
                BEQ     hl_redge
                STA     <HL_T
hl_mid:
                LDA     <HL_DB
                STA     ,U
                LDA     <HL_DR
                STA     $4000,U
                LDA     <HL_DG
                STA     $8000,U
                LEAU    1,U
                DEC     <HL_T
                BNE     hl_mid
hl_redge:
                ; --- 右端バイト: マスク付き RMW ---
                LDA     <HL_RM
                STA     <HL_T
                COMA
                STA     <HL_MI
                BSR     hl_edge                 ; 右端 1 バイトを 3 面ぶん RMW
hl_done:
                STA     IO_VRAM_GATE            ; gate OFF (書込で閉じる)
                LDD     <WK_LN_X1                ; 終点記録 (汎用経路と同じ)
                STD     <WK_LAST_X
                LDD     <WK_LN_Y1
                STD     <WK_LAST_Y
                PULS    A,B,DP,X,Y,U,PC

;--- hl_edge — 端バイト 1 個をマスク付きで 3 プレーンへ書く --------------------
;   左端と右端が共用する (new = (old & ~m) | (充填値≠0 ? m : 0))。
;   入力: U = B プレーン内アドレス / HL_T = 立てるビット /
;   HL_MI = その反転 / HL_DB,HL_DR,HL_DG = 面ごとの充填値。U は保つ。
hl_edge:
                LDX     #$D000+HL_DB            ; B → R → G の 3 面ぶん
                PSHS    U
hl_ep:
                LDA     ,U
                ANDA    <HL_MI
                TST     ,X+
                BEQ     hl_ep0
                ORA     <HL_T
hl_ep0:
                STA     ,U
                LEAU    $4000,U                 ; 次のプレーンへ
                CMPX    #$D000+HL_DG+1
                BCS     hl_ep
                PULS    U,PC
        ENDC


;------------------------------------------------------------------------------
; cmd_01_screen — SCREEN ($D384=W, $D385=H)
;------------------------------------------------------------------------------
                ; 本体は ROM 末尾 c01_ext (全パラメータ検証 → モード設定 → 消去)。
                ; 検証に合格するまでワークへ一切書かない。誤ったパラメータでワークを
                ;   変えないため。

;--- chl_fill — $20 ボックスフラグ 2: 矩形塗潰し (行ループ、本体は cl_line) ---
;   Y0..Y1 の各行に水平の文字直線を引く。Y0>Y1 は正順化。終端行は CL_END に
;   保持 (cl_line が CL_* スクラッチを使うため呼出間で生存する別バイト)。
;   chl_disp (別の空き領域) から LBEQ で飛んでくる。
chl_fill:
                LDA     5,U                     ; Y0
                LDB     9,U                     ; Y1
                CMPA    9,U                     ; Y0 > Y1 なら交換 (正順化)
                BLS     chf_ord
                EXG     A,B
chf_ord:
                STB     <CL_END                 ; 終端行
chf_loop:
                STA     >SH_PARAM+5             ; 行 r を両端の Y へ
                STA     >SH_PARAM+9
                PSHS    A
                BSR     cl_line                 ; (X0,r)-(X1,r)
                PULS    A
                CMPA    <CL_END
                BEQ     chf_done
                INCA
                BRA     chf_loop

;--- cl_edge — 端点の欄 1 つを差し替えて直線を 1 本引き、元の値へ戻す ----------
;   X = 差し替える欄のアドレス、U = 差し替える値のあるアドレス。
;   復帰時 A = 戻した元の値 (右辺がそのまま使う)。B/X/U 破壊。
cl_edge:
                LDB     ,U
                LDA     ,X
                PSHS    A,X
                STB     ,X
                BSR     cl_line
                PULS    A,X
                STA     ,X
chf_done:
                RTS

;------------------------------------------------------------------------------
; $1E の汎用経路もバイトブリットエンジン (pbb2_entry、ROM 末尾) が受け持つ。
;------------------------------------------------------------------------------

;==============================================================================
; chl_disp — $20 Character Line ボックスフラグ ($D38D) 分岐 (動作観察)
;   0=直線 / 1=矩形の 4 辺枠 / 2=矩形塗潰し / 3 以上=直線フォールバック。
;   cl_line は端点を SH_PARAM+3/+5/+7/+9 (X0,Y0,X1,Y1) から読むため、
;   辺ごとに端点を書換えて呼ぶ (元値は都度退避・復元)。塗潰し本体 chl_fill
;   は別の空き領域に配置 (本区画の残量都合)。
;==============================================================================
chl_disp:
                LDA     10,U                    ; $D38D = ボックスフラグ
                CMPA    #1
                BEQ     chl_frame
                CMPA    #2
                BEQ     chl_fill                ; 2 = 塗潰し (別区画)
                BRA     cl_line                 ; 0/3 以上 = 直線 1 本 (末尾呼び)
chl_frame:
                ; 4 辺とも「端点の欄を 1 つだけ差し替えて 1 本引き、元へ戻す」
                ;   同じ並びなので cl_edge にまとめてある。
                LDX     #SH_PARAM+9             ; --- 上辺: (X0,Y0)-(X1,Y0) ---
                LDU     #SH_PARAM+5             ;     Y1 ← Y0
                BSR     cl_edge
                LDX     #SH_PARAM+5             ; --- 下辺: (X0,Y1)-(X1,Y1) ---
                LDU     #SH_PARAM+9             ;     Y0 ← Y1
                BSR     cl_edge
                LDX     #SH_PARAM+7             ; --- 左辺: (X0,Y0)-(X0,Y1) ---
                LDU     #SH_PARAM+3             ;     X1 ← X0
                BSR     cl_edge
                ; --- 右辺: (X1,Y0)-(X1,Y1) --- (A = 戻した X1 のまま)
                STA     >SH_PARAM+3             ; X0 ← X1

;--- cl_line — (X0,Y0)-(X1,Y1) に CL_CHR/CL_ATR で文字直線 (DDA) を引く -------
;   端点は SH_PARAM+3/+5/+7/+9 から読む (chl_disp が辺ごとに書換えて呼ぶ)。
;   A/B/X 破壊。CL_ATR/CL_CHR は不変。
cl_line:
                LDA     >SH_PARAM+3             ; 現在セル = (X0,Y0)
                STA     <CB_DCOL
                LDA     >SH_PARAM+5
                STA     <CB_DROW
                LDB     #$01                    ; dx = |X1-X0|, 方向 = ±1
                LDA     >SH_PARAM+7
                SUBA    >SH_PARAM+3
                BCC     chl_dx
                NEGA
                LDB     #$FF
chl_dx:
                STA     <CL_DX
                STB     <CL_SX
                LDB     #$01                    ; dy = |Y1-Y0|, 方向 = ±1
                LDA     >SH_PARAM+9
                SUBA    >SH_PARAM+5
                BCC     chl_dy
                NEGA
                LDB     #$FF
chl_dy:
                STA     <CL_DY
                STB     <CL_SY
                LDA     <CL_DX                  ; 主軸判定
                CMPA    <CL_DY
                BCS     chl_ymaj
                ; --- 桁主軸: dx ステップ + dy 蓄積 ---
                STA     <CL_N
                LSRA
                STA     <CL_ACC
chlx_loop:
                BSR     cl_plot
                LDA     <CL_N
                BEQ     chl_fin
                DEC     <CL_N
                LDA     <CB_DCOL                ; 桁を前進
                ADDA    <CL_SX
                STA     <CB_DCOL
                LDA     <CL_ACC                 ; 蓄積 -= dy、借りで行を前進
                SUBA    <CL_DY
                BCC     chlx_acc
                ADDA    <CL_DX
                LDB     <CB_DROW
                ADDB    <CL_SY
                STB     <CB_DROW
chlx_acc:
                STA     <CL_ACC
                BRA     chlx_loop
chl_ymaj:
                ; --- 行主軸: dy ステップ + dx 蓄積 ---
                LDA     <CL_DY
                STA     <CL_N
                LSRA
                STA     <CL_ACC
chly_loop:
                BSR     cl_plot
                LDA     <CL_N
                BEQ     chl_fin
                DEC     <CL_N
                LDA     <CB_DROW                ; 行を前進
                ADDA    <CL_SY
                STA     <CB_DROW
                LDA     <CL_ACC                 ; 蓄積 -= dx、借りで桁を前進
                SUBA    <CL_DX
                BCC     chly_acc
                ADDA    <CL_DY
                LDB     <CB_DCOL
                ADDB    <CL_SX
                STB     <CB_DCOL
chly_acc:
                STA     <CL_ACC
                BRA     chly_loop

;--- fld_start_cur — U = カーソルを含むフィールドの先頭。A,B,X 破壊 --------------
;   後ろへ F=1 のセルを探す。無ければモード画面の先頭 (非保護フィールド、参考書籍)。
fld_start_cur:
                LDA     <WK_CURSOR_Y
                JSR     ms_rows
                JSR     row_addr                ; X = モード画面の先頭
                PSHS    X
                BSR     con_cell
                TFR     X,U
fsc_lp:
                LDA     CONBUF_ATTR-CONBUF_CHAR,U
                BMI     fsc_done
                CMPU    ,S
                BLS     fsc_done
                LEAU    -1,U
                BRA     fsc_lp
fsc_done:
                LEAS    2,S
chl_fin:
                RTS

;--- con_cell — カーソル位置のコンソールバッファアドレスを X に返す ----------------
;   X = CONBUF_CHAR + (行 × 桁数 + 桁)。属性は同じ変位 + $07D0 に並ぶ。
;   文字の記録 (draw_char_at_cursor) と行消去 (EL オーダ) が共用する。A,B 破壊。
con_cell:
                LDA     <WK_CURSOR_Y
                LDB     <WK_SCREEN_W
                MUL                             ; D = 行 × 桁数
                ADDB    <WK_CURSOR_X
;--- cb_mkx — D (行 × 桁数 + 桁) から X = コンソールバッファのアドレスを組む ---------
;   cl_plot / cb_next_cell も共用する。
cb_mkx:
                ADCA    #0
cb_mkx0:                                        ; 桁上がりを拾わない入口 (MUL の直後から入る)
                ADDD    #CONBUF_CHAR
                TFR     D,X
                RTS

;--- at_fld_start — カーソルがフィールドの先頭なら Z=1 (参考書籍) ------------
;   BS (オーダ・キーの双方) がフィールドの先頭では何もしないための判定。
;   フィールドが定義されていない部分の先頭はモード画面の先頭 (fld_start_cur)。
;   A,B,X,U 破壊。
at_fld_start:
                BSR     fld_start_cur           ; U = フィールドの先頭
                BSR     con_cell                ; X = 現在のセル
                PSHS    U
                CMPX    ,S++
                RTS

;--- cl_plot — CB_DCOL/CB_DROW セルへ CL_CHR を属性 CL_ATR で打つ -------------
;   既存セル属性の bit7 (F = フィールドの先頭) は保つ。
cl_plot:
                LDA     <CB_DROW
                LDB     <WK_SCREEN_W
                MUL                             ; D = 行 * 画面幅
                ADDB    <CB_DCOL
                BSR     cb_mkx          ; X = 文字バッファアドレス
                LDB     CONBUF_ATTR-CONBUF_CHAR,X
                ANDB    #$80                    ; 旧属性の F のみ保存
                ORB     <CL_ATR                 ; B = 合成属性
                LDA     <CL_CHR
                JMP     cb_drawcell             ; セル描画 + conbuf 記録 (RTS 継承)

;--- el_body — カーソルからフィールドの終りまで Null にして描き直す (EL、参考書籍) --
;   バッファアドレスは変えない。A,B,X,U 破壊
el_body:
                JSR     fld_end_cur             ; U = フィールドの終り
                BSR     con_cell
                PSHS    U
elb_lp:
                CMPX    ,S
                BCC     elb_done
                CLR     ,X+
                BRA     elb_lp
elb_done:
                PULS    U

;--- redraw_rng — カーソルのセルから U の手前までコンソールバッファの文字を描き直す ------
;   カーソルは元へ戻す。全レジスタ保存
redraw_rng:
                PSHS    A,B,X,Y,U
                LDD     <WK_CURSOR_X
                PSHS    A,B
                BSR     con_cell
                PSHS    X
                TFR     U,D
                SUBD    ,S++
                BEQ     rr_done
                TFR     D,Y                     ; Y = 描き直すセル数
rr_lp:
                BSR     con_cell
                LDA     ,X
                BSR     draw_char_at_cursor
                LEAY    -1,Y
                BEQ     rr_done                 ; 最後のセルの後はバッファアドレスを進めない
                JSR     cell_next
                BRA     rr_lp
rr_done:
                PULS    A,B
                STD     <WK_CURSOR_X
                PULS    A,B,X,Y,U,PC

;------------------------------------------------------------------------------
; draw_char_at_cursor — 現在カーソル位置 (文字グリッド) に A の文字を描画
;   文字コードをコンソールバッファへ、アトリビュートをアトリビュートバッファへ
;   置き (参考書籍)、フォント = font_base + A*8 をアトリビュートの色
;   (bit0-2) で B/R/G 3 プレーンへ描く。
;   アトリビュートの決め方 (参考書籍):
;     ・SF の一連の文字列の中 (WK_SF_ADR ≠ 0) … SF で定めた属性 WK_TXT_COL。
;       SF を置いたセル自身は F を保つ。次のフィールドの先頭 (F=1) を書き潰したら
;       その F を消し、そのフィールドの残りにも同じ属性を及ぼす。
;     ・一連の外 … セルのアトリビュートのまま。
;   $04 GET の編集中 (GE_EDIT = $40) は M (変更) を立てる。A,B,X,Y,U 保存。
;------------------------------------------------------------------------------
draw_char_at_cursor:
                PSHS    A,B,X,Y,U
                PSHS    A                       ; char 退避 (MUL が A を壊す前に)
                BSR     con_cell                ; X = キャラクタバッファアドレス
                LDA     ,S                      ; A = char (退避値を覗く)
                STA     ,X                      ; 文字を記録
                LDA     CONBUF_ATTR-CONBUF_CHAR,X ; セルのアトリビュート
                LDU     <WK_SF_ADR
                BEQ     dc_attr                 ; 一連の外 → セルのアトリビュートのまま
                TSTA
                BPL     dc_seq                  ; F 無し
                CMPX    <WK_SF_ADR
                BEQ     dc_seq                  ; SF を置いたセル自身 → F を保つ
                JSR     sf_fill1                ; 次のフィールドの先頭を書き潰した →
                CLRA                            ;   残りへ属性を及ぼし、F を消す
dc_seq:
                ANDA    #$80
                ORA     <WK_TXT_COL
dc_attr:
                ORA     <GE_EDIT                 ; 編集中なら M
                CMPX    #CONBUF_CHAR            ; 画面の先頭のセルは F を立てて記録する
                BNE     dc_sto                  ;   (動作観察)
                ORA     #$80
dc_sto:
                STA     CONBUF_ATTR-CONBUF_CHAR,X
                TST     <WK_MONO                 ; 単色表示 ($01 の相対 10 = GR) の指定なら
                BEQ     dc_col                   ;   描く色だけを緑 (4) に差し替える
                ANDA    #$F8                     ;   (セルへ記録する属性は本来の色のまま)
                ORA     #$04
dc_col:
                STA     <WK_CELL_COL
                PULS    A                       ; char 復元
                LDB     #8
                MUL                             ; D = char * 8
                ADDD    #font_base
                TFR     D,U                     ; U = フォント先頭
                BSR     cell_vaddr              ; X = セル先頭 VRAM byte offset (B 面)
        IFDEF SUBSYS_TB
                LDA     <WK_CELL_COL
                JSR     tb_glyph                ; 12 の面へ描く
                PULS    A,B,X,Y,U,PC
        ELSE
                LDA     IO_VRAM_GATE            ; gate ON
                LDB     #8                      ; 8 行
                TST     <WK_CHAR_DBL
                BNE     dc_dbl
                ;--- 80桁 (8px): 1 byte/行、各プレーンをセルの属性の bit で選択 ---
                ;   bit 0=B / bit 1=R / bit 2=G。立っていないプレーンは 0 で消す
                ;   (色変更時の残像防止)。dc_put3 が 3 プレーン分を処理。
dc_loop:                                        ; 行数は入口で B に置いてある (dc_put3 は B を保つ)
                LDA     ,U+                     ; A = フォントバイト
                BSR     dc_put3                 ; B/R/G へセルの属性に従い書く (X=B面)
                LEAX    80,X
                DECB
                BNE     dc_loop
                BRA     dc_end
                ;--- 40桁 (16px倍幅): フォント1バイト→倍化2バイト、各プレーン色選択 ---
dc_dbl:
dc_dbl_loop:
                LDA     ,U+                     ; フォントバイト
                PSHS    B,U                     ; 行カウンタ + フォントptr 退避
                LDU     #dbl_nibble             ; U = 倍化テーブル
                TFR     A,B
                LSRB
                LSRB
                LSRB
                LSRB                            ; B = 上位ニブル
                LDB     B,U                     ; B = dbl[上位] = 左バイト
                PSHS    A                       ; 元フォントバイト退避
                TFR     B,A
                BSR     dc_put3                 ; 左 (X) へ色選択書込
                PULS    A                       ; 元フォントバイト
                ANDA    #$0F                    ; A = 下位ニブル
                LDA     A,U                     ; A = dbl[下位] = 右バイト (U は倍化テーブルのまま)
                LEAX    1,X                     ; 右セルへ
                BSR     dc_put3
                LEAX    -1,X                    ; 左へ戻す
                PULS    B,U                     ; 行カウンタ + フォントptr 復元
                LEAX    80,X
                DECB
                BNE     dc_dbl_loop
                BRA     dc_end
;--- dc_put3: A=パターン, X=B面アドレス。WK_CELL_COL bit0=B/1=R/2=G に従い ---
;   立っているプレーンに A を、立っていないプレーンに 0 を書く。A,B,X 保存。
;   色はセルのアトリビュート (グラフィック描画色 WK_COLOR から独立)。
;   アトリビュートの bit3 = R (参考書籍) が 1 のセルは文字の色を反転して表示する。
;   字形のビットを反転させてから同じ経路へ流すので、字形の部分が
;   黒、地の部分が CL の色になる (セルの下端の余り 2 ラインも同じ規則で埋まる)。
;   3 プレーンを +$4000 刻みのループで処理 (X < $4000 で入る前提。B 面
;   オフセットは常に $0000-$3FFF なので 3 周後 X >= $C000 で必ず終了)。
dc_put3:
                PSHS    A,B,X
                LDB     <WK_CELL_COL
                BITB    #$08                    ; R = 反転表示 (参考書籍)
                BEQ     dcp_lp
                COM     ,S                      ; 退避したパターンを反転して描く
                LDA     ,S
dcp_lp:
                LSRB                            ; carry = 現プレーンの色 bit
                BCS     dcp_st
                CLRA                            ; bit=0 のプレーンは 0 で消す
dcp_st:
                STA     ,X
                LDA     ,S                      ; パターン復元 (0,S = 退避 A)
                LEAX    $4000,X                 ; 次プレーン (B→R→G)
                CMPX    #$C000
                BCS     dcp_lp
                PULS    A,B,X,PC
dc_end:
                ; 10px 行 (20 行モード) はセルが 10 ライン。フォントは 8 ライン
                ;   ぶんしかないので、残る下端 2 ラインを消しに行く (X はここで
                ;   セル先頭 + 8 ライン = その 2 ラインの先頭を指している)。
                BSR     dc_pad10
                STA     IO_VRAM_GATE            ; gate OFF
                PULS    A,B,X,Y,U,PC
        ENDC

;--- cell_vaddr — カーソル位置の文字セル先頭 VRAM アドレスを X に返す --------------
;   X = 行 × 行バイト (640/800) + 桁オフセット (40 桁は倍幅なので桁 × 2)。
;   字形描画 (draw_char_at_cursor) と反転 (cursor_toggle) が共用する。
;   途中結果はスタックに置く。A,B 破壊。
cell_vaddr:
                LDA     <WK_CURSOR_Y
                JSR     su_ofs                  ; D = 行 × 行バイト (640/800)
                PSHS    A,B                     ; 行方向オフセット
                LDB     <WK_CURSOR_X             ; 桁オフセット
                TST     <WK_CHAR_DBL
                BEQ     cva_col
                ASLB                            ; 40 桁 (倍幅): 桁 × 2
cva_col:
                CLRA
                ADDD    ,S++
                TFR     D,X
                RTS

;------------------------------------------------------------------------------
; dc_pad10 — 10px 行 (20 行モード) の文字セル下端 2 ラインを消す
;
;   文字を書く操作は「セルを塗り替える」= セルの中に前の描画を残さない、が
;   参考書籍どおりの見え方である。字形は 8 ライン ぶんしかないので、行ピッチが
;   10px のときはセル下端の 2 ラインが書かれないまま残り、そこに前の描画
;   (図形や以前の文字) が居座ってしまう。ここでその 2 ラインを 0 で消す。
;   8px 行 (25 行モード) はセル = 字形と同じ高さなので何もしない。
;
;   入力: X = セル先頭 + 8 ライン (= 消す 2 ラインの先頭)。VRAM ゲートは
;         呼出元が開いたまま。A,B,X 破壊 (呼出元が復元する)。
;   dc_put3 に A=0 を渡すと、色ビットの立っていないプレーンは元から 0 を書き、
;   立っているプレーンにも 0 (=A) を書くので 3 プレーンとも消える。
;------------------------------------------------------------------------------
        IFNDEF SUBSYS_TB
dc_pad10:
                TST     <WK_ROW20
                BEQ     dcp10_ret               ; 8px 行 → 消す余りは無い
                LDB     #2                      ; 下端 2 ライン
dcp10_lp:
                CLRA
                BSR     dc_put3                 ; 左バイト (80桁はこれだけ)
                TST     <WK_CHAR_DBL
                BEQ     dcp10_nx
                LEAX    1,X
                CLRA
                BSR     dc_put3                 ; 40桁 (倍幅) は右バイトも消す
                LEAX    -1,X
dcp10_nx:
                LEAX    80,X                    ; 次のライン
                DECB
                BNE     dcp10_lp
dcp10_ret:
                RTS
        ENDC

;------------------------------------------------------------------------------
; cursor_toggle — カーソルセルのフルブロックを XOR 反転
;   現在カーソル (WK_CURSOR_X/Y) のセルの全高を 3 プレーンとも反転する。
;   セルの高さは行ピッチに従う (25 行 = 8 ライン / 20 行 = 10 ライン)。
;   XOR なので 2 回呼べば元へ戻る (= 表示/消去 共用、対称)。反転する高さが
;   表示側と消去側で常に同じ値になるので、残像は残らない。
;   40桁 (倍幅) 時は 2 byte 幅。A,B,X 保存。
;------------------------------------------------------------------------------
        IFNDEF SUBSYS_TB                        ; Type-B の位置はカーソルを描かない (動作観察)
cursor_toggle:
                PSHS    A,B,X
                ; 行ベースオフセット = CY * 行バイト
                BSR     cell_vaddr              ; X = セル先頭 VRAM byte offset (B 面)
                LDA     IO_VRAM_GATE            ; gate ON (A = ゲートトークン, ループ中保持)
                ; 反転する高さは行ピッチに合わせて 8 / 10 ライン (25 行で 8、20 行で 10)。
                ; 消去も同じ経路 (XOR) なので、高さを揃えれば残像も出ない。
                LDB     <WK_ROW20                ; $00 (8px 行) / $FF (10px 行)
                ANDB    #2                      ; → 0 / 2
                ADDB    #8                      ; → 8 / 10 スキャンライン
ck_line:
                COM     ,X                      ; B プレーン反転
                COM     $4000,X                 ; R
                COM     $8000,X                 ; G
                TST     <WK_CHAR_DBL
                BEQ     ck_l1
                COM     1,X                     ; 40桁: 右 byte も
                COM     $4001,X
                COM     $8001,X
ck_l1:
                LEAX    80,X                    ; 次スキャンライン (1 ライン = 80 byte 下)
                DECB
                BNE     ck_line
                STA     IO_VRAM_GATE            ; gate OFF
                PULS    A,B,X,PC

;--- g4_blink — キー待ちの間のカーソルの点滅 (g4_wait から来て g4_wait へ戻る) --------
;   キー待ちの間、0.4 秒ごとに反転を入れ替える。
;   点滅はキー待ちの繰り返しの中だけで行う。周期処理 (20 ms) が進める時刻カウンタの
;   最下位桁 ($D00F) が変わるたびに X を 1 減らし、20 回でセルの反転を入れ替える。
;   VRAM のアクセスの開閉 ($D409) を割り込みの中で行わないので、割り込まれた側
;   (描画の途中や、転送された処理) の開閉の状態に触れない。キー待ちの外では点滅しない。
;   反転は cursor_toggle (XOR) なので、控え WK_CURSOR_ON も一緒に入れ替える
;   (この後の cursor_hide が「いま反転が掛かっているか」を正しく読める)。
;   入れ替えの途中で取り消しが入ると反転と控えが食い違うので、その間だけ IRQ を止める。
g4_blink:
                LDA     <TM_CLK+3
                CMPA    <WK_BLINK
                BEQ     g4_blret                ; 時刻が進んでいない (通常はここで戻る)
                STA     <WK_BLINK
                LEAX    -1,X
                BNE     g4_blret
                ORCC    #$10
                BSR     cursor_toggle           ; セル反転 (A,B,X は保存される)
                COM     <WK_CURSOR_ON            ; 反転状態の控えを入れ替える
                ANDCC   #$EF
                LDX     #20
g4_blret:
                LBRA    g4_wait
        ENDC

;------------------------------------------------------------------------------
; nmi_subwork — 周期 NMI のタイマ処理 (参考書籍)。
;   時刻カウンタ + アラーム一致判定 + インターバルタイマのダウンカウントと
;   リロード + 状態バイトの合成を行う。
;   DP=$D0 は呼出元 (hdlr_NMI / timer_complete) が確立済み。
;   NMI ($D00A ガード) と Set/Read Timer 完了 (timer_complete) の両方から呼ばれる。
;------------------------------------------------------------------------------
nmi_subwork:
                ; --- 時刻カウンタ $D00C-$D00F を 1 ティック進める ---
                ;   桁ごとの上限は nmi_climits (フレーム 50 / 秒 60 / 分 60 / 時 24)。
                ;   ティックの大半は最下位桁が上限に届かずそこで終わる。この通常経路
                ;     だけを直接アドレッシングで畳み、桁送りが要る 50 ティックに 1 回
                ;     だけ表参照のループへ落とす (周期処理の所要時間を詰めるため)。
                LDY     #$0000                  ; Y = 通知要因の蓄積器
                INC     <TM_CLK+3       ; 最下位桁 (フレーム) を +1
                LDA     <TM_CLK+3
                CMPA    #$32                    ; 最下位桁の上限 (= nmi_climits 先頭値)
                BCS     nsw_alarm               ; 未達 → 桁送りなし (通常はここで抜ける)
                ; --- 桁送り: 満杯の桁を 0 に戻しながら上位へ送る ---
                LDX     #TM_CLK+3       ; X = いま満杯になった桁
                LDU     #nmi_climits+1          ; U → 秒の桁上限から
nsw_rtcloop:
                CLR     ,X                      ; 満杯の桁を 0 へ戻す
                CMPX    #TM_CLK         ; 最上位桁 (時) まで送り終えたか
                BEQ     nsw_evsnd               ; 全桁送り (0 時) のときだけ通知判定へ
                INC     ,-X                     ; 1 つ上の桁へ繰り上げる
                LDB     ,U+                     ; B = その桁の上限
                CMPB    ,X                      ; 上限 <= 現在値 なら更に上位へ送る
                BLS     nsw_rtcloop
                BRA     nsw_alarm
nsw_almatch:
                LEAY    $10,Y                   ; アラーム一致 (要因 $10) — 通常経路の外に置き、一致時だけ戻る
                BRA     nsw_itimer
nsw_evsnd:
                ; --- 全桁送り (0 時) の通知要因: $D00B bit3 が許可のときだけ $40 ---
                LDB     <TM_TC          ; TC = 通知許可
                BITB    #$08
                BEQ     nsw_alarm
                LEAY    $40,Y
nsw_alarm:
                ; --- 一致通知: 時刻 4 バイトが $D010-$D013 と揃ったとき $10 ---
                LDB     <TM_TC
                BITB    #$01
                BEQ     nsw_itimer
                LDD     <TM_CLK         ; 時:分
                SUBD    <TM_T1I         ; 予約時刻の時:分との差
                BNE     nsw_itimer
                LDD     <TM_CLK+2       ; 秒:フレーム
                SUBD    <TM_T1I+2       ; 予約時刻の秒:フレームとの差
                BEQ     nsw_almatch             ; 一致 → 要因 $10 を立てて戻る
nsw_itimer:
                ; --- インターバルタイマ $D014:$D016 (上位:下位の 32bit) を 1 減じる ---
                LDD     <TM_T2+2        ; 下位 16bit
                BNE     nsw_itdec               ; 0 でなければ上位からの借りは不要
                LDX     <TM_T2          ; 下位が 0 → 上位から 1 借りる
                LEAX    -1,X
                STX     <TM_T2
nsw_itdec:
                SUBD    #$0001
                STD     <TM_T2+2
                BNE     nsw_status              ; 下位が残っていれば継続
                LDD     <TM_T2
                BNE     nsw_status              ; 上位が残っていれば継続
                ; --- 満了: 再設定値 $D018:$D01A から積み直し、許可時は通知要因 $20 ---
                LDD     <TM_T2D+2       ; 再設定値の下位
                STD     <TM_T2+2
                LDD     <TM_T2D         ; 再設定値の上位
                STD     <TM_T2
                LDB     <TM_TC
                BITB    #$02
                BEQ     nsw_status
                LEAY    $20,Y
                BITB    #$04                    ; 1 回限りの指定なら以後の通知許可を落とす
                BEQ     nsw_status
                EORB    #$02
                STB     <TM_TC
nsw_status:
                LEAY    ,Y              ; Y = 事象のビット
                BEQ     nspub_ret       ; 事象なし (通常はこちら)
                LDX     ,S              ; 呼出し元: SET / READ TIMER の完了 (命令の実行中) なら
                CMPX    #nmi_ret        ;   そのまま通知する
                BNE     nsw_publish2
                LDA     12,S            ; 周期 NMI: 割り込まれた場所 (戻りアドレスの上に積まれた PC の
                CMPA    #reset_entry/256        ;   上位) が ROM の中のときだけ通知する (転送された処理が
                BCS     nspub_ret               ;   走っている間は計時だけ)
                ; ↓ nsw_publish2 へ落ちる (間は注釈だけ)

;------------------------------------------------------------------------------
; nsw_publish2 — タイマ機能の割込事象を共有 RAM の STATUS (相対 1) へ立てる
;   (参考書籍: bit4 TIMER / bit5 INTERVAL / bit6 0 時。
;   各ビットを 1 にセットするだけで、0 にするのはメイン CPU)。Y = 事象のビット。
;   続けて ATTENTION (参考書籍) をメイン CPU へ出す: サブ側の $D404 の読出し
;   (負パルス) がメイン CPU の FIRQ になる (参考書籍:
;   メイン側は $FD04 の bit0 で要因を見る)。
;   呼ぶのは ROM の処理がサブ CPU を使っているときだけである。周期の割り込みとキーの
;   割り込みは、$3F / $7F で転送された処理が走っている間にも入る。転送された処理は
;   ワーク RAM を自分のコードやデータで上書きしてよく、共有 RAM とメイン CPU への
;   割り込みも自分で使う。そこで割り込みの処理は、割り込まれた場所 (スタックに
;   積まれた PC) が ROM の中であることを確かめてからここを呼ぶ。持ち主の印を
;   ワーク RAM に置かないので、転送された処理の置き場に書くことが無い。
; nspub_put — B = STATUS に置く値。キーの FIRQ (PF キーの割込みの通知) と共用。
;------------------------------------------------------------------------------
nsw_publish2:
                TFR     Y,D
                ORB     $D381
nspub_put:
                STB     $D381
                TST     >$D404          ; ATTENTION (読みの負パルスでメイン CPU に FIRQ)
nspub_ret:
                RTS

;--- c0a_ext — $0A Get Buffer Address 本体 (カーソル座標応答) -------------------
;   行はスクロール範囲相対で返す (メイン側は $12 と対で範囲相対座標系を往復する)。
;   cmd_0a_getbufadr から JMP。
c0a_ext:
                ; 参考書籍の Y は 0〜24 のキャラクタ座標 = 画面の絶対行で
                ; ある (スクロール範囲の上端を 0 と数えるものではない)。
                LDD     <WK_CURSOR_X             ; A=桁, B=行 (画面の絶対行)
                STD     0,U                     ; $D383=桁, $D384=行
                RTS

;------------------------------------------------------------------------------
; kb_enqueue — B のキーコードをリングバッファへ 1 byte 格納
;   満杯 (count>=32) なら破棄。A,X 保存、U 破壊。
;   満杯は「容量から個数を引いた空き数」で見る (引き算の符号で、個数が容量を
;   越えてしまっている場合もまとめて弾ける)。
;   折返しは下位バイトだけで済ませる。バッファは 32 byte で、先頭 KB_BUF_LO が
;   その 32 の境界に載っているので、下位バイトを +1 して 5 ビットへ畳み、先頭値を
;   重ねれば終端の次が先頭に戻る。上位バイトは常に変わらないので触らない。
;------------------------------------------------------------------------------
kb_enqueue:
                PSHS    A                       ; 呼出元の A を退避 (以降 A は作業用)
                LDA     #KB_BUF_SIZE
                SUBA    <KB_COUNT               ; A = 空き数
                BLE     kbe_done                ; 空き無し → 破棄
                LDU     <KB_WRPTR
                STB     ,U                      ; 書込位置へ格納
                INC     <KB_COUNT
                LDA     <KB_WRPTR+1             ; ポインタの下位だけを進める
                INCA
                ANDA    #KB_BUF_MASK            ; 32 の境界で畳む
                ORA     #KB_BUF_ORG             ; 先頭の下位値を重ねる
                STA     <KB_WRPTR+1
kbe_done:
                PULS    A,PC

;------------------------------------------------------------------------------
; hdlr_FIRQ — キーボード FIRQ (参考書籍)
;   $D400 = ステータス (bit7 = PF キーの識別)、$D401 = キーコード (読出しで
;   ストローブが消える)。通常のキーはリングへ。PF キーは文字列の生成か
;   割込みの発生のどちらか (定義テーブルの割込み制御フラグで選ぶ)。
;   割込みの発生は、キーが届いたその場で STATUS (共有 RAM
;   相対 1) の bit0〜3 に PF 番号を置き、ATTENTION ($D404 の読出し) を出す
;   (参考書籍)。割り込まれた場所 (スタックの PC) が
;   ROM の中のときだけ行う。転送された処理が走っている間は番号も置かない。
;   DP 不定の文脈でも入るため入口で DP を退避して $D0 にする。A,B,DP,X,U を退避。
;------------------------------------------------------------------------------
hdlr_FIRQ:
                PSHS    A,B,DP,X,U      ; DP も退避する (FIRQ は PC と CC しか積まない)
                LDA     #$D0
                TFR     A,DP            ; DP=$D0 を確立 (作業 RAM を直接ページで参照)
                LDD     IO_KEY_STAT             ; A=$D400 (bit7=PF), B=$D401 (key)
                TST     <WK_KB_CTL      ; Lock Keyboard (参考書籍) の間は
                BMI     fr_done                 ;   キー入力を受付けない
                IFNDEF  STAGE_ALT_RAM
                BNE     fr_key                  ; bit0 = 先行入力の間はリングに溜める
                LDX     #KB_BUF_LO              ; 停止中はリングを空にしてから格納する
                CLR     <KB_COUNT
                STX     <KB_RDPTR
                STX     <KB_WRPTR
fr_key:
                ENDC
                TSTA
                BPL     fr_normal
                DECB                            ; PF キー番号は 1〜10 (0 と 11 以上は捨てる)
                CMPB    #9
                BHI     fr_done
                INCB
                TFR     B,A                     ; A = PF キー番号
                LDX     #KB_PF_BASE
                ASLB
                ASLB
                ASLB
                ASLB
                ABX                     ; X = 定義テーブルの枠 (番号 × 16)
                LDB     ,X+             ; +0 = bit7 割込み制御フラグ / bit0-3 文字数
                BMI     fr_pf_int       ;   (参考書籍)
                ANDB    #$0F
                BEQ     fr_done
                TFR     B,A             ; A = 定義文字数
fr_pf_loop:
                LDB     ,X+
                BSR     kb_enqueue
                DECA
                BNE     fr_pf_loop
                BRA     fr_done
fr_normal:
                BSR     kb_enqueue
fr_done:
                PULS    A,B,DP,X,U
                RTI
fr_pf_int:                                      ; A = PF キー番号 (1〜10)
                LDB     8,S             ; 割り込まれた場所 (入口で 7 byte 積んだ上の CC の次 = PC の上位)
                CMPB    #reset_entry/256        ;   が ROM の中でなければ何もしない
                BCS     fr_done
                STA     <KB_PFIRQ       ; 番号を置く (参考書籍)。ROM が読むのは
                                        ;   すぐ下の STATUS の合成だけで、周期の割り込みは読まない
                LDB     $D381           ; STATUS の bit0〜3 を PF 番号に置き換え、
                ANDB    #$F0
                ORB     <KB_PFIRQ
                BSR     nspub_put       ;   ATTENTION を出す
                BRA     fr_done

        IFNDEF SUBSYS_TB                        ; Type-B の位置の $1E は tb1e_entry
;--- pb2_fast — $1E 矩形書込み 2 高速経路 --------------------------------------
pb2_fast:
                CLR     <PBF_INV                ; 既定はパターン反転なし ($1C fn5 の
                                                ;   残骸が FIFO 供給点に効かないよう消す)
                LDA     9,U                     ; 相対 12 = ファンクションコードを
                STA     <WK_PB_FN                ;   1 度だけ確定 (全 $1E 経路が参照)
                CMPA    #5                      ; 参考書籍の NOT = 出力ビットパターン
                BNE     pb2f_nn                 ;   の反転 (参考書籍は 0, 2〜5)
                COM     <PBF_INV                ; パターンを供給点で反転し、
                CLR     <WK_PB_FN                ;   反転後の値を上書きで置く
                BRA     pb2f_fall               ; 反転を扱えるのは汎用経路のみ
pb2f_nn:
                BSR     pbf_guard
                CMPD    #37                     ; 3 プレーン分が共有 RAM 内に収まるか
                BHI    pb2f_fall
                STB     <PBF_T2                 ; 3*h*C が計数 ($D38D) を超える
                ASLB                            ;   (末尾省略パケット) は汎用経路へ
                ADDB    <PBF_T2
                CMPB    10,U
                BHI    pb2f_fall
                JSR     pbf_setup
                LDA     #$01                    ; パス 1 = B プレーン
                STA     <PBF_PMASK
                LDA     IO_VRAM_GATE            ; VRAM gate ON
pb2f_pass:
                BSR     pb_passinit             ; 行・行数・プレーンオフセットを整える
                BSR     pbf_rowloop             ; 行ループ (このプレーン 1 パスぶん)
                ; プレーンデータは h*C byte ちょうどで連続 (プレーン間の読み飛ばしなし)
                LDA     <PBF_PMASK            ; 次パス ($01→$02→$04→終了)
                LSLA
                STA     <PBF_PMASK
                CMPA    #$04
                BLS    pb2f_pass
                BRA     pbf_finish

;--- pb_passinit — $1E プレーン 1 パスぶんの初期化 (2 経路の共通部) -----------
;   矩形左上と行数をパスの先頭へ戻し、対象プレーン ($01/$02/$04) の VRAM
;   オフセット (0/$4000/$8000) を求めて PBF_PLOFS へ置く。
pb_passinit:
                LDD     <PBF_VR0                ; 行・行数をパス毎にリセット
                STD     <PBF_ROWVR
                LDA     <PBF_HH
                STA     <PBF_H
                LDA     <PBF_PMASK              ; 対象プレーンの VRAM オフセット:
                LSRA                            ;   $01/$02/$04 → 0/1/2
                LDB     #$40
                MUL                             ;   D = 0/$0040/$0080
                EXG     A,B                     ;   D = 0/$4000/$8000 (B/R/G)
                STD     <PBF_PLOFS
                RTS

        ENDC
;==============================================================================
; $1C/$1E 矩形書込み 高速化 — バイト単位ブリット (ROM 末尾空き領域)
;
;   汎用形 (フォールバック) は pbb1_entry / pbb2_entry (任意幅ビット連続ブリット)。
;   cmd_1c/cmd_1e の JMP 先は本ルーチンで、適用条件を満たさない形は汎用経路へ落ちる。
;
;   高速経路の適用条件 (満たさない場合は汎用経路へ JMP):
;     X0<=X1<=639, Y0<=Y1<=199, 幅 (X1-X0+1) が 8 の倍数、計数 ($D38D) が必要量
;     以上、かつパターン読出が共有 RAM ($D38E-$D3FF) 内で完結すること
;     ($1C: h*C<=113 / $1E: 3*h*C<=計数<=113。C=幅/8, h=行数)。
;
;   動作観察のパターン詰め規則:
;     - パターンは行末でバイト境界へ切上げないビット連続詰め。幅が 8 の倍数の
;       ときに限り行とバイト境界が常に一致する (= 行単位処理できるのはこの形のみ。
;       それ以外の幅は汎用経路がビット連続のまま処理する)
;     - $1C はパターン bit=1 のドットだけを CL 色で描画し bit=0 は画面を保持する
;     - $1E は 3 プレーン分のパターンが連続し、各プレーン h*C byte ちょうど。
;       各パスは対象プレーンのみ書換える (他プレーン不可侵)
;==============================================================================

; ---- PutBlock 高速経路の固定ワークエリア ----
;   作業域はスタックフレーム (LEAS -28,S) ではなく固定 RAM に置く。描画中の
;   スタックを浅く保つため (コマンドは直列実行で再入なし)。
;   配置は $D0C6-$D0E1 (直接ページ $D0 の空き。参照を直接アドレスで書ける)。
PBF_BASE        EQU     $D0C6
PBF_PHASE       EQU     PBF_BASE+0      ; 1: X0 & 7
PBF_LMASK       EQU     PBF_BASE+1      ; 1: 左端バイトマスク
PBF_RMASK       EQU     PBF_BASE+2      ; 1: 右端バイトマスク
PBF_NB          EQU     PBF_BASE+3      ; 1: 1 行の出力バイト数 N
PBF_C           EQU     PBF_BASE+4      ; 1: 1 行のパターン消費バイト数 C
PBF_H           EQU     PBF_BASE+5      ; 1: 残行数 (パス内カウンタ)
PBF_ROWVR       EQU     PBF_BASE+6      ; 2: 現在行の VRAM 左端アドレス (B プレーン)
PBF_ROWST       EQU     PBF_BASE+8      ; 2: 現在行のパターン読出ポインタ
PBF_MASK        EQU     PBF_BASE+10     ; 1: 現バイトの書込マスク
PBF_PREV        EQU     PBF_BASE+11     ; 1: 直前パターンバイト (アライン用)
PBF_SHC         EQU     PBF_BASE+12     ; 1: シフトカウンタ
PBF_CNT         EQU     PBF_BASE+13     ; 1: 行内バイトカウンタ
PBF_DB          EQU     PBF_BASE+14     ; 1: B プレーン書込データ (D 定数。$1C は
                                        ;    入口の pb1_decode が fn×色から確定)
PBF_DR          EQU     PBF_BASE+15     ; 1: R プレーン書込データ
PBF_DG          EQU     PBF_BASE+16     ; 1: G プレーン書込データ
PBF_T2          EQU     PBF_BASE+17     ; 1: テンポラリ
PBF_CB          EQU     PBF_BASE+18     ; 1: $1C 用 B プレーン保持マスク (A 定数)。
                                        ;    書込式 new=old^(((old&A)^D^old)&m) の A
PBF_CR          EQU     PBF_BASE+19     ; 1: 同 R
PBF_CG          EQU     PBF_BASE+20     ; 1: 同 G
PBF_VR0         EQU     PBF_BASE+21     ; 2: 矩形左上の VRAM アドレス (パス毎の行リセット用)
PBF_INV         EQU     PBF_BASE+23     ; 1: $FF = fn5 (パターン反転) / 0 = 通常。
                                        ;    $1C/$1E 入口で確定。反転はビット FIFO
                                        ;    供給点 (pbb_fetch) で適用する
PBF_PLOFS       EQU     PBF_BASE+24     ; 2: $1E 対象プレーンの VRAM オフセット (0/$4000/$8000)
PBF_PMASK       EQU     PBF_BASE+26     ; 1: 0=$1C モード / $01,$02,$04=$1E 現パスプレーン
PBF_HH          EQU     PBF_BASE+27     ; 1: 行数 h (定数)

;--- pbf_guard — 共通ガード。D = h*C (適用可) / $FFFF (範囲外→フォールバック) --
pbf_guard:
                JSR     pbb_coord               ; X0<=X1 / X1<=639 / Y0<=Y1 / Y1<=199
                BCS     pbg_bad
                LDD     6,U                     ; h = Y1-Y0+1 (1..200)
                SUBD    2,U
                ADDD    #1
                STB     <WK_TMP                  ; (スクラッチ: pset と同じ扱い)
                LDD     4,U                     ; 幅 = X1-X0+1 が 8 の倍数か
                SUBD    0,U
                ADDD    #1
                BITB    #$07                    ; パターンはビット連続詰めのため、行と
                BNE     pbg_bad                 ;   バイト境界が一致する 8 倍数幅のみ対応
                ADDD    #7                      ; C = (X1-X0+8)>>3 (1..80)
                JSR     d_lsr3
                LDA     <WK_TMP
                MUL                             ; D = h*C
                RTS
pbg_bad:
                LDD     #$FFFF
                RTS

;--- pbb_rowloop — ビット FIFO 経路の行ループ (ブロック 1 パスぶん) -----------
;   1 行書くごとに VRAM を 1 行 (80 byte) 下げる。パターン側は行を跨いで
;   ビットを詰めるので進めない。$1C と $1E が共用する。
pbb_rowloop:
                JSR     pbb_row
                LDD     <PBF_ROWVR              ; 次行へ (+80 byte)
                ADDD    #80
                STD     <PBF_ROWVR
                DEC     <PBF_H
                BNE     pbb_rowloop
                RTS
        IFNDEF SUBSYS_TB
pb2f_fall:

;--- pbb2_entry — $1E 任意幅本体 (pb2_fast 不合格時の受け皿) -------------------
;   各プレーンはバイト境界から開始 (動作観察) — パス毎に FIFO を空にして
;   プレーン末尾の端数ビットを破棄する。fn は pb2_fast 入口で確定済。
pbb2_entry:
                JSR     pbb_coord
                LBCS    gr_err_coord            ; 座標範囲外 (参考書籍)
                BSR     pbb_setup2
                LDA     #$01
                STA     <PBF_PMASK              ; パス 1 = B プレーン
                LDA     IO_VRAM_GATE            ; VRAM gate ON
pbb2_pass:
                CLRA                            ; プレーン開始 = バイト境界 (FIFO 破棄)
                CLRB
                STD     <PBB_ACC
                CLR     <PBB_CNT
                BSR     pb_passinit             ; 行・行数・プレーンオフセットを整える
                BSR     pbb_rowloop
                ASL     <PBF_PMASK              ; 次パス $01→$02→$04
                LDA     <PBF_PMASK
                CMPA    #$04
                BLS     pbb2_pass
        ENDC

;--- pbf_finish — 共通終端: gate OFF + 観測可能ワークの終端値を既存経路と一致 --
pbf_finish:
                STA     IO_VRAM_GATE            ; VRAM gate OFF
                RTS

;--- pbf_rowloop — 高速経路の行ループ (ブロック / プレーン 1 パスぶん) --------
;   1 行書くごとに VRAM を 1 行 (80 byte) 下げ、パターン読出位置を 1 行ぶん
;   (C byte) 進める。$1C と $1E が共用する。
pbf_rowloop:
                JSR     pbf_row
                LDD     <PBF_ROWVR              ; 次行へ (+80 byte)
                ADDD    #80
                STD     <PBF_ROWVR
                CLRA                            ; パターンは行毎にバイト境界へ (C 消費)
                LDB     <PBF_C
                ADDD    <PBF_ROWST
                STD     <PBF_ROWST
                DEC     <PBF_H
                BNE     pbf_rowloop
                RTS

;--- pb1_fast — $1C 矩形書込み 1 高速経路 --------------------------------------
pb1_fast:
                BSR     pb1_decode              ; 描画ファンクション+色 → (A,D) 定数
                LBCS    gr_err_fn               ; F>5 (参考書籍は 0〜5)
                BSR     pbf_guard
                CMPD    #113                    ; パターン読出が共有 RAM 内に収まるか
                BHI     pb1f_fall
                CMPB    10,U                    ; 計数 ($D38D) が h*C に満たない
                BHI     pb1f_fall               ;   (末尾省略パケット) は汎用経路へ
                TST     <PBF_INV                ; fn5 (パターン反転) は FIFO 供給点で
                BNE     pb1f_fall               ;   反転する汎用経路に委ねる
                JSR     pbf_setup
                CLR     <PBF_PMASK            ; $1C モード
                LDA     IO_VRAM_GATE            ; VRAM gate ON (ブロックで 1 回)
                BSR     pbf_rowloop             ; 行ループ (ブロック 1 つぶん)
                BRA     pbf_finish
pb1f_fall:


;--- pbb1_entry — $1C 任意幅本体 (pb1_fast 不合格時の受け皿) -------------------
;   描画ファンクションと面別 (A,D) 定数は pb1_fast 入口の pb1_decode が確定済
;   (pbb_coord/pbb_setup2 は PBF_DB../PBF_CB../PBF_INV を破壊しない)。
pbb1_entry:
                JSR     pbb_coord
                LBCS    gr_err_coord            ; 座標範囲外 (参考書籍)
                BSR     pbb_setup2
                CLR     <PBF_PMASK              ; $1C モード
                LDA     IO_VRAM_GATE            ; VRAM gate ON (ブロックで 1 回)
                BSR     pbb_rowloop             ; ビットは FIFO が行を跨いで持ち越す
                BRA     pbf_finish              ; gate OFF + 終端状態 + 正常応答

;--- pbb_setup2 — 共通セットアップ (pbf_setup 流用 + FIFO 初期化) --------------
pbb_setup2:
                JSR     pbs_init                ; パターンストリーム ($D38E〜, 計数 $D38D)
                JSR     pbf_setup               ; 端マスク/NB/h/VR0/ROWVR (ROWST は FIFO に転用)
                LDB     5,U                     ; 行末バイトの取得ビット数 = (X1&7)+1
                ANDB    #$07
                INCB
                STB     <PBB_QN
                CLRA
                CLRB
                STD     <PBB_ACC                ; FIFO 空
                STB     <PBB_CNT
                RTS

;--- pb1_decode — $1C 入口共通: 描画ファンクション+色の確定 --------------------
;   F = $D38C (0-5)。F > 5 は C=1 で拒否 (描かない)。演算指定の意味:
;     0=PSET / 1=PRESET (CL 無視・背景色) / 2=OR / 3=AND / 4=XOR / 5=NOT
;   (5 はパターン反転 PSET。PBF_INV=$FF とし定数表は fn0 行を使う)。
;   描画色 (fn1 は背景色) の各プレーンビットで pb1_fntbl の (A,D) ペアを選択し、
;   PBF_CB/CR/CG (A=保持マスク) と PBF_DB/DR/DG (D=データ) へ展開する。
;   PBF_FF (マスク $FF 直書き可) は色置換系 (fn≦1) のみ非0。A/B/X/Y 破壊。
pb1_decode:
                CLR     <PBF_INV
                LDB     9,U                     ; F ($D38C)
                CMPB    #5
                BHI     pbd_rej                 ; F>5 = 不正 → 描かない
                BNE     pbd_n5
                COM     <PBF_INV                ; fn5: パターン反転
                CLRB                            ;   定数は fn0 (PSET) と同じ
pbd_n5:
                CLRA                            ; PBF_FF: fn0/1 (色置換) = $FF
                CMPB    #2
                BCC     pbd_ff
                DECA
pbd_ff:
                STA     <PBF_FF
                LDA     8,U                     ; CL ($D38B)
                CMPB    #1
                BNE     pbd_n1
                LDA     <WK_BGCOL               ; PRESET: 描画色 = 背景色
pbd_n1:
                ANDA    #$07
                STA     <WK_COLOR
                LDX     #pb1_fntbl              ; X = 定数表の fn 行 (4 byte/fn)
                ASLB
                ASLB
                ABX
                LDY     #PBF_DB                 ; 展開先 (D 列。A 列は +4 = PBF_CB)
pbd_lp:
                LSRA                            ; C = 現プレーンの色ビット
                PSHS    A
                BCS     pbd_b1
                LDD     2,X                     ; 色ビット 0: (A0,D0)
                BRA     pbd_st
pbd_b1:
                LDD     ,X                      ; 色ビット 1: (A1,D1)
pbd_st:
                STA     4,Y                     ; A 定数 → PBF_CB+k
                STB     ,Y                      ; D 定数 → PBF_DB+k
                LEAY    1,Y
                PULS    A
                CMPY    #PBF_DB+3               ; B → R → G の 3 面
                BNE     pbd_lp
                ANDCC   #$FE                    ; C=0 合格
                RTS
pbd_rej:
                ORCC    #$01                    ; C=1 拒否
                RTS

        IFDEF SUBSYS_AV
        IFNDEF SUBSYS_TB
;--- pt_match (Type-A の位置) — 行オフセット+PT_BC の 1byte を全色照合 (8px 同時) ---
;   出力 A = ヒットビットマップ (PT_MODE 適用済)、CC.Z = 全 0。U 破壊。
;   ALU の比較 ($D410 = $87) を使う。比較データ ($D413-$D41A) には paint_entry が
;   照合する色を置いてある。VRAM を読むと $D413 の読みに 8 画素ぶんの一致が出る。
pt_match:
                LDD     <PT_RO
                ADDB    <PT_BC
                ADCA    #0
                TFR     D,U                     ; U = 面の中のバイト位置
                LDA     #$87
                STA     >IO_ALU_CMD             ; ALU = 比較
                LDA     ,U
                LDA     >IO_ALU_CMP             ; 比較の結果
                BRA     pt_mexit
        ELSE
;--- pt_match (SUBSYS_TB) — 行オフセット+PT_BC の 1byte を全色照合 (8px 同時) ------
;   出力 A = ヒットビットマップ (PT_MODE 適用済)、CC.Z = 全 0。B,X,U 破壊。
;   ALU の比較 ($D410 = $87) を使う。比較データ 0 ($D413) に色の 1 層ぶんの値を置いて
;   その層の VRAM を読むと、$D413 の読みに 8 画素ぶんの一致が出る。1 色は 4 層
;   (処理ページ 0 / 1 × オフセット $0000 / $2000) の一致の AND で、どこかの層で
;   0 になればその色の残りの層は読まない。全色の結果の OR がヒットビットマップ。
pt_match:
                LDD     <PT_RO
                ADDB    <PT_BC
                ADCA    #0
                TFR     D,U                     ; U = 面の中のバイト位置
                LDA     #$87
                STA     >IO_ALU_CMD             ; ALU = 比較
                CLR     <PT_HIT
                LDX     #AV_CL                  ; 色ごとの 4 層の比較値
                LDA     <PT_NCOL
                STA     <PT_PB                  ; 残りの色の数
pma_col:
                LDA     #$FF
                STA     <PT_T                   ; 4 層の一致の AND
                CLRB                            ; B = 層 (0〜3)
pma_lay:
                LDA     <AV_PG0
                BITB    #2
                BEQ     pma_pg
                ORA     #$20                    ; 層 2・3 は処理ページ 1
pma_pg:
                STA     >IO_AV_MISC
                LDA     ,X+
                STA     >IO_ALU_CMP             ; 比較データ 0 ← この層の値
                BITB    #1
                BEQ     pma_o0
                LDA     $2000,U                 ; 層 1・3 はオフセット $2000 を読んで比べる
                BRA     pma_rd
pma_o0:
                LDA     ,U
pma_rd:
                LDA     >IO_ALU_CMP             ; 比較の結果
                ANDA    <PT_T
                STA     <PT_T
                BEQ     pma_skip                ; この色はどの画素も一致しない
                INCB
                CMPB    #4
                BCS     pma_lay
                ORA     <PT_HIT
                STA     <PT_HIT
                BRA     pma_next
pma_skip:
                NEGB
                ADDB    #3
                ABX                             ; 読まなかった層の値を飛ばす
pma_next:
                DEC     <PT_PB
                BNE     pma_col
                LDA     <PT_HIT
        ENDC
;--- pt_mexit — 描画範囲の縁のバイトでは範囲の外の画素を境界にしてから、境界 / 塗れる を選ぶ ---
pt_mexit:
                LDB     <PT_BC
                INCB
                CMPB    >PT_DLR+1               ; 右の縁の列か
                BNE     ptm_l
                ORA     <PT_RMASK
ptm_l:
                SUBB    #2
                CMPB    >PT_DLL+1               ; 左の縁の列か
                BNE     ptm_e
                ORA     <PT_LMASK
ptm_e:
                EORA    <PT_MODE                ; 境界 / 塗れる の選択 (Z=全 0)
                RTS
        ELSE
;--- pt_match — 行オフセット+PT_BC の 1byte を全色照合 (8px 同時) ---------------
;   出力 A = ヒットビットマップ (PT_MODE 適用済: $00=境界ビット/$FF=塗れるビット)、
;   CC.Z = 全 0。B,X,U 破壊。VRAM gate は読出で開く (終了時 pt_exit で閉じる)。
pt_match:
                LDD     <PT_RO
                ADDB    <PT_BC
                ADCA    #0
                TFR     D,U                     ; U = 行内バイトの VRAM アドレス (B 面)
                LDA     >IO_VRAM_GATE           ; VRAM gate ON (読出で開く)
                ; 3 面のバイトは色を替えても同じものを読み直すだけなので、色ごとの
                ; 繰り返しの外で 1 度だけ取り出して直接ページへ控える。繰り返しの中は
                ; 16bit 変位付き指標参照 2 回が直接ページ参照 2 回に変わる。
                LDA     ,U                      ; B 面
                STA     <PT_PB
                LDA     $4000,U                 ; R 面
                STA     <PT_PR
                LDA     $8000,U                 ; G 面
                STA     <PT_PG
                CLR     <PT_HIT
                LDX     #PT_CLIST
                LDB     <PT_NCOL
pt_mc:
                LDA     <PT_PB                  ; B 面
                EORA    ,X+
                STA     <PT_T
                LDA     <PT_PR                  ; R 面
                EORA    ,X+
                ORA     <PT_T
                STA     <PT_T
                LDA     <PT_PG                  ; G 面
                EORA    ,X+
                ORA     <PT_T
                COMA                            ; 8px ぶんの「この色に一致」ビット
                ORA     <PT_HIT
                STA     <PT_HIT
                DECB
                BNE     pt_mc
                LDA     <PT_HIT
                EORA    <PT_MODE                ; 境界 / 塗れる の選択 (Z=全 0)
                RTS
        ENDC
;--- pt_scan_rf / pt_scan_lf — 塗れる点探索 (右/左) ----------------------------
pt_scan_rf:
        IFDEF SUBSYS_AV
                LDX     >PT_DLR                 ; DIR=+1 / リミット列 = 描画範囲の右の縁の列 + 1
                BRA     pt_sf
        ELSE
                LDX     #$0100+PT_COLS          ; DIR=+1 / リミット列=1 行のバイト数。行端まで走査
                STX     <PT_DIR                 ;   (行端まで走査しても、ヒット有無の判定は
                LDX     #PT_COLS*256+$FF        ;   呼出側比較でリミット列の打ち切りと等価)
                STX     <PT_LIM                 ; リミット列 / MODE=$FF (塗れる点)
        ENDC
                BRA     pt_scan
pt_sfd:
                PSHS    B                       ; X = 列*8 + ビット内画素
                LDA     <PT_BC
                LDB     #8
                MUL
                ADDB    ,S+
                ADCA    #0
                RTS
pt_scan_lf:
        IFDEF SUBSYS_AV
                LDX     >PT_DLL                 ; DIR=-1 / リミット列 = 描画範囲の左の縁の列 - 1
pt_sf:
                STX     <PT_DIR
                CLR     <PT_MODE
                DEC     <PT_MODE                ; MODE=$FF (塗れる点)
        ELSE
                LDX     #$FFFF                  ; DIR=-1 / リミット列=-1
                STX     <PT_DIR
                STX     <PT_LIM                 ; リミット列=-1 / MODE=$FF
        ENDC
                ; fallthrough pt_scan
;--- pt_scan — 走査共通部。D=開始 X → D=最初のヒット X --------------------------
;   開始バイトはヒットビットマップを開始位置でマスクし、以後はバイト単位で
;   丸ごと照合 (8px/回)。リミット列到達で画面幅 PT_W (右) / -1 (左) を返す (SUBSYS_AV の
;   組は描画範囲の右の縁 + 1 / 左の縁 - 1)。
pt_scan:
        IFDEF SUBSYS_AV
                CMPD    <WK_VIEW_X0             ; 描画範囲の外は縁の外側の X を返す
                BLT     pt_srm
                CMPD    <WK_VIEW_X1
                BGT     pt_soob
        ELSE
                CMPD    #PT_W                   ; 範囲外は即返し (負は $FFFF で兼用)
                BCC     pt_soob
        ENDC
                PSHS    A,B
                JSR     d_lsr3
                STB     <PT_BC                  ; 開始バイト列
                PULS    A,B
                ANDB    #$07                    ; 開始ビット位置
                PSHS    B
        IFDEF SUBSYS_AV
                JSR     pt_match
        ELSE
                BSR     pt_match
        ENDC
                PULS    B
                LDX     #pbf_tbl_r              ; 左: 開始位置以左のビットのみ
                TST     <PT_DIR
                BMI     pt_sml
                SUBB    #pbf_tbl_r-pbf_tbl_l    ; 右: 開始位置以右のビットのみ (pbf_tbl_l)
pt_sml:
                ANDA    B,X
pt_schk:
                BNE     pt_sfound
pt_sloop:
                LDB     <PT_BC
                ADDB    <PT_DIR
                STB     <PT_BC
                CMPB    <PT_LIM
                BEQ     pt_slim                 ; リミット列到達 → なし
                JSR     pt_match
                BEQ     pt_sloop
pt_sfound:
                TST     <PT_DIR                 ; A = ヒットビットマップ (非0)
                BMI     pt_sfl
                CLRB                            ; 右: MSB から最初のヒット
pt_sfr:
                ASLA
                BCS     pt_sfd
                INCB
                BRA     pt_sfr
pt_slim:
                TST     <PT_DIR
                BMI     pt_srm
pt_soob:
        IFDEF SUBSYS_AV
                LDD     <WK_VIEW_X1             ; 右: 描画範囲の右の縁 + 1
                ADDD    #1
                RTS
        ELSE
                TSTA                            ; 開始 X 範囲外: 負→-1 / 右→画面幅
                BMI     pt_srm
                LDD     #PT_W
                RTS
        ENDC
pt_sfl:
                LDB     #7                      ; 左: LSB から最初のヒット
pt_sfl2:
                LSRA
                BCS     pt_sfd
                DECB
                BRA     pt_sfl2
pt_srm:
        IFDEF SUBSYS_AV
                LDD     <WK_VIEW_X0             ; 左: 描画範囲の左の縁 - 1
                SUBD    #1
        ELSE
                LDD     #$FFFF
        ENDC
                RTS

;==============================================================================
; PAINT スキャン原線 + バイト照合 (paint_entry 系から JSR。ROM 末尾空き領域)
;==============================================================================
;--- pt_scan_rb / pt_scan_lb — 境界探索 (右/左)。D=開始 X → D=境界 X ----------
;   見つからない場合 右=画面幅 PT_W / 左=-1。
pt_scan_rb:
        IFDEF SUBSYS_AV
                LDX     >PT_DLR                 ; DIR=+1 / リミット列 = 描画範囲の右の縁の列 + 1
        ELSE
                LDX     #$0100+PT_COLS          ; DIR=+1 / リミット列=1 行のバイト数 (右端)
        ENDC
                BRA     pt_sb
pt_scan_lb:
        IFDEF SUBSYS_AV
                LDX     >PT_DLL                 ; DIR=-1 / リミット列 = 描画範囲の左の縁の列 - 1
        ELSE
                LDX     #$FFFF                  ; DIR=-1 / リミット列=-1 (左端)
        ENDC
pt_sb:
                STX     <PT_DIR
                CLR     <PT_MODE
                BRA     pt_scan

;==============================================================================
; paint_entry — cmd $18 PAINT 本体 (動作観察のスキャンライン・シードフィル)
;   仕様根拠: 参考書籍の PAINT と、入口の受け渡しの動作観察。
;   パラメータ: $D383-4=X / $D385-6=Y / $D387=塗色 / $D388=境界色数 N (0-8) /
;               $D389〜=境界色リスト (N 個)
;   仕様:
;     1. 境界条件 = 「境界色 N 個 + 塗色」のいずれかへの 3 面完全一致
;        (塗色は常に暗黙の境界。リストは N スロットへ塗色を前置して N+1 個)
;     2. 塗りは常に上書き。境界ピクセル自体は塗らない
;     3. シードが境界色/塗色上なら無動作
;     4. N>8・座標範囲外はエラー条件 (描かず終了。応答は $15 と同方針の簡略化)
;     5. 1px 境界の裏側 (張り出し) は方向反転 + 文脈 push で追跡し、
;        凹形状・渦巻も完全充填する
;     6. シードスタック満杯時は黙殺 (取り残しが出得る)
;   実装方式:
;     - 照合・走査はバイト単位 8px 同時 (3 面を EOR/OR/COM でビットマップ化)
;     - スパン塗りは draw_line の水平バイトブリット (hline_blit) を流用
;     - 変数は DP=$D0 の $D0A0-$D0BF (SYMBOL/$04 のスクラッチと重畳するが
;       別コマンドで非同時。NMI は DP 退避復元済かつ当領域不使用)
;     - シードスタック = $D263↓$D100 (4byte/個 88 個、空=SP==$D264、
;       満杯=SP==$D100 で黙殺)
;     - 展開色リスト = $D394〜 (共有 RAM。BUSY 中はメインが書かない)
;==============================================================================
;   直接ページ $D0 の変位で書く。基点 PT_B は SYM_B (SYMBOL のスクラッチの基点) の下位
;   バイトで、Type-A / Type-B の位置 (SUBSYS_AV) では $25、Type-C の位置では $A0 (冒頭の
;   WK_SYM_PTR の説明を参照)。PT_NO 以降の 7 byte ($B7-$BD) はどちらの位置でも同じ。
PT_B            EQU     SYM_B-$D000     ; 基点 (直接ページ $D0 の変位)
PT_NCOL         EQU     PT_B+0  ; 1: 照合色数 (N+1)
PT_T            EQU     PT_B+1  ; 1: 照合スクラッチ
PT_DIR          EQU     PT_B+2  ; 1: スキャン方向 (+1 / $FF)。PT_LIM と隣接
PT_LIM          EQU     PT_B+3  ; 1: バイト列リミット (符号付き、-1..80)。PT_MODE と隣接
PT_MODE         EQU     PT_B+4  ; 1: $00=境界探索 / $FF=塗れる点探索
PT_HIT          EQU     PT_B+5  ; 1: ヒットビットマップ
PT_PB           EQU     PT_B+6  ; 1: 照合中バイトの B 面 (色ごとの繰り返しの外で取得)
PT_PR           EQU     PT_B+7  ; 1: 同 R 面
PT_TX           EQU     PT_B+8  ; 2: テンポラリ X (張り出し境界等)
PT_CY           EQU     PT_B+10 ; 2: 現処理行 Y (PT_TX と 2 ワード連続 = 種点の転記先)
PT_RDIR         EQU     PT_B+12 ; 1: 行方向 ($00=Y-1 / $FF=Y+1。COM で反転)
PT_RX           EQU     PT_B+13 ; 2: 現スパン右境界 X
PT_PG           EQU     PT_B+15 ; 1: 照合中バイトの G 面 (色ごとの繰り返しの外で取得)
PT_LX           EQU     PT_B+17 ; 2: 現スパン左境界 X
PT_NY           EQU     PT_B+19 ; 2: 次行 Y
PT_RO           EQU     PT_B+21 ; 2: 現スキャン行オフセット (Y*80)
PT_NO           EQU     $B7     ; 2: 次行オフセット
PT_OO           EQU     $B9     ; 2: 現行オフセット (行送り中の戻り先)
PT_BC           EQU     $BB     ; 1: 走査バイト列 (X>>3、符号付き)
PT_SP           EQU     $BC     ; 2: シードスタックポインタ
        IFDEF SUBSYS_AV
PT_DLR          EQU     $D394   ; 2: 右向きの走査の向き (+1) と限界の列 (描画範囲の右の縁の列 + 1)。SUBSYS_AV
PT_DLL          EQU     $D396   ; 2: 左向きの走査の向き (-1) と限界の列 (左の縁の列 - 1)
PT_LMASK        EQU     PT_B+7  ; 1: 左の縁のバイトのうち範囲の外の画素 (PT_PR の位置。SUBSYS_AV は PT_PR を使わない)
PT_RMASK        EQU     PT_B+15 ; 1: 右の縁のバイトのうち範囲の外の画素 (PT_PG の位置。同上)
        ENDC
PT_CLIST        EQU     $D394   ; 展開済み色リスト (最大 9 色 × 3byte)
PT_STK_TOP      EQU     $D264   ; シードスタック上端 (空 = SP がここ)
PT_STK_BOT      EQU     $D100   ; 同下端 (満杯 = SP がここ)
        IFDEF SUBSYS_TB
PT_W            EQU     320     ; 画面幅 (FM77AV の 320×200 の画面)
PT_COLS         EQU     40      ; 1 行のバイト数
        ELSE
PT_W            EQU     640     ; 画面幅
PT_COLS         EQU     80      ; 1 行のバイト数
        ENDC

paint_entry:
        IFDEF SUBSYS_AV
        IFNDEF SUBSYS_TB
                ; Type-A の位置の並び (動作観察): 相対 3〜6 = 種点 X,Y / 相対 7 = 塗る色 /
                ;   相対 8 = 境界色の数 N (0〜8) / 相対 9〜 = 境界色 × N。塗る色も境界に
                ;   なる。N が 9 以上は $45、種点が画面外は $3F。ALU の比較データの
                ;   「色の番号の位置」にその色を置いて照合する。
                LDB     5,U                     ; N
                CMPB    #8
                LBHI    gr_err_param
                JSR     av_begin
                LDX     #IO_ALU_CMP             ; 比較データ 0〜7 ($D413-$D41A) を
                LDD     #$8008                  ;   使わない印 $80 に
ptt_cd:
                STA     ,X+
                DECB
                BNE     ptt_cd
                LDA     4,U                     ; 塗る色 (線を引く色にもする)
                STA     <AV_COL
                BSR     ptt_set
                LEAY    6,U                     ; 境界色
                LDB     5,U
                BEQ     ptt_xy
ptt_bl:
                LDA     ,Y+
                BSR     ptt_set
                DECB
                BNE     ptt_bl
ptt_xy:
                LDA     <AV_PG0
                STA     >IO_AV_MISC
                LDD     2,U                     ; 種点
                STD     <PT_CY
                LDD     ,U
                STD     <PT_TX
                BRA     pt_avseed
ptt_set:
                ANDA    #$07
                LDX     #IO_ALU_CMP
                STA     A,X
                RTS
        ELSE
                ; FM77AV の並び (動作観察): 相対 3〜6 = 種点 X,Y / 相対 7〜9 = 塗る色 B,R,G
                ;   (各 4 ビット) / 相対 10 = 境界色の数 N (0〜8) / 相対 11〜 = 境界色
                ;   B,R,G × N。塗る色も境界になる。N が 9 以上は $45、種点が画面外は $3F。
                LDB     7,U                     ; N
                CMPB    #8
                LBHI    gr_err_param
                INCB
                STB     <PT_NCOL                ; 照合する色の数 = 塗る色 + N
                LDA     6,U                     ; 塗る色を N の位置へずらし、色を
                STA     7,U                     ;   3 バイトずつ連ねる
                LDD     4,U
                STD     5,U
                JSR     av_begin
                LDX     #IO_ALU_CMP             ; 比較データ 0〜7 ($D413-$D41A) を
                LDD     #$8008                  ;   使わない印 $80 に
pta_cd:
                STA     ,X+
                DECB
                BNE     pta_cd
                LEAX    5,U                     ; 色 (塗る色、境界色の順)
                LDY     #AV_CL+4
                LDB     <PT_NCOL
pta_cl:
                PSHS    B
                JSR     av_col4                 ; 1 色 = 4 層の比較値 (Y の手前 4 バイト。Y はその先頭)
                LEAX    3,X
                PSHS    U
                LDU     #AV_CL                  ; 前の色と同じなら照合しない (この枠は次の色で上書き)
pta_dq:
                PSHS    Y
                CMPU    ,S++
                BEQ     pta_new                 ; 前の色に同じものは無い
                LDD     ,U
                CMPD    ,Y
                BNE     pta_dn
                LDD     2,U
                CMPD    2,Y
                BEQ     pta_dup
pta_dn:
                LEAU    4,U
                BRA     pta_dq
pta_dup:
                DEC     <PT_NCOL
                LEAY    -4,Y
pta_new:
                LEAY    8,Y
                PULS    U
                PULS    B
                DECB
                BNE     pta_cl
                LDD     >AV_CL                  ; 塗る色の 4 回ぶんの $D411 の値
                STD     <AV_COL
                LDD     >AV_CL+2
                STD     <AV_COL+2
                LDD     2,U                     ; 種点
                STD     <PT_CY
                LDD     ,U
                STD     <PT_TX
        ENDC
;   種点が描画範囲 (WK_VIEW_*) の外なら $3F。範囲の左右の縁のバイト列と、
;   縁のバイトのうち範囲の外の画素のマスクを用意してから種点の行へ進む。D = 種点の X。
pt_avseed:
                CMPD    <WK_VIEW_X0
                BLT     pta_ec
                CMPD    <WK_VIEW_X1
                BGT     pta_ec
                LDD     <PT_CY
                CMPD    <WK_VIEW_Y0
                BLT     pta_ec
                CMPD    <WK_VIEW_Y1
                BLE     pt_wedge
pta_ec:
                JMP     gr_err_coord
;--- pt_wedge — 描画範囲の左右の縁から、走査の向きと限界の列 (PT_DLL / PT_DLR) と、縁の
;   バイトのうち範囲の外の画素のビット (PT_LMASK / PT_RMASK) を用意し、種点の行へ進む
pt_wedge:
                LDD     <WK_VIEW_X0
                BSR     ptw_1
                LDX     #pbf_tbl_l              ; $FF >> n → 反転 = n より左の画素
                LDA     A,X
                COMA
                STA     <PT_LMASK
                DECB
                LDA     #$FF
                STD     >PT_DLL                 ; 左向き: 向き -1 / 限界の列 = 左の縁の列 - 1
                LDD     <WK_VIEW_X1
                BSR     ptw_1
                LDX     #pbf_tbl_r              ; 上位 n+1 ビット → 反転 = n より右の画素
                LDA     A,X
                COMA
                STA     <PT_RMASK
                INCB
                LDA     #1
                STD     >PT_DLR                 ; 右向き: 向き +1 / 限界の列 = 右の縁の列 + 1
                BRA     pt_seed
ptw_1:
                PSHS    B                       ; D = X → B = X >> 3、A = X & 7
                JSR     d_lsr3
                PULS    A
                ANDA    #$07
                RTS
        ELSE
                ; 境界色の数を、直接ページを移す前に見る。移した後だとエラー応答の書込先が
                ;   共有 RAM からずれる。N = 0〜8 を受け付け (0 は境界色なし)、9 以上は $45
                ;   (STAGE_ALT_RAM は参考書籍の 1〜8 の外を $43)。
                LDB     5,U                     ; N = 境界色の数
        IFDEF STAGE_ALT_RAM
                DECB                            ; 参考書籍の Nc は 1〜8
                CMPB    #7
                LBHI    gr_err_ncol
                INCB
        ELSE
                CMPB    #8
                LBHI    gr_err_param
        ENDC
                LDD     4,U                     ; A=塗色, B=N
                STA     5,U                     ; 塗色を N スロットへ前置 (N+1 個に)
                INCB
                STB     <PT_NCOL
                ANDA    #$07
                STA     <WK_COLOR                ; 塗色 = hline_blit の充填値
                ; --- 色リスト展開: $D388〜 N+1 個 → $D394〜 (1 色 = 面別 $00/$FF×3) ---
                LEAX    5,U
                LDU     #PT_CLIST
                PSHS    B
pt_exc:
                LDA     #3                      ; 1 色 = 3 面ぶん
                STA     <PT_T
pt_exc2:
                LSR     ,X                      ; bit0=B 面 → bit1=R 面 → bit2=G 面
                RORB
                SEX
                STA     ,U+
                DEC     <PT_T
                BNE     pt_exc2
                LEAX    1,X
                DEC     ,S
                BNE     pt_exc
                LEAS    1,S
                ; --- 種点の範囲検査 (画面外の種点では何も塗らない) ---
                ;   PT_TX (シード X) と PT_CY (処理行 Y) は 2 ワード (2 バイト) 連続に配置して
                ;   あるので、gr_xy の転記先としてそのまま渡せる。
                LDX     #$D383
                LDY     #$D000+PT_TX            ; DP=$D0 側の実アドレス
                JSR     gr_xy
                LBCC    pt_exit                 ; 画面外の種点 → 塗らない
        ENDC
pt_seed:
                LDA     <PT_CY+1                ; 行オフセット = Y × 1 行のバイト数
                LDB     #PT_COLS
                MUL
                STD     <PT_RO
                LDX     #PT_STK_TOP             ; シードスタック空
                STX     <PT_SP
                ; --- シード行の左右境界を確定 ---
                LDD     <PT_TX
        IFDEF SUBSYS_AV
                JSR     pt_scan_rb
        ELSE
                BSR     pt_scan_rb
        ENDC
                STD     <PT_RX
                LDD     <PT_TX
        IFDEF SUBSYS_AV
                JSR     pt_scan_lb
        ELSE
                BSR     pt_scan_lb
        ENDC
                STD     <PT_LX
                CMPD    <PT_RX
                LBEQ    pt_exit                 ; 左右一致 = シードが境界上 → 無動作
                LDA     #$FF                    ; 反対方向 (Y+1 側) を type1 で予約
                STA     <PT_RDIR
                LDA     #1
                JSR     pt_push
                COM     <PT_RDIR                ; $00 = Y-1 側から処理開始
                ; fallthrough pt_row_span

;--- 行ループ (type0 再開点): 現行スパン [PT_LX+1..PT_RX-1] を塗る -------------
pt_row_span:
                LDA     <PT_CY+1                ; 行オフセット再計算
                LDB     #PT_COLS
                MUL
                STD     <PT_RO
                LDD     <PT_LX
                ADDD    #1
                JSR     pt_scan_rf              ; 行内で塗れる最初の点
                CMPD    <PT_RX
                LBCC    pt_pop                  ; なし → POP
                TFR     D,X                     ; 左境界 = X-1 (D は塗り開始点のまま)
                LEAX    -1,X
                STX     <PT_LX
                JSR     pt_fill                 ; [X..右境界-1] を塗る → D=右境界
                CMPD    <PT_RX
                BCC     pt_row_next                 ; スパンが右境界まで届いた → 行送り
                STD     <PT_TX                  ; 内部障害物の右端
                ADDD    #1
                JSR     pt_scan_rf              ; 同じ行に残る塗れる区間
                CMPD    <PT_RX
                BCC     pt_f1
                PSHS    A,B                     ; 残区間先頭 X
                LDD     <PT_LX
                PSHS    A,B                     ; 左境界退避
                LDD     2,S
                SUBD    #1
                STD     <PT_LX
                CLRA                            ; 残区間を type0 で予約
                JSR     pt_push
                PULS    A,B
                STD     <PT_LX                  ; 左境界復帰
                LEAS    2,S
pt_f1:
                LDD     <PT_TX
                STD     <PT_RX                  ; 右リミットを障害物まで縮める
                ; fallthrough pt_row_next

;--- 行送り + 右張り出し (type1 再開点) ----------------------------------------
pt_row_next:
                JSR     pt_advance              ; 次行計算 (範囲外は呼出元破棄→POP)
                LDD     <PT_RX
                SUBD    #1
                JSR     pt_scan_rb              ; 次行で右境界を探す
                STD     <PT_TX
                ADDD    #1
                CMPD    <PT_RX                  ; 右境界-1 が既に境界か (右が閉じた)
                BNE     pt_right_open
                LDD     <PT_RX
                SUBD    #2
                JSR     pt_scan_lf              ; 次行スパン内を右から探す
                CMPD    <PT_LX
                BLT     pt_pop                  ; 塗れる点なし → POP
                ADDD    #1
                STD     <PT_RX                  ; 右境界を縮める
                BRA    pt_left_edge
pt_right_open:
                LDD     <PT_OO                  ; --- 右が開いている: 現行へ一旦戻る ---
                STD     <PT_RO
                LDD     <PT_RX
                ADDD    #1
                JSR     pt_scan_rf              ; 現行で旧右境界の先に塗れる点?
                CMPD    <PT_TX
                BGE     pt_right_widen                 ; なし → リミット拡大のみ
                STD     <PT_TX                  ; --- 右張り出し ---
                JSR     pt_fill                 ; 現行の張り出し区間を塗る
                STD     <PT_RX
                LDA     #1                      ; いまの文脈を type1 で予約
                JSR     pt_push
                LDD     <PT_TX
                SUBD    #1
                STD     <PT_LX
                COM     <PT_RDIR                ; 方向反転して回り込みを処理
                BRA     pt_row_next
pt_left_open:
                LDD     <PT_OO                  ; --- 左が開いている: 現行へ ---
                STD     <PT_RO
                LDD     <PT_LX
                SUBD    #1
                JSR     pt_scan_lf              ; 現行で旧左境界の左に塗れる点?
                CMPD    <PT_TX
                BGT     pt_lov                  ; あり → 左張り出し
                LDD     <PT_TX
                STD     <PT_LX                  ; なし → 左リミット拡大
                BRA     pt_commit
pt_right_widen:
                LDD     <PT_TX
                STD     <PT_RX                  ; 右リミットを次行の境界まで拡大
                ; fallthrough pt_left_edge

;--- 左境界 + 左張り出し (type2 再開点) ----------------------------------------
pt_left_edge:
                JSR     pt_advance              ; 行オフセットを次行へ再計算
                LDD     <PT_LX
                ADDD    #1
                JSR     pt_scan_lb              ; 次行で左境界を探す
                STD     <PT_TX
                SUBD    #1
                CMPD    <PT_LX                  ; 左境界+1 が既に境界か (左が閉じた)
                BNE     pt_left_open
                LDD     <PT_LX
                ADDD    #2
                JSR     pt_scan_rf              ; 次行スパン内を左から探す
                CMPD    <PT_RX
                BGT    pt_pop                  ; 塗れる点なし → POP
                SUBD    #1
                STD     <PT_LX
pt_commit:
                LDD     <PT_NY                  ; 次行を現行としてコミット
                STD     <PT_CY
                LDD     <PT_LX
                CMPD    <PT_RX
                LBLT    pt_row_span                 ; スパンが残っている → 続行
                ; fallthrough pt_pop

;--- シード pop: 空なら PAINT 完了 ----------------------------------------------
pt_pop:
                LDX     <PT_SP
                CMPX    #PT_STK_TOP
                BEQ     pt_exit                 ; 空 = 完了
                CLRB                            ; (CC を先に消費: 直後の BPL は LDA の N を見る)
                LDA     ,X                      ; フラグ (bit7=方向標識)
                BPL     pt_po1
                COMA                            ; 方向 $FF: 全反転を戻す
                COMB
pt_po1:
                STB     <PT_RDIR
                TFR     A,B
                ANDB    #$03                    ; 左境界 X 上位 2bit (3 は -1 に正規化)
                CMPB    #$03
                BNE     pt_po2
                LDB     #$FF
pt_po2:
                STB     <PT_LX
                LDB     2,X
                STB     <PT_LX+1
                LSRA
                LSRA
                TFR     A,B
                ANDB    #$03                    ; 右境界 X 上位 2bit
                STB     <PT_RX
                LDB     1,X
                STB     <PT_RX+1
                CLR     <PT_CY
                LDB     3,X
                STB     <PT_CY+1
                LEAX    4,X
                STX     <PT_SP
                LSRA
                LSRA                            ; A = 再開タイプ (0/1/2)
                ASLA
                LDX     #pt_jtab
                JMP     [A,X]
pt_lov:
                STD     <PT_TX                  ; --- 左張り出し ---
                JSR     pt_scan_lb              ; 現行で張り出しの左境界
                STD     <PT_LX
                ADDD    #1                      ; [左境界+1 .. 張り出し右端] を塗る
                STD     <WK_LN_X0
                LDD     <PT_TX
                JSR     pt_hln
                LDA     #2                      ; いまの文脈を type2 で予約
                BSR     pt_push
                LDD     <PT_TX
                ADDD    #1
                STD     <PT_RX
                COM     <PT_RDIR                ; 方向反転
                LBRA    pt_row_next
pt_jtab:
                FDB     pt_row_span
                FDB     pt_row_next
                FDB     pt_left_edge

;--- pt_exit — 完了/エラー共通出口 ----------------------------------------------
;   SUBSYS_AV の組み立てでは av_end ($D430 を控えの値に戻して ALU を止める) へ行く。
pt_exit:
        IFDEF SUBSYS_AV
                JMP     av_end
        ELSE
                STA     >IO_VRAM_GATE           ; VRAM gate OFF (開きっぱなし防止)
                RTS
        ENDC

;--- pt_advance — 行送り: 次行 = 現行±1。0..199 外は呼出元を破棄して POP -------
;   (呼出深度 1 段 (pt_row_next/pt_left_edge からの JSR) に依存。ラッパ厳禁)
pt_advance:
                LDD     <PT_CY
                TST     <PT_RDIR
                BNE     pt_adp
                SUBD    #1
                BRA     pt_adc
pt_ado:
                LEAS    2,S                     ; 呼出元を破棄
                BRA     pt_pop
pt_adp:
                ADDD    #1
pt_adc:
        IFDEF SUBSYS_AV
                CMPD    <WK_VIEW_Y0             ; 描画範囲の外の行へは進まない
                BLT     pt_ado
                CMPD    <WK_VIEW_Y1
                BGT     pt_ado
        ELSE
                CMPD    #200                    ; 負は $FFFF で同時に弾く
                BCC     pt_ado
        ENDC
                STD     <PT_NY
                LDA     <PT_NY+1                ; 次行オフセット (現スキャン行に設定)
                LDB     #PT_COLS
                MUL
                STD     <PT_NO
                STD     <PT_RO
                LDA     <PT_CY+1                ; 現行オフセット (戻り先)
                LDB     #PT_COLS
                MUL
                STD     <PT_OO
                RTS

;--- pt_push — 現文脈 (左境界/右境界/行/方向) を 4byte パックで push -----------
;   入力 A = 再開タイプ (0/1/2)。満杯時は黙殺。
pt_push:
                LDX     <PT_SP
                CMPX    #PT_STK_BOT
                BEQ     pt_puf
                ASLA
                ASLA                            ; タイプ << 2
                LDB     <PT_RX
                ANDB    #$03
                PSHS    B
                ORA     ,S+                     ; | 右境界上位 2bit
                ASLA
                ASLA
                LDB     <PT_LX
                ANDB    #$03
                PSHS    B
                ORA     ,S+                     ; | 左境界上位 2bit
                EORA    <PT_RDIR                ; 方向 $FF は全反転 (bit7=標識)
                LEAX    -4,X
                STA     0,X
                LDB     <PT_RX+1
                STB     1,X
                LDB     <PT_LX+1
                STB     2,X
                LDB     <PT_CY+1
                STB     3,X
                STX     <PT_SP
pt_puf:
                RTS


;------------------------------------------------------------------------------
; hdlr_IRQ — 取り消し (COMMAND ABORT。参考書籍) の割り込み
;   最初に $D402 を読んで取り消しの要求の保持を消し (参考書籍: 読出しの
;   負パルスがキャンセル IRQ のアクノリッジ)、アボートフラグ (参考書籍。$FF = IRQ 発生)
;   を置く。保持を消さないと、要求が残る限り IRQ が入り続ける。
;   ROM の命令の実行中なら、実行中の命令を直ちに捨てて
;   READY へ戻る (参考書籍): FM77AV の描画の後始末をして応答完了へ跳ぶ (S と DP は
;   その先のメインループの先頭が既定に戻す)。IRQ を受けるのは命令の実行中と、転送された
;   処理が自分で許したときだけである。どちらであるかは、割り込まれた場所 (スタックに
;   積まれた PC) が ROM の中かどうかで見分ける (ワーク RAM に印を持たない)。$3F / $7F で
;   転送された処理の中では印を置いて戻るだけ (動作観察のまま)。
;   割り込まれた処理の DP は分からないので拡張アドレスで読み書きする。
; hdlr_SWI / hdlr_SWI2 / hdlr_SWI3 — 使わない (RTI のみ)
;------------------------------------------------------------------------------
hdlr_IRQ:
                TST     >$D402                  ; 読みで取り消しの要求の保持を消す
                LDA     #$FF
                STA     >SUB_ABRT
                LDA     10,S                    ; 割り込まれた場所 (PC の上位)
                CMPA    #reset_entry/256
                BCC     abort_cmd               ; ROM の中 = ROM の命令の実行中
hdlr_SWI:
hdlr_SWI2:
hdlr_SWI3:
                RTI
abort_cmd:
        IFDEF SUBSYS_AV
                LDA     #$D0
                TFR     A,DP            ; DP = $D0 (av_end が直接ページでワークを読む)
                JSR     av_end          ; ALU を止め $D430 を控えの値に戻す
        ENDC
                JMP     dispatch_done   ; 応答完了 (READY) へ。実行中の命令のスタックは
                                        ;   メインループの先頭が捨てる

;--- null_to_end — X = カーソルのセル。その次からフィールドの終りまで Null を探す ------
;   U = 見つけた Null のセル (無ければフィールドの終り)。C=1 見つけた。X 保存
null_to_end:
                PSHS    X
                BSR     fld_end_cur             ; U = フィールドの終り
                PSHS    U
                LDX     2,S
nte_lp:
                LEAX    1,X
                CMPX    ,S
                BCC     nte_none
                TST     ,X
                BNE     nte_lp
                TFR     X,U
                ORCC    #$01
                BRA     nte_ret
nte_none:
                ANDCC   #$FE
nte_ret:
                LEAS    2,S
                PULS    X,PC

;--- sf_fill1 — X の次のセルからフィールドの終りまで属性 WK_TXT_COL を置く --------
;   フィールドの終り = 次の F=1 のセル、またはカーソルを含むモード画面の終り
;   (参考書籍)。全レジスタ保存
sf_fill1:
                PSHS    A,B,X,U
                TFR     X,U
                BSR     ms_end_cur              ; X = モード画面の終り
                PSHS    X
                LDB     <WK_TXT_COL
sff_lp:
                LEAU    1,U
                CMPU    ,S
                BCC     sff_done
                LDA     CONBUF_ATTR-CONBUF_CHAR,U
                BMI     sff_done                ; 次のフィールドの先頭
                STB     CONBUF_ATTR-CONBUF_CHAR,U
                BRA     sff_lp
sff_done:
                LEAS    2,S
                PULS    A,B,X,U,PC

;--- fld_end_cur — U = カーソルを含むフィールドの終り (排他)。A,B,X 破壊 ----------
fld_end_cur:
                BSR     ms_end_cur              ; X = モード画面の終り
                PSHS    X
                JSR     con_cell
                TFR     X,U
fec_lp:
                LEAU    1,U
                CMPU    ,S
                BCC     fec_done
                LDA     CONBUF_ATTR-CONBUF_CHAR,U
                BPL     fec_lp
fec_done:
                LEAS    2,S
                RTS

;--- cell_next — バッファアドレスを 1 つ後ろへ ------------------------------------
;   行末では次の行の先頭へ、モード画面の終りではその先頭へ (参考書籍)。
;   全レジスタ保存
cell_next:
                PSHS    A,B
                INC     <WK_CURSOR_X
                LDA     <WK_CURSOR_X
                CMPA    <WK_SCREEN_W
                BCS     cn_ret
                CLR     <WK_CURSOR_X
                LDA     <WK_CURSOR_Y
                BSR     ms_rows                 ; A = 上端, B = 下端+1
                INC     <WK_CURSOR_Y
                CMPB    <WK_CURSOR_Y
                BHI     cn_ret
                CMPB    <WK_WIN_END              ; スクロールモード画面の終り (参考書籍) →
                BNE     cn_wrap                 ;   1 行上へスクロールし最下段の行の先頭へ
                CMPA    <WK_WIN_TOP
                BNE     cn_wrap
                DEC     <WK_CURSOR_Y
                JSR     scroll_up
                BRA     cn_ret
cn_wrap:
                STA     <WK_CURSOR_Y             ; ページモード画面の終り (参考書籍) → 先頭へ
cn_ret:
                PULS    A,B,PC

;--- ms_end_cur — X = カーソルを含むモード画面の終り (排他)。A,B 破壊 ---------------
ms_end_cur:
                LDA     <WK_CURSOR_Y
                BSR     ms_rows
                TFR     B,A

;--- row_addr — A = 行 → X = その行の先頭セル (CONBUF_CHAR + 行 × 桁数)。A,B 破壊 --
row_addr:
                LDB     <WK_SCREEN_W
                MUL
                JMP     cb_mkx0

;==============================================================================
; $04 GET / $05 GETC (参考書籍) と
; フィールド (参考書籍) の共通処理
;==============================================================================

;--- ms_rows — A = 行 → A = その行を含むモード画面の上端行、B = 下端行+1 --------
;   モード画面は参考書籍のとおり: スクロールモード画面 [SL, SL+NS)、その上が
;   ページモード 1 画面、下がページモード 2 画面。
ms_rows:
                CMPA    <WK_WIN_TOP
                BCS     msr_p1
                CMPA    <WK_WIN_END
                BCS     msr_sc
                LDA     <WK_WIN_END              ; ページモード 2 画面
                LDB     <WK_SCREEN_H
                RTS
msr_p1:
                CLRA                            ; ページモード 1 画面
                LDB     <WK_WIN_TOP
                RTS
msr_sc:
                LDA     <WK_WIN_TOP              ; スクロールモード画面
                LDB     <WK_WIN_END
                RTS

;------------------------------------------------------------------------------
; cmd_05_getc — $05 GETC (参考書籍)
;   GET の後、未転送の変更フィールドを画面最上段のものから順に返す。無ければ N=0。
;   相対 3 の K は直前の GET の終了キーコード。
;------------------------------------------------------------------------------
cmd_05_getc:
getc_body:
                LDA     <WK_SCREEN_H
                BSR     row_addr
                PSHS    X                       ; ,S = 画面の終り
                LDX     #CONBUF_CHAR
gb_find:
                LDA     CONBUF_ATTR-CONBUF_CHAR,X
                BITA    #$40                    ; M = オペレータによって変更された
                BNE     gb_found
                LEAX    1,X
                CMPX    ,S
                BCS     gb_find
                LEAS    2,S
                LDA     <GE_K
                CLRB
                BRA     gb_sp           ; 相対 3 = K、相対 4 = N = 0 (変更フィールドなし)
gb_found:
                LEAS    2,S
                PSHS    X                       ; 0,S = 変更セル
                BSR     addr_xy                 ; B = 行
                TFR     B,A
                BSR     ms_rows
                PSHS    A,B                     ; 0,S = 上端行 / 1,S = 下端行+1 / 2,S = 変更セル
                BSR     row_addr                ; X = モード画面の先頭
                LDU     2,S
gbs_lp:                                         ; 後ろへフィールドの先頭 (F) を探す
                LDA     CONBUF_ATTR-CONBUF_CHAR,U
                BMI     gbs_got
                PSHS    X
                CMPU    ,S++
                BLS     gbs_got                 ; モード画面の先頭 (非保護フィールド)
                LEAU    -1,U
                BRA     gbs_lp
gbs_got:
                STU     <GE_FS
                LDA     1,S                     ; 下端行+1
                BSR     row_addr                ; X = モード画面の終り
                LEAS    4,S
gbe_lp:                                         ; 前へフィールドの終りを探す
                LEAU    1,U
                PSHS    X
                CMPU    ,S++
                BCC     gbe_got
                LDA     CONBUF_ATTR-CONBUF_CHAR,U
                BPL     gbe_lp
gbe_got:
                STU     <GE_FE
                ; --- 応答: $12、X、Y、TEXT (制御コード $00〜$1F と $7F を除く) ---
                LDX     #$D385
                STX     <GE_PTR
                LDA     #$12
                BSR     ge_emit
                LDX     <GE_FS
                BSR     addr_xy                 ; A = 桁, B = 行
                BSR     ge_emit
                TFR     B,A
                BSR     ge_emit
gbt_lp:
                LDA     CONBUF_ATTR-CONBUF_CHAR,X
                ANDA    #$BF                    ; 転送したので M を落とす
                STA     CONBUF_ATTR-CONBUF_CHAR,X
                LDA     ,X
                CMPA    #$7F
                BEQ     gbt_nx
                CMPA    #$20
                BCS     gbt_nx
                BSR     ge_emit
gbt_nx:
                LEAX    1,X
                CMPX    <GE_FE
                BCS     gbt_lp
                LDD     <GE_PTR                  ; 最後のチャンク: N = 今回のバイト数
                SUBD    #$D385
                LDA     <GE_K
gb_sp:
                STD     >SH_PARAM               ; 相対 3 = K / 相対 4 = N
ps_ret:
                RTS

;--- addr_xy — X = セル → A = 桁、B = 行。X 保存 ----------------------------------
addr_xy:
                PSHS    X
                TFR     X,D
                SUBD    #CONBUF_CHAR
                LDX     #0                      ; X = 行
axy_lp:
                SUBB    <WK_SCREEN_W
                SBCA    #0
                BCS     axy_done
                LEAX    1,X
                BRA     axy_lp
axy_done:
                ADDB    <WK_SCREEN_W             ; B = 桁
                PSHS    B
                TFR     X,D                     ; B = 行
                LDA     ,S+                     ; A = 桁
                PULS    X,PC
;--- ge_emit — A を応答へ置く。120 バイトで S′=1 にして継続コマンドを待つ -----------
;   (参考書籍: N ≦ 120。相対 4 = N、相対 5〜 = DATA)。X 保存
ge_emit:
                PSHS    A,X
                LDX     <GE_PTR
                CMPX    #$D385+120
                BCS     gee_st
                LDD     #120
                LDA     <GE_K
                STD     >SH_PARAM               ; 相対 3 = K / 相対 4 = N = 120
                JSR     sh_cont_set             ; S′ = 1 を提示して継続コマンドを待つ
                LDX     #$D385
gee_st:
                LDA     ,S
                STA     ,X+
                STX     <GE_PTR
                PULS    A,X,PC

;--- put_stream — 相対 3 = N、相対 4〜 = 文字列 (オーダを含む) を表示する ----------
;   相対 1 の S′ (MSB) が 1 の間は応答を返さず、継続コマンド (参考書籍
;   : 相対 3 = 今回のバイト数、相対 4〜 = データ) を受けて続ける。
;   $03 PUT と $04 GET (参考書籍) の表示の部分。B/X 破壊。
put_stream:
                LDB     >SH_PARAM               ; B = 今回転送されたバイト数 (N)
                BEQ     ps_cont                 ; N=0 の回は出力せず継続判定だけ行う
                LDX     #$D384                  ; 出力文字列は相対 4 から
print_loop:
                LDA     ,X+
                JSR     putchar
                DECB
                BNE     print_loop
ps_cont:
                LDA     >SH_CNT                 ; 相対 1 = 継続フラグビット S′ (MSB)
                BPL     ps_ret                  ; S′=0 → 全て表示した
                JSR     sh_rereq_c              ; 清算して BUSY を落とし継続コマンドを待つ
                CLR     >SH_ERR                 ; 相対 0 = エラーコード (参考書籍)
                BRA     put_stream


;------------------------------------------------------------------------------
; $02 ERASE
;------------------------------------------------------------------------------
                ; (本体は ROM 末尾 c02_ext。表から直接指すのでスタブは置かない。
                ;  W ($D383) の指す範囲を BC ($D384) の背景色で消す。)

;------------------------------------------------------------------------------
; cmd_05_print — PRINT ($D383=文字数, $D384〜=文字列)
;------------------------------------------------------------------------------
; cmd_03_console — $03 PUT (参考書籍)。
;   相対 3 = N (文字数)、相対 4〜 = 文字列 (オーダを含む)。各文字を putchar で描画する。
;
;   カーソルを出したまま文字を描いてはならない。カーソルはセルの全高
;     (行ピッチに従い 8 / 10 ライン) を反転して見せる方式なので、出したまま
;     文字を重ねると反転が残り、次にカーソルを消すときに反転が逆に効いて
;     セルが化ける。しかも反転はその時点のカーソル位置に掛かるため、描画中に
;     位置が動くと「出した場所」と「消す場所」がずれて元へ戻せなくなる。
;     よって出力の間はカーソルを消し、
;     出し終えてから「出力前に出ていたか」どおりに出し直す。
;   相対 1 の MSB は継続フラグビット S′ である。S′=1 で送られた文字列は、
;     残りが継続コマンドで届く。継続コマンド
;     はコマンドコード $64、相対 1 = S′、相対 3 = 今回転送するバイト数 (1〜120)、
;     相対 4 以降 = 転送データ。よって S′=1 の間は応答を返さず、$64 を受けて
;     同じバッファアドレスから出力を続ける。
;   表示の本体 (put_stream) は $04 GET の文字列表示と共用する。
cmd_03_console:
                LDA     <WK_CURSOR_EN            ; 出す指定だったかを控える ($00/$FF)
                PSHS    A
                BSR     cursor_hide             ; 出力中はカーソルを伏せる
                BSR     put_stream              ; 相対 3=N、4〜=文字列を表示 (S′ で継続)
                CLR     >SH_PARAM               ; 相対 3 は 0 で返る (動作観察)
                ; --- 表示停止 (参考書籍) ---
                ;   プットウェイト (CF bit4) が有効で [ESC] が押されていれば、文字列を
                ;   表示し終えた後、次のキー入力まで待つ。
                LDA     <WK_CSCTL       ; 表示制御ワーク (CF の現在値)
                BITA    #$10                    ; bit4 = プットウェイト
                BEQ     c03_fin
                LDB     <KB_COUNT                ; キーバッファに溜まっているか
                BEQ     c03_fin
                LDX     <KB_RDPTR
                LDA     ,X                      ; 次に読まれるキー
                CMPA    #$1B                    ; [ESC]
                BNE     c03_fin
                JSR     kb_dequeue              ; [ESC] は表示停止の指示として消費する
                JSR     con_wait_key            ; 解除キーが押されるまで待機する
c03_fin:
                PULS    A                       ; 出力前の指定を戻す ($00/$FF)

;--- con_cur_apply — A の bit0 どおりにカーソルを出す/消す --------------------
;   $0C コンソール制御 (A = 表示制御ワークの値) と $03 出力後の復帰 (A = 出力前の
;   カーソル指定 $00/$FF) で共用する。どちらも「有効なら出す、無効なら消す」。
;   復帰は「出力前に出ていたか」で決める。表示状態のカーソル有効ビットは
;     起動時から立っており、そちらで決めるとプログラムが出していないカーソルまで
;     印字のたびに出てしまう (打点の有無や字の読み取りが崩れる)。
;   cursor_show / cursor_hide は現在値と希望値が同じなら何もしない。
con_cur_apply:
                LSRA                            ; C = bit0 (カーソル有効)
                BCS     cca_on
                BRA     cursor_hide             ; 末尾呼び (正常応答は RTS のみ)

;--- ge_prot — 保護 (P=1) のセルはオペレータに変更させない (参考書籍) --------
;   $04 GET の編集操作 (文字・DEL・BS・EL・TAB・DUP) の入口から JSR で呼ぶ。
;   カーソルのセルのアトリビュートの bit4 (P) が 1 なら、呼出元へは戻らず
;   キー待ち (g4_key) へ抜ける。0 なら A を保ったまま普通に戻る。
ge_prot:
                PSHS    A,B,X                   ; B は呼出元の作業値 (TAB / DUP の別)。
                JSR     con_cell                ;   con_cell が A と B を壊すので双方退避
                LDA     CONBUF_ATTR-CONBUF_CHAR,X
                ANDA    #$10                    ; P = 保護
                PULS    A,B,X                   ; (PULS は CC を変えない)
                BEQ     gep_ret
                TST     >$D403          ; 保護されたフィールドへの入力はベルを鳴らす
                LEAS    2,S             ;   (参考書籍)。呼出元へは戻らず
                BRA     g4_key                  ;   次のキーを待つ
gep_ret:
                RTS

;------------------------------------------------------------------------------
; cmd_0c_cursor — CONSOLE CONTROL ($0C、参考書籍)
;------------------------------------------------------------------------------
cmd_0c_cursor:
                ; コンソール制御 ($0C、参考書籍): 相対 3 = 制御フラグ CF を
                ;   表示制御ワーク ($D021) へ格納する。CF のビット割付は
                ;     bit0 = カーソル表示   bit1 = オーダ動作   bit2 = TAB 動作
                ;     bit3 = ページウェイト bit4 = プットウェイト bit5 = オート LF
                ;   で、読み手はそれぞれ con_cur_apply / ctl_order_act / pc_ht /
                ;   con_page_wait / cmd_03_console / putchar_cr にある。
                ; 応答データの相対 0 (エラーコード) は常に 0 = 正常終了で、受理時に
                ;   dispatch_cmd2 が 0 を置いているのでここでは何もしない。
                LDA     0,U                     ; CF
                STA     <WK_CSCTL       ; 表示制御ワーク ← CF
                BRA     con_cur_apply           ; bit0 どおりに出す/消す (共用)
cursor_hide:
                PSHS    A,B
                CLRA                            ; 希望 = 反転を外す (非表示)
curs_c:
                CMPA    <WK_CURSOR_ON
                BEQ     curs_e                  ; 既に希望の反転状態
        IFNDEF SUBSYS_TB
                JSR     cursor_toggle           ; セル反転 (A,B は保存される)
        ENDC
curs_e:
                TFR     A,B
                STD     <WK_CURSOR_ON            ; 反転状態 + 有効指定 を同時に更新
                PULS    A,B,PC
cca_on:

;--- cursor_show / cursor_hide — 控え付きのカーソル表示切替。A,B 保存 ----------
;   カーソルはセルの反転で見せるので、消す側は「いま反転が掛かっているか」を
;   知らないと逆に掛けてしまう。そこで反転状態を WK_CURSOR_ON に控える。
;   控えが希望と同じなら何もしない。有効指定 WK_CURSOR_EN は隣接アドレスなので
;   16bit で同時に書く。
cursor_show:
                PSHS    A,B
                LDA     #$FF                    ; 希望 = 反転を掛ける (表示)
                BRA     curs_c

;------------------------------------------------------------------------------
; cmd_04_get — $04 GET (参考書籍)
;   相対 3 = N、相対 4〜 = 文字列 (オーダを含む) を表示し (S′ なら $64 で続きを
;   受ける)、オペレータの操作 (参考書籍) を受けて、RETURN / CTRL+C / CTRL+X /
;   CLS で終了する。応答は getc_body (変更フィールドのなかで画面最上段のもの)。
;   応答: 相対 3 = K (0 RETURN / 1 CTRL+C, CTRL+X / 2 CLS)、4 = N、
;   5 = $12、6 = X、7 = Y、8〜 = TEXT。120 バイトを越えたら S′=1 + $64 (参考書籍)。
;   参考書籍の [SHIFT] 併用 (単語・フィールドの単位の移動) は、キーコードが
;   通常の移動キーと同じで区別できないので、通常の移動として扱う。
;------------------------------------------------------------------------------
cmd_04_get:
                BSR     cursor_hide
                BSR     put_stream              ; 文字列を表示
                ; 前回までの変更の印 (M) を消す
                LDA     <WK_SCREEN_H
                JSR     row_addr
                PSHS    X                       ; 画面の終り
                LDX     #CONBUF_CHAR
g4_mclr:
                LDA     CONBUF_ATTR-CONBUF_CHAR,X
                ANDA    #$BF
                STA     CONBUF_ATTR-CONBUF_CHAR,X
                LEAX    1,X
                CMPX    ,S
                BCS     g4_mclr
                LEAS    2,S
                CLR     <WK_SF_ADR               ; 表示した文字列の一連は終える
                CLR     <WK_SF_ADR+1
                CLR     <ED_INS
                STA     >$D40D          ; INS LED を消灯 (書込み。値は問わない)
                LDA     #$40
                STA     <GE_EDIT                 ; 以後に描くセルには M (変更) を立てる
g4_key:
                BSR     cursor_show
        IFDEF SUBSYS_TB                         ; Type-B の位置はカーソルを描かない (動作観察)
g4_wait:
                JSR     kb_dequeue
                TSTB
                BEQ     g4_wait
        ELSE
                LDX     #20                     ; 点滅の計数: 20 × 20 ms = 0.4 秒
g4_wait:
                JSR     kb_dequeue              ; Z = キー無し (B = 0)。X は保たれる
                LBEQ    g4_blink                ; キーが無い間は点滅の時刻を見る (g4_wait へ戻る)
        ENDC
                BSR     cursor_hide
                CLRB                            ; K = 0
                CMPA    #$0D                    ; [RETURN] (参考書籍)
                BEQ     g4_end
                INCB                            ; K = 1
                CMPA    #$03                    ; [CTRL]+[C] (参考書籍)
                BEQ     g4_end
                CMPA    #$18                    ; [CTRL]+[X]
                BEQ     g4_end
                INCB                            ; K = 2
                CMPA    #$0C                    ; [CLS] (参考書籍)
                BEQ     g4_cls
                CMPA    #$7F                    ; [DEL] (参考書籍)
                LBEQ    g4_del
                CMPA    #$20
                LBCC    g4_type                 ; 文字
                CMPA    #$1C
                BCS     g4_ctl
                CMPA    #$1E
                BCC     g4_updn
                JSR     pc_curmove              ; [←] [→] は同一行でラップ (参考書籍)
                BRA     g4_key
g4_updn:                                        ; [↑] [↓] は同一桁で、モード画面の中でラップ
                PSHS    A
                LDA     <WK_CURSOR_Y
                JSR     ms_rows                 ; A = 上端, B = 下端+1
                DECB                            ; B = 最下段
                LSR     ,S+                     ; C = bit0 (0: ↑ / 1: ↓)
                BCS     g4_dn
                CMPA    <WK_CURSOR_Y
                BNE     g4_up1
                STB     <WK_CURSOR_Y             ; 最上段 → 最下段へ
                BRA     g4_key
g4_dn1:
                INC     <WK_CURSOR_Y
                BRA     g4_key
g4_ctl:
                CMPA    #$0B                    ; [HOME] (参考書籍)
                BEQ     g4_home
                CMPA    #$12                    ; [INS] (参考書籍)
                BEQ     g4_ins
                CMPA    #$08                    ; [BS] (参考書籍)
                LBEQ    g4_bs
                CMPA    #$05                    ; [EL] (参考書籍)
                BEQ     g4_el
                CMPA    #$09                    ; [TAB] (参考書籍)
                BEQ     g4_tab
                CMPA    #$11                    ; [DUP] (参考書籍)
                BEQ     g4_dup
                BRA     g4_key                  ; その他は何もしない
g4_tab:                                         ; タブ動作 (CF bit2) が選ばれていれば次の停止位置まで SPACE で埋める
                LDA     <WK_CSCTL
                BITA    #$04
                BEQ     g4_key
                CLRB                            ; B = 0: SPACE を置く
                BRA     g4_fill
g4_dn:
                CMPB    <WK_CURSOR_Y
                BNE     g4_dn1
                STA     <WK_CURSOR_Y             ; 最下段 → 最上段へ
                BRA     g4_key
g4_up1:
                DEC     <WK_CURSOR_Y
                BRA     g4_kjmp                 ; 中継 (キー待ちまで短い分岐が届かない)
g4_end:
                STB     <GE_K
                CLR     <GE_EDIT
                LDA     <WK_CSCTL
                JSR     con_cur_apply           ; カーソルはコンソール制御の指定どおり
                LBRA    getc_body
g4_home:
                LDA     <WK_CURSOR_Y
                JSR     ms_rows
                STA     <WK_CURSOR_Y             ; モード画面の先頭へ
                CLR     <WK_CURSOR_X
                BRA     g4_kjmp
g4_ins:
                BRA     g4_insx         ; 本体は g4_kjmp の隣 (INS LED も切り替える)
g4_cls:                                         ; カーソルを含むモード画面を消し、入力を終える
                PSHS    B
                LDA     <WK_CURSOR_Y
                JSR     ms_rows
                PSHS    A,B
                JSR     vrows_clear             ; VRAM の行 A..B-1 を 0 に
                PULS    A,B
                PSHS    B
                JSR     row_addr                ; X = 先頭
                PULS    A
                TFR     X,U
                JSR     row_addr                ; X = 終り
g4c_lp:
                CLR     ,U+
                PSHS    X
                CMPU    ,S++
                BCS     g4c_lp
                PULS    B
                BRA     g4_end
g4_el:
                JSR     ge_prot                 ; 保護されたフィールドは変更しない
                JSR     el_body
                BRA     g4_kjmp
g4_put:
                JSR     draw_char_at_cursor
g4_putj:
                JSR     cell_next
                BRA     g4_kjmp
g4_dup:                                         ; 画面最上段でなければ、1 つ上の行の文字を停止位置まで複写
                TST     <WK_CURSOR_Y
                BEQ     g4_kjmp
                LDB     #$FF                    ; B = $FF: 上の行の文字を置く
g4_fill:
                JSR     ge_prot                 ; 保護されたフィールドは変更しない
                PSHS    B
g4f_lp:
                LDA     #' '
                TST     ,S
                BEQ     g4f_put
                JSR     con_cell
                TFR     X,D
                SUBB    <WK_SCREEN_W
                SBCA    #0
                TFR     D,X
                LDA     ,X                      ; 1 つ上の行の同じ桁の文字
g4f_put:
                JSR     draw_char_at_cursor
                JSR     cell_next
                TST     <WK_CURSOR_X
                BEQ     g4f_end                 ; 行を折り返したら終り
                JSR     tab_at
                BCC     g4f_lp
g4f_end:
                PULS    B
g4_kjmp:
                LBRA    g4_key          ; 近くからの分岐をここで中継する (短い分岐で届かせる)
g4_insx:                                        ; 挿入モードの印を反転し、INS LED を印に合わせる
                COM     <ED_INS         ;   ($D40D は読出しで点灯・書込みで消灯。
                BNE     g4_inson        ;    参考書籍)
                STA     >$D40D          ; 消灯 (書込み。値は問わない)
                BRA     g4_kjmp
g4_inson:
                TST     >$D40D          ; 点灯
                BRA     g4_kjmp
g4_type:                                        ; A = 文字
                JSR     ge_prot                 ; 保護されたフィールドは変更しない
                TST     <ED_INS
                BEQ     g4_put
                PSHS    A                       ; con_cell が入力文字を壊すので保存
                JSR     con_cell                ; X = カーソルのセル
                PULS    A
                TST     ,X
                BEQ     g4_put                  ; Null のセルには文字がそのまま入る
                PSHS    A
                JSR     null_to_end             ; U = カーソルの次以降の最初の Null
                PULS    A
                BCC     g4_kjmp         ; Null が無ければ挿入しない
                PSHS    X,U                     ; 0,S = X / 2,S = U (Null のセル)
g4i_lp:                                         ; [X, U) を 1 つ後ろへ
                LDB     -1,U
                STB     ,U
                LEAU    -1,U
                CMPU    ,S
                BHI     g4i_lp
                STA     ,X                      ; カーソルのセルに文字
                PULS    X,U
                LEAU    1,U
                JSR     redraw_rng              ; カーソルのセルから Null だったセルまで描き直す
                BRA     g4_putj         ; セルを 1 つ進めてキー待ちへ (g4_put の末尾と共用)
g4_bs:
                JSR     ge_prot                 ; 保護されたフィールドは変更しない
                JSR     at_fld_start
                BEQ     g4_kjmp         ; フィールドの先頭では何もしない
                BSR     cell_prev
                BRA     g4_del1                 ; 1 つ前のセルは同じフィールド (保護は同じ)
g4d_done:
                CLR     -1,X                    ; 最後に Null
                PULS    U
                JSR     redraw_rng
                BRA     g4_kjmp
g4_del:
                JSR     ge_prot                 ; 保護されたフィールドは変更しない
g4_del1:
                JSR     con_cell                ; X = カーソルのセル
                JSR     null_to_end             ; U = 次の Null (無ければフィールドの終り)
                PSHS    U
g4d_lp:                                         ; [X+1, U) を 1 つ前へ
                LEAX    1,X
                CMPX    ,S
                BCC     g4d_done
                LDA     ,X
                STA     -1,X
                BRA     g4d_lp

;--- cell_prev — バッファアドレスを 1 つ前へ (行頭では前の行の末尾へ) ----------------
cell_prev:
                PSHS    A
                DEC     <WK_CURSOR_X
                BPL     cp_ret
                LDA     <WK_SCREEN_W
                DECA
                STA     <WK_CURSOR_X
                DEC     <WK_CURSOR_Y
cp_ret:
                PULS    A,PC


;==============================================================================
; symbol_entry — $19 SYMBOL: 文字パターン (グリフ) を VRAM へ倍率付きで描画
;
;   SYMBOL 文 (参考書籍) の
;   下請けサブコマンド。メイン CPU は共有 RAM のパラメータ域に以下を置いて
;   $19 を発行する (入口の受け渡し):
;
;     $D383 = CL: 描画色 (0-7。0=黒も正当な色 = 重ね描きによる消去に使う)
;     $D384 = F : ファンクションコード (参考書籍)
;     $D385 = A : アングルコード (0-3。参考書籍)
;     $D386 = 横倍率 (magX、各グリフ画素を magX 倍幅で描画)
;     $D387 = 縦倍率 (magY、各グリフ画素を magY 倍高で描画)
;     $D388 = 描画開始 X 座標 (16-bit、ピクセル単位)
;     $D38A = 描画開始 Y 座標 (16-bit、ピクセル単位)
;     $D38C = 文字数
;     $D38D〜 = 描画する文字列 (ASCII、各文字 = フォント 8 byte)
;
;   各文字を font_base からグリフ (8x8) として取得し、立っている画素を
;   magX × magY の矩形ブロックとして pset_internal で打点する。ROM 末尾の
;   空き領域へ ORG 不要で配置。描画色は入口で WK_COLOR を退避して CL を設定し、
;   出口で復元する (sym_color_set / sym_color_fin、SYMBOL が描画色を汚さない)。
;
;   アングルコード A (参考書籍の相対 5) の扱い:
;     グリフ内の桁送り方向 (u) と行送り方向 (v) を、画面上の単位ベクトル
;     DU / DV として持ち、座標の進みをすべてベクトル加算 (sym_addvec) に
;     置き換えてある。指定座標 (X, Y) は常にグリフ原点 (u=0, v=0) の置かれる
;     点で、A が変わると画面上でその点が字の別の隅になる。
;       A=0 (ノーマル)     : u→右   v→下
;       A=1 (90゜左回転)   : u→上   v→右
;       A=2 (180゜左回転)  : u→左   v→上
;       A=3 (270゜左回転)  : u→下   v→左
;     文字列は u 方向へ 1 字あたり 8*W ドットずつ進む (参考書籍の図の矢印)。
;==============================================================================
symbol_entry:
        IFDEF SUBSYS_TB
;   Type-B の位置: 相対 3〜5 = 色 B,R,G / 6 = F / 7 = A / 8 = W / 9 = H / 10〜13 = X,Y /
;   14 = 文字数 / 15〜 = 文字列 (FM-7 の並びの色を 3 バイトにしたもの)。X が 0〜319・Y が
;   0〜199 を外れると $3F。点はラインドロワで 12 の面へ打つ。
                JSR     av_begin_e      ; 終わったら $D430 と ALU を戻す
                LDD     7,U
                CMPD    #AV_XMAX
                LBHI    gr_err_coord
                LDD     9,U
                CMPD    #199
                LBHI    gr_err_coord
                LEAU    2,U
        ENDC
                ; --- パラメータ取り込み ---
                LDA     9,U                     ; $D38C = 文字数 (0 は何も描かず、81 以上も誤りにしない)
                STA     <WK_SYM_CNT              ; 残文字数 (途中の処理はこの欄を使わない)
        IFDEF STAGE_ALT_RAM
                DECA
                CMPA    #79
                LBHI    gr_err_nchar            ; 参考書籍の 1〜80 の外は文字数の誤り
        ENDC
                JSR     sym_color_set           ; CL / F ($D383/$D384) を確定 (旧色退避)
                LBCS    gr_err_fn               ; F>5 (参考書籍は 0〜5)
        IFDEF SUBSYS_TB
                ; 色 B,R,G ($D381〜$D383) を 4 回ぶんの値にし、F ($D384) が
                ;   1 なら色 0、2〜4 なら ALU の演算にする
                LEAX    -2,U
                JSR     av_col4c
                LDA     1,U
                CMPA    #1
                BHI     tsc_op
                BNE     tsc_done
                LDX     #0                      ; 1 (PRESET) は色 0
                STX     <AV_COL
                STX     <AV_COL+2
                BRA     tsc_done
tsc_op:
                ORA     #$80
                STA     <AV_ALU
tsc_done:
        ENDC
                LDA     2,U                     ; $D385 = A (アングルコード)
                CMPA    #3
                LBHI    gr_err_param            ; 参考書籍の A は 0〜3
                LDB     #2
                MUL                             ; 表は 1 方向 2 byte
                ADDD    #sym_vtab               ; 表内 A の位置 = 行送り DV
                STD     <WK_SYM_VEC              ; 桁送り DU はその 2 byte 後
                LDA     3,U                     ; $D386 = W 文字横幅倍率 (0〜255)
                BEQ     sym_done        ; 0 倍は何も描かない
                STA     <WK_SYM_MAGX
                LDA     4,U                     ; $D387 = H 文字縦幅倍率 (0〜255)
                BEQ     sym_done
                STA     <WK_SYM_MAGY
                LDD     5,U                     ; $D388 = 開始 X (16-bit BE)
                STD     <WK_SYM_X
                LDD     7,U                     ; $D38A = 開始 Y (16-bit BE)
                STD     <WK_SYM_Y
                LEAX    10,U            ; 文字列先頭 (Type-B の位置は U が相対 5 を
                                                ;   指しているので同じ変位で届く)
                STX     <WK_SYM_PTR
                BRA     sym_char_loop
sym_done:                                       ; 近い所に置いて短い分岐で届かせる
                LBRA    sym_color_fin   ; WK_COLOR 復元 → 成功応答
sym_char_loop:
                LDA     <WK_SYM_CNT
                BEQ     sym_done        ; 残 0 → 終了
                ; --- 現文字のグリフ先頭算出 (font_base + char*8) ---
                LDX     <WK_SYM_PTR
                LDA     ,X+                     ; A = 文字コード
                STX     <WK_SYM_PTR              ; ポインタ前進
                LDB     #8
                MUL                             ; D = char*8
                ADDD    #font_base
                STD     <WK_SYM_GLYPH
                ; --- 1 文字内ピクセル基準を文字基準にセット ---
                LDD     <WK_SYM_X
                STD     <WK_SYM_CX               ; 文字内 X 走査開始
                LDD     <WK_SYM_Y
                STD     <WK_SYM_CY               ; 文字内 Y 走査開始 (行ごと更新)
                LDA     #8
                STA     <WK_SYM_ROW              ; 8 行
                LDX     <WK_SYM_GLYPH
sym_row_loop:
                LDA     ,X+                     ; A = この行のフォントパターン
                EORA    <WK_SYM_INV              ; NOT なら反転
                STA     <WK_SYM_PAT
                STX     <WK_SYM_GLYPH            ; グリフポインタ前進を退避
                ; この 1 行を magY 回複製描画
                LDA     <WK_SYM_MAGY
                STA     <WK_SYM_DUPY
sym_rowdup_loop:
                ; --- 1 行 (8 画素) を magX 倍幅で打点 ---
                LDD     <WK_SYM_CX               ; 打点位置を行基準へ戻す
                STD     <WK_PX_X
                LDD     <WK_SYM_CY
                STD     <WK_PX_Y
                LDA     #$80
                STA     <WK_SYM_BITM             ; ビット走査マスク (MSB から)
sym_col_loop:
                LDA     <WK_SYM_MAGX
                STA     <WK_SYM_DUPX             ; 1 画素を magX ドットに広げる
sym_dupx_loop:
                LDA     <WK_SYM_PAT
                BITA    <WK_SYM_BITM
                BEQ     sym_nodot               ; 画素 0 → 打点せず幅だけ進める
        IFDEF SUBSYS_TB
                LDD     <WK_PX_X                ; 1 点をラインドロワで引く
                STD     <WK_LN_X0
                STD     <WK_LN_X1
                LDD     <WK_PX_Y
                STD     <WK_LN_Y0
                STD     <WK_LN_Y1
                JSR     avd_line
        ELSE
                JSR     pset_internal           ; WK_PX_X/Y, WK_COLOR で 1 dot
        ENDC
sym_nodot:
                LDX     <WK_SYM_VEC
                LEAX    2,X                     ; 桁送り DU
                LDU     #WK_PX_X
                BSR     sym_addvec              ; 打点位置を 1 ドット桁送り
                DEC     <WK_SYM_DUPX
                BNE     sym_dupx_loop
                LSR     <WK_SYM_BITM             ; 次ビットへ
                BNE     sym_col_loop            ; マスクが 0 になるまで (8 画素)
                ; この複製行が終わったら行基準を 1 ドット行送りする
                LDX     <WK_SYM_VEC              ; 行送り DV
                LDU     #WK_SYM_CX
                BSR     sym_addvec
                DEC     <WK_SYM_DUPY
                BNE     sym_rowdup_loop
                ; 次のフォント行へ
                LDX     <WK_SYM_GLYPH
                DEC     <WK_SYM_ROW
                BNE     sym_row_loop
                ; --- 1 文字完了: 文字基準を桁送り方向へ 8*magX ドット進める ---
                ;     行基準はこの時点で用済みなので送りカウンタに流用する
                LDA     <WK_SYM_MAGX
                LDB     #8
                MUL                             ; D = magX*8
                STD     <WK_SYM_CX
sym_chadv_loop:
                LDX     <WK_SYM_VEC
                LEAX    2,X                     ; 桁送り DU
                LDU     #WK_SYM_X
                BSR     sym_addvec
                LDD     <WK_SYM_CX
                SUBD    #1
                STD     <WK_SYM_CX
                BNE     sym_chadv_loop
                DEC     <WK_SYM_CNT
                LBRA    sym_char_loop

;------------------------------------------------------------------------------
; sym_addvec / sym_vtab — SYMBOL の座標の進みを単位ベクトルの加算に一本化する
;   X = 進み方向 (符号つき 1 byte × 2 = 画面 X 成分, 画面 Y 成分)
;   U = 進める座標対 (16 bit の X, Y が並んだ 4 byte)
;   表は「↓ → ↑ ←」を 1 周ぶん余分に持たせた輪で、アングルコード A の位置が
;   行送り DV、その 2 byte 後が桁送り DU になる (参考書籍の図の関係)。
;------------------------------------------------------------------------------
sym_addvec:
                LDB     ,X                      ; 画面 X 成分 (-1 / 0 / +1)
                SEX
                ADDD    ,U
                STD     ,U
                LDB     1,X                     ; 画面 Y 成分
                SEX
                ADDD    2,U
                STD     2,U
                RTS
sym_vtab:
                FCB     0,1                     ; ↓ : A=0 の行送り / A=3 の桁送り
                FCB     1,0                     ; → : A=0 の桁送り / A=1 の行送り
                FCB     0,-1                    ; ↑ : A=1 の桁送り / A=2 の行送り
                FCB     -1,0                    ; ← : A=2 の桁送り / A=3 の行送り
                FCB     0,1                     ; ↓ (輪を閉じるための再掲)


;------------------------------------------------------------------------------
; cmd_15_line, cmd_16_line2 — ライン描画
;   $15: $D383=X0, $D385=Y0, $D387=X1, $D389=Y1, $D38B=色
;   $16: 前回終点起点で $D383=新終点 X, $D385=新終点 Y
;------------------------------------------------------------------------------
                ; 本体は line15_entry。入口のパラメータ配置は
                ; $D383=色 / $D385-=X0,Y0,X1,Y1 / $D38D=形状。

;--- sym_color_set / sym_color_fin — $19 SYMBOL 描画色の設定と復元 -------------
;   入口の受け渡し: CL=$D383 (カラー)、F=$D384 (ファンクション)。入口で現在の
;   WK_COLOR を退避してから CL (下位 3bit) を設定し、出口で復元する。
;   CL=0 (黒) も正当な描画色 (重ね描き消去) としてそのまま採用する。
sym_color_set:
                LDA     <WK_COLOR
                STA     <WK_SYM_SAVC             ; 旧描画色を退避
                CLR     <WK_SYM_INV
                LDA     1,U                     ; F (参考書籍は 0〜5)
                CMPA    #5
                BNE     scs_fc
                COM     <WK_SYM_INV              ; NOT (参考書籍) = 出力ビットパターンを反転して
                CLR     1,U                     ;   PSET で表示する
scs_fc:
                BRA     fig_color               ; CL / F を確定 (C を呼出元へ継承)
sym_color_fin:
                LDA     <WK_SYM_SAVC
                STA     <WK_COLOR                ; 描画色を復元 (SYMBOL は汚さない)
                RTS                             ; 正常応答

        IFNDEF SUBSYS_AV
;--- dr_edgetab / dr_filltab — 隅の変位表 (1 行 4 byte = X0,Y0,X1,Y1) ---------
;   変位は WK_RECT_X0 からの byte 変位 (0=X0 / 2=Y0 / 4=X1 / 6=Y1 / 8=現在行)。
dr_edgetab:
                FCB     0,2,4,2                 ; 上辺 (X0,Y0)-(X1,Y0)
                FCB     0,6,4,6                 ; 下辺 (X0,Y1)-(X1,Y1)
                FCB     0,2,0,6                 ; 左辺 (X0,Y0)-(X0,Y1)
                FCB     4,2,4,6                 ; 右辺 (X1,Y0)-(X1,Y1)
dr_filltab:
                FCB     0,8,4,8                 ; 塗潰しの 1 行 (X0,CY)-(X1,CY)
        ENDC

;--- fig_color / fn_color — 色 + ファンクションコードの確定 (共通処理) ---------
;   fig_color は相対 3 = CL / 相対 4 = F の配置 ($15 / $16 / $19) をそのまま
;   読む入口。fn_color は A = F / B = CL を直接受ける入口で、点ごとに CL と F を
;   持つ $17 が使う。
;   参考書籍のファンクションコードは PSET=0 / PRESET=1 / OR=2 / AND=3 /
;   XOR=4。F > 4 は不正 (C=1 で拒否 = 描かない)。F = 1 (PRESET) は CL を無視して
;   背景色 (WK_BGCOL) を描画色とし、合成はしない。合格時 C=0 で WK_COLOR と
;   WK_PB_FN (打点の合成に使うファンクションコード) を確定する。A/B 破壊。
fig_color:
                LDB     0,U                     ; CL
                LDA     1,U                     ; F (ファンクションコード)
fn_color:
                CMPA    #4
                BHI     fgc_rej
                CMPA    #1
                BNE     fgc_set
                LDB     <WK_BGCOL               ; PRESET: 描画色 = 背景色
fgc_set:
                ANDB    #$07
                STB     <WK_COLOR
                CMPA    #2
                BCC     fgc_fn                  ; F >= 2 は合成コードをそのまま使う
                CLRA                            ; F = 0 / 1 は上書き (PSET)
fgc_fn:
                STA     <WK_PB_FN
                ANDCC   #$FE                    ; C=0 合格
                RTS
fgc_rej:
pbc_bad:
                ORCC    #$01                    ; C=1 拒否 (呼出元は描かず復帰)
                RTS

;--- gr_xy — 座標 1 組 (X,Y) の画面内判定つき転記 (共通処理) -------------
;   入力: X = 読み元 (X 座標 2 byte → Y 座標 2 byte の順に並ぶこと)
;         Y = 書き先 (同じ並びの 2 ワード (2 バイト))
;   出力: C=1 … 画面内。2 ワードとも転記済で X/Y は 4 byte 進む
;         C=0 … 画面外。違反ワードは転記せず打切る (呼出側は描画を取止める)
;   画面内は X が 0-639、Y が 0-199。負数は符号無し比較で巨大値になるため
;   同じ判定で範囲外になる。RTS は CC を変えないので直後に BCC/BCS で分岐できる。
;   D 破壊。$15 の端点・$17 の点列・$18 の種点で共用する。
gr_xy:
                LDD     ,X++                    ; X 座標
                CMPD    #640
                BCC     gr_xy_rts               ; X >= 640 → 範囲外 (C=0)
                STD     ,Y++
                LDD     ,X++                    ; Y 座標
                CMPD    #200
                BCC     gr_xy_rts               ; Y >= 200 → 範囲外 (C=0)
                STD     ,Y++
gr_xy_rts:
                RTS
        IFNDEF SUBSYS_AV
l15_line:
                JSR     draw_line
        ENDC

;==============================================================================
; pbb — $1C/$1E 任意幅ビット連続バイトブリットエンジン (ROM 末尾空き領域)
;
;   pb1_fast/pb2_fast のガード不合格形 (幅 8 非倍数・計数不足・分割転送) の
;   受け皿。動作観察のビット連続詰め (行末でバイト境界へ切上げない) を
;   16bit ビット FIFO で位相合わせし、左右端マスク付きのバイト単位書込で
;   1 行を一括処理する。ドット単位の経路 (1 ドット毎の Y*80/X>>3/マスク
;   シフト再計算) は座標範囲外の防御フォールバックとしてのみ残す。
;   パターン供給は pbs_next 経由 = $D38D 計数尊重・$D400 ガード・$64 分割
;   転送継続をそのまま継承する。
;
;   ワークは PutBlock 高速経路の固定ワーク (PBF_*) を別名で全面流用する
;   (pbf 経路と本エンジンは同一コマンドの排他経路なので干渉しない):
;==============================================================================
PBB_ACC         EQU     PBF_ROWST       ; 2: ビット FIFO (16bit、MSB 詰め)
PBB_CNT         EQU     PBF_SHC         ; 1: FIFO 内有効ビット数 (0..15)
PBB_BCNT        EQU     PBF_CNT         ; 1: 行内出力バイトカウンタ
PBB_EM          EQU     PBF_PREV        ; 1: 現出力バイトの端マスク
PBB_QN          EQU     PBF_C           ; 1: 行末バイトの取得ビット数 = (X1&7)+1

;--- pbb_coord — 座標範囲ガード。C=1 → 範囲外 (ドット単位の防御経路へ) --------
pbb_coord:
                LDD     ,U              ; X0 > X1 は不正
                CMPD    4,U
                BHI     pbc_bad
                LDD     4,U             ; X1 > 639 は範囲外
                CMPD    #639
                BHI     pbc_bad
                LDD     2,U             ; Y0 > Y1 は不正
                CMPD    6,U
                BHI     pbc_bad
                LDD     6,U             ; Y1 > 199 は範囲外
                CMPD    #199
                BHI     pbc_bad
                ANDCC   #$FE
                RTS

        IFNDEF SUBSYS_AV
line15_entry:
                BSR     fig_color               ; 色+描画ファンクション確定 ($D383/$D384)
                BCS     gr_err_fn               ; F>4 (参考書籍は 0〜4)
                ; 座標 4 ワード ($D385=X0, $D387=Y0, $D389=X1, $D38B=Y1) を範囲検査
                ; しながら WK_LN_X0/Y0/X1/Y1 (連続 4 ワード) へ取り込む。1 ワードでも
                ; 画面外なら何も描かずに正常終了する。
                LEAX    2,U
                LDY     #WK_LN_X0
l15_chk:
                BSR     gr_xy
                BCC     gr_err_coord            ; 端点が画面外 (参考書籍)
                CMPY    #WK_LN_Y1+2
                BCS     l15_chk
                LDA     10,U                    ; $D38D = 形状
                BEQ     l15_line
                ; 矩形形状は境界を WK_RECT_* へ複写してから do_rect_pre へ渡す
                ; (do_rect は行ごとに WK_LN_* を書き換えるため別領域が要る)。
                LDX     #WK_LN_X0
                LDY     #WK_RECT_X0
l15_copy:
                LDD     ,X++
                STD     ,Y++
                CMPY    #WK_RECT_Y1+2
                BCS     l15_copy
                LDA     10,U
                CMPA    #1
                BEQ     l15_box
                ; D38D>=2 = 塗潰し矩形 (BOXFILL)。
                LDA     #1                      ; do_rect モード 1 = 塗潰し
                BRA     l15_rect
l15_box:
                CLRA                            ; do_rect モード 0 = 枠
l15_rect:
                JMP     do_rect_pre
        ENDC

;------------------------------------------------------------------------------
; point_entry — POINT ($17)。
;   $D383=N (表示点数 1-20)。各点 n は 6 バイト周期:
;     Xn = $D384+6(n-1) (16bit, 0-639)
;     Yn = $D386+6(n-1) (16bit, 0-199)
;     CLn= $D388+6(n-1) (カラー 0-7)
;     Fn = $D389+6(n-1) (ファンクションコード 0-4)
;   本実装は内部打点 pset_internal を用いる。ファンクションコード Fn は
;   fn_color が WK_PB_FN へ確定し、pb1e_op が OR/AND/XOR を 3 面それぞれへ
;   適用する (PSET 固定ではない)。
;   範囲外座標: 画面外の座標を持つ点に達したらそこで打切り、以降の点も
;   打たない。$15/$18 と同じく「範囲外は描かない」で揃える。
;------------------------------------------------------------------------------
point_entry:
                LDA     0,U                     ; $D383 = N
                DECA                            ; N-1: 0 点と 21 点以上を一括で弾く
                CMPA    #19                     ;   (N=0 は $FF になり符号無し比較で外れる)
                BHI     gr_err_ncoord           ; 参考書籍の表示点数は 1〜20
        IFDEF SUBSYS_TB
                STA     <TB_M                   ; 残りの点の数 - 1 (相対 3 は変えない。動作観察)
        ENDC
                LEAU    1,U             ; U → 点パラメータ先頭
pt_loop:
        IFDEF SUBSYS_TB
;   Type-B の位置: 点ごとに X,Y (各 2 バイト)・色 B,R,G (各 4 ビット)・演算の 8 バイト。
;   X が 0〜319・Y が 0〜199 を外れると $3F、演算 5 以上は $40。演算 1 は色 0 の PSET。
;   1 ライン 40 バイトの 12 の面へ打つ (動作観察)。
                TFR     U,X
                LDY     #WK_PX_X
                BSR     gr_xy
                BCC     gr_err_coord
                LDD     <WK_PX_X
                CMPD    #AV_XMAX
                BHI     gr_err_coord
                JSR     tb_rgb                  ; X = 色の位置 (点の先頭 +4)
                STD     <TB_FW
                LDA     7,U                     ; 演算
                CMPA    #4
                BHI     gr_err_fn
                CMPA    #1
                BHI     tpt_fn
                BNE     tpt_ps
                CLR     <TB_FW
                CLR     <TB_FW+1
tpt_ps:
                CLRA
tpt_fn:
                STA     <WK_PB_FN
                PSHS    U
                JSR     tb_pix
                PULS    U
                LEAU    8,U
                DEC     <TB_M
                BPL     pt_loop
                RTS
        ELSE
                TFR     U,X                     ; 点 n のパラメータ先頭
                LDY     #WK_PX_X                ; WK_PX_X/WK_PX_Y は 2 ワード (2 バイト) 連続
                JSR     gr_xy                   ; 範囲検査つき転記
                BCC     gr_err_coord            ; 画面外の点 (参考書籍)
                LDA     5,U                     ; Fn (参考書籍の相対 9+6(n-1))
                LDB     4,U                     ; CLn (同 相対 8+6(n-1))
                JSR     fn_color                ; 色 + ファンクションコードを確定
                BCS     gr_err_fn               ; F>4 (参考書籍は 0〜4)
                JSR     pset_internal           ; A,B,X,Y,U を保存して打点
                LEAU    6,U                     ; 次の点へ (+6)
                DEC     >SH_PARAM               ; N--
                BNE     pt_loop
        ENDC
pt_done:
bc_stop:
                RTS                 ; 直前の JMP cmd_stub_ok を共用

;--- 図形系コマンドのパラメータ誤り (参考書籍のエラーコード) --------------
;   各コマンド本体は既に不正パラメータを検出して「描かずに応答」しているので、
;   その分岐先をここへ向け替えて、参考書籍が定めるコードを添えるだけにする。
;   5 つの入口は「値を A に置いて cmd_err へ落ちる」だけなので、後続の LDA を
;   CMPX #imm の即値として読み飛ばす形 (FCB $8C) で 1 本につないである。
;   CMPX が壊すのは CC だけで、cmd_err は CC を見ない。
        IFDEF STAGE_ALT_RAM
gr_err_nchar:
                LDA     #$42                    ; 文字数の誤り
                FCB     $8C                     ; 次の LDA #$45 を即値として読み飛ばす
        ENDC
gr_err_param:
                LDA     #$45                    ; コマンド、パラメータの誤り
                FCB     $8C                     ; 次の LDA #$43 を即値として読み飛ばす
gr_err_ncol:
                LDA     #$43                    ; 色数の誤り
                FCB     $8C                     ; 次の LDA #$41 を即値として読み飛ばす
gr_err_ncoord:
                LDA     #$41                    ; 座標数の誤り
                FCB     $8C                     ; 同上 (LDA #$40)
gr_err_fn:
                LDA     #$40                    ; ファンクションコードの誤り
                FCB     $8C                     ; 同上 (LDA #$3F)
gr_err_coord:
                LDA     #$3F                    ; グラフィック座標値の誤り

;==============================================================================
; コマンドハンドラ群
;==============================================================================

;------------------------------------------------------------------------------
; cmd_err / cmd_stub_ok — コマンド処理の共通出口
;   cmd_err     : A = 参考書籍のエラーコード。共有 RAM 相対 0 (参考書籍
;                 の ERROR CODE) へ置いて復帰する。コマンドは実行しない。
;   cmd_stub_ok : 正常応答。相対 0 は受理時に 0 化済み、相対 1 は応答完了処理が
;                 0 に戻すので、ここでは何もせず戻ればよい (= ハングしない)。
;------------------------------------------------------------------------------
cmd_err:
                STA     >SH_ERR
cmd_stub_ok:
                RTS



;------------------------------------------------------------------------------
; cmd_17_circle, cmd_19_paint — 円描画 / PAINT (簡易実装)
;------------------------------------------------------------------------------

                ; 本体は別ルーチンに配置。jmp で飛ばす。


; cmd $3D Set Timer / $3E Read Timer — 本体は別ルーチン (settimer_entry/
;   readtimer_entry) に配置。ここは JMP スタブのみ。

;------------------------------------------------------------------------------
; cmd_1b_getblock1 / cmd_1c_putblock1 / cmd_1d_getblock2 / cmd_1e_putblock2
;   矩形の読出し・書込みコマンド。共有 RAM 経由の受け渡しは動作観察による。
;   本体は別ルーチンに配置。stub から JMP で飛ぶ。
;------------------------------------------------------------------------------

;------------------------------------------------------------------------------
; cmd_3f_jmp — 拡張コマンド $3F: メイン CPU 指定の任意サブ CPU アドレスへジャンプ
;   $D38B-$D38C にジャンプ先アドレス (16-bit big-endian)
;   JSR ではなく JMP で渡す (戻ってこなくてもよい)。
;------------------------------------------------------------------------------
;------------------------------------------------------------------------------
; cmd_3f_bytecode — 拡張コマンド $3F のバイトコードインタプリタ
;
; メイン CPU が $D38B-$D3FF にバイトコード列を配置し、本ハンドラが解釈実行する。
;
; オペコード (参考書籍と動作観察に基づく):
;   $90        : STOP (RTS)
;   $91 src dst len : MEMCPY (各 16-bit BE)
;   $92 addr   : GOTO (バイトコード PC = 指定アドレス)
;   $93 addr   : CALL (JSR addr、ワーク RAM 内の 6809 機械語ルーチン実行)
;   その他      : エラー終了 (参考書籍の $45 = コマンド、パラメータの誤り)
;
; アドレスはディスパッチテーブル経由で呼ばれるため $E000 台で OK。
;------------------------------------------------------------------------------
cmd_3f_bytecode:
                ORCC    #$10            ; 転送された処理の中では取り消しの IRQ を受けない
                                        ;   (転送された処理が IRQ を許したときは印を置くだけ)。
                                        ;   渡す前にワーク RAM へは何も書かない
                LEAX    8,U             ; バイトコード先頭
bc_loop:
                LDA     ,X+                     ; A = opcode
                SUBA    #$90                    ; A = opcode - $90。$90 以上は借りが出ず C = 0
                BCS     bc_error                ; opcode < $90 → エラー
                BEQ     bc_stop                 ; $90 → STOP
                                                ; 以下 DECA / LDU / PSHS は C を変えないので、
                                                ;   CALL は必ず C = 0 で飛び先を呼ぶ
                DECA
                BEQ     bc_memcpy               ; $91 → MEMCPY
                DECA
                BEQ     bc_goto                 ; $92 → GOTO
                DECA
                BEQ     bc_call                 ; $93 → CALL
                ; $94+ → エラー
bc_error:
                ; 誤りは共有 RAM 相対 0 (参考書籍の ERROR CODE) へ置く。
                ;   相対 1 は STATUS (PF 割込コード / 各種タイマ割込 / 継続フラグ)
                ;   のビット割付なので、エラーコードを置いてはならない。
                LDA     #$45                    ; コマンド、パラメータの誤り (参考書籍)
                BRA     cmd_err
bc_call:
                LDU     ,X++                    ; U = CALL 先アドレス
                PSHS    X,U                     ; バイトコード PC と飛び先を退避
                JSR     [2,S]                   ; 6809 ルーチン実行
                PULS    X,U                     ; PC 復元
                BRA     bc_loop
bc_goto:
                LDX     ,X                      ; X = 次の 2 byte をアドレスとして使用
                BRA     bc_loop
bc_memcpy:
                LDD     ,X                      ; D = 転送元
                LDU     2,X                     ; U = 転送先
                LEAX    6,X                     ; オペランド 6 byte を消費
                LDY     -2,X                    ; Y = 長さ (Z = len==0)
                PSHS    X                       ; PC 退避
                TFR     D,X                     ; X = 転送元
                BEQ     bc_memcpy_done          ; len=0 ならスキップ (Z は LDY のまま)
bc_memcpy_loop:
                LDB     ,X+
                STB     ,U+
                LEAY    -1,Y
                BNE     bc_memcpy_loop
bc_memcpy_done:
                PULS    X                       ; PC 復元
                BRA     bc_loop

;------------------------------------------------------------------------------
; hdlr_NMI — 周期 NMI によるタイマ機能の計時 (参考書籍)
;   ※ NMI は DP 不定の文脈でも入るため、入口で DP=$D0 を確立する (RTI が復元する)。
;------------------------------------------------------------------------------
hdlr_NMI:
                LDA     #$D0
                TFR     A,DP                    ; DP=$D0 を確立 (RTI が復元)
                ; NMI は全レジスタ自動退避 (E フラグ) 済みで RTI が全復元するため、
                ;   手動の PSHS/PULS は不要。
                ; --- $D00A ガード: セット中は作業スキップ ---
                TST     <TM_GUARD
                BNE     nmi_skip_work
                JSR     nmi_subwork             ; タイマ処理 (拡張アドレスのまま。BSR へ
                                                ;   縮めない)
nmi_ret:                                        ; nmi_subwork はこの戻りアドレスで周期 NMI からの呼出しを見分ける
nmi_skip_work:
                CLR     <TM_GUARD
                RTI                             ; E=1 で全レジスタ復元

;==============================================================================
; ドット単位書込ヘルパは持たない (書込はバイトブリットエンジンに一本化)。
;==============================================================================

;==============================================================================
; chain16_entry — cmd $16 CHAIN 本体
;   パラメータ: $D383 = 色 (下位 3bit) / $D384 = ファンクション (0-4 のみ有効) /
;               $D385 = 点数 N (2-30) / $D386〜 = N 組の (X 16bit, Y 16bit)
;   N-1 本の連結線分 (ポリライン) を描画する。範囲検査は $15 と同方針:
;   1 点でも X >= 640 / Y >= 200 (負数は符号無し比較で巨大値) があれば一切
;   描画しない。これにより VRAM 外への打点流出が構造的に発生しない。
;   ワーク: 点/線分カウンタは Y レジスタに置く (RAM を 1 byte も占めない)。
;   draw_line は Y を保存するため線分ループを跨いで保持できる。
;==============================================================================

chain16_entry:
                JSR     fig_color               ; 色+描画ファンクション確定 ($D383/$D384)
                BCS     gr_err_fn               ; F>4 (参考書籍は 0〜4)
                LDB     2,U                     ; $D385 = 座標点数 N (参考書籍: 2〜30)
                CMPB    #2
                BCS     gr_err_ncoord
                CMPB    #30
                BHI     gr_err_ncoord
                CLRA                            ; --- 全点の範囲検査 ---
                TFR     D,Y                     ; Y = 残点数 N
                LEAX    3,U
c16_vloop:
                LDD     ,X++                    ; X 座標
                CMPD    #640
                BCC     gr_err_coord            ; 参考書籍は 0〜639
                LDD     ,X++                    ; Y 座標
                CMPD    #200
                BCC     gr_err_coord            ; 参考書籍は 0〜199
                LEAY    -1,Y
                BNE     c16_vloop
                ; --- 合格: 連結線分 N-1 本を描画 ---
        IFDEF SUBSYS_AV
                JSR     av_begin_e      ; 終わったら $D430 と ALU を戻す
                LDA     <WK_COLOR               ; 色 (Type-B の位置は 4 層とも同じ値)
        IFDEF SUBSYS_TB
                TFR     A,B
                STD     <AV_COL
                STD     <AV_COL+2
        ELSE
                STA     <AV_COL
        ENDC
                LDA     <WK_PB_FN               ; OR / AND / XOR は ALU の演算で
                BEQ     c16_fn
                ORA     #$80
                STA     <AV_ALU
c16_fn:
        ENDC
                LDB     2,U
                DECB
                CLRA
                TFR     D,Y                     ; Y = 残線分数 = N-1
                LEAX    3,U
                LDD     ,X++                    ; 第 1 点 = 始点
                STD     <WK_LN_X0
                LDD     ,X++
                STD     <WK_LN_Y0
c16_dloop:
                LDD     ,X++                    ; 次点 = 終点
                STD     <WK_LN_X1
                LDD     ,X++
                STD     <WK_LN_Y1
        IFDEF SUBSYS_AV
                JSR     avd_line                ; (X / Y は avd_line が保存)
        ELSE
                JSR     draw_line               ; (X は draw_line が保存)
        ENDC
                LDD     <WK_LN_X1                ; 終点を次線分の始点へ
                STD     <WK_LN_X0
                LDD     <WK_LN_Y1
                STD     <WK_LN_Y0
                LEAY    -1,Y
                BNE     c16_dloop

;==============================================================================
; キャラクタブロック GET@/PUT@ ($06-$09)
;   矩形 ($D383=X1, $D384=Y1, $D385=X2, $D386=Y2、文字座標) のセルを
;   コンソールバッファ (文字 $C000 / 属性 +$7D0) と VRAM で読み書きする。
;   GET 応答: $D383=計数, $D384〜=データ (形式2 は 属性,文字 の順)。応答バッファが
;   共有 RAM 終端 ($D400) に達したら $D381 bit7 を立てて分割応答し、継続コマンド
;   $64 で次チャンクへ進む (応答側の継続の手順)。
;   PUT 入力: $D387=固定属性(形式1), $D388=データ計数, $D389〜=データ。チャンク
;   枯渇時は pbs_next の受信側 $64 継続を共用する。
;==============================================================================

;--- cb_valid — A=桁, B=行 が画面範囲内なら C=1 -------------------------------
cb_valid:
                CMPA    <WK_SCREEN_W
                BCC     cbv_bad
                CMPB    <WK_SCREEN_H
                BCC     cbv_bad
                ORCC    #$01                    ; C=1 (範囲内)
                RTS
cbv_bad:
                ANDCC   #$FE                    ; C=0 (範囲外)
                RTS

;==============================================================================
; 矩形読出し/書込み 1・2 の実装 ($1B/$1C/$1D/$1E)
;   仕様根拠: 共有 RAM 経由の受け渡しの動作観察。
;   - 矩形読出し 1: 矩形を指定色マッチで単色ビットイメージとして読出
;   - 矩形書込み 1: 単色ビットイメージを矩形へ書込
;   - 矩形読出し 2: 矩形の 3 原色プレーン (B/R/G) をそのまま読出
;   - 矩形書込み 2: 3 原色プレーンを矩形へ書込
;
;   応答プロトコル (矩形読出し 1・2 共通):
;   - $D383 = 当該チャンクのデータ計数、データ本体は $D384 から (動作観察)
;   - 出力は cbg_emit (有界エミッタ) 経由。124 byte で $D381 bit7 を立てて
;     応答側継続ハンドシェイク (メインの継続コマンド $64 待ち) → $D384 から再開。
;     共有 RAM ($D384-$D3FF) の外には何 byte あっても絶対に書かない
;     (上限無しの直書きは I/O 領域 $D408 を汚して画面を消灯させる)
;==============================================================================

        IFNDEF SUBSYS_TB
;------------------------------------------------------------------------------
; getblock1_entry — $1B 矩形読出し 1 本体
;   入力: $D383=X1, $D385=Y1, $D387=X2, $D389=Y2 (各 2byte BE)
;         $D38B=指定色数 n (1-8)、$D38C..=色コードリスト (どれか一致で bit=1)
;   出力: $D383=チャンク計数、$D384+ にビットイメージ
;         (左上から右、上から下、MSB 左。行末で切上げないビット連続詰めで、
;          全行終了時のみ余りを 0 詰め = 総 byte 数は ceil(幅*高/8))
;   色数が 0 または 9 以上は無効指定 → 計数 0 の空応答 (描かず正常応答の方針)
;------------------------------------------------------------------------------
getblock1_entry:
                ; 入力をローカルへコピー (出力で上書きされる前に)
                JSR     rect_from_param         ; $D383-$D38A → WK_RECT_X0/Y0/X1/Y1
                LDB     8,U                     ; $D38B = 指定色数 n
                BEQ     gb1_badn                ; n=0 は無効
                CMPB    #8
                BHI     gb1_badn                ; n>8 も無効
                STB     <WK_GB_NCOL
                LEAX    9,U             ; 色リストも退避 (応答で上書きされる)
                LDY     #WK_GB_COLS
gb1_ccpy:
                LDA     ,X+
                ANDA    #$07                    ; 色は下位 3 bit のみ有効
                STA     ,Y+
                DECB
                BNE     gb1_ccpy
                CLR     <WK_GB_TGTCOL            ; 走査モード = 色リスト一致
                ; 応答初期化: $D383 = 計数 0、データは $D384 から
                JSR     cb_resp0        ; $D383 = 計数 0 / 応答は $D384 から
                BRA     gb_scan                 ; 末尾呼び
gb1_badn:
                CLR     0,U                     ; 無効指定 → 計数 0 の空応答
                LDA     #$43                    ; 色数の誤り (参考書籍)
                JMP     cmd_err
        ENDC

;--- cb_parse — $D383-$D386 を検証し走査状態を初期化 (C=1 成功) ---------------
;   両端点を検証後、桁/行とも min/max へ正規化する (動作観察として逆順指定も許容)。
cb_parse:
                LDD     0,U                     ; A=X1, B=Y1
                BSR     cb_valid
                BCC     cbp_rts
                LDD     2,U                     ; A=X2, B=Y2
                BSR     cb_valid
                BCC     cbp_rts
                LDA     0,U                     ; 桁の正規化
                LDB     2,U
                CMPA    2,U
                BLS     cbp_xo
                EXG     A,B                     ; A=min, B=max
cbp_xo:
                STA     <CB_COL
                STA     <CB_CMIN
                STB     <CB_CMAX
                LDA     1,U                     ; 行の正規化
                LDB     3,U
                CMPA    3,U
                BLS     cbp_yo
                EXG     A,B
cbp_yo:
                STA     <CB_ROW
                STB     <CB_RMAX
                ORCC    #$01
cbp_rts:
                RTS

;------------------------------------------------------------------------------
; $1C の汎用経路は任意幅対応のバイトブリットエンジン (pbb1_entry、ROM 末尾)
;   が受け持つ。座標範囲外は動作観察のエラー条件であり「描かず正常応答」
;   ($15/$16 と同方針)。
;------------------------------------------------------------------------------

        IFNDEF SUBSYS_TB
;------------------------------------------------------------------------------
; getblock2_entry — $1D 矩形読出し 2 本体
;   入力: $D383-$D38A=矩形
;   出力: $D383=チャンク計数、$D384+ に 3 プレーン (B/R/G) のバイトデータ
;   各プレーンはビット連続詰めで、プレーン末尾のみ 0 詰めフラッシュ
;   (= プレーンごとに ceil(幅*高/8) byte)。チャンクはプレーン境界と
;   無関係に 124 byte 区切り (cbg_emit が一括管理)
;------------------------------------------------------------------------------
getblock2_entry:
                JSR     rect_from_param         ; $D383-$D38A → WK_RECT_X0/Y0/X1/Y1
                ; 応答初期化: $D383 = 計数 0、データは $D384 から
                JSR     cb_resp0        ; $D383 = 計数 0 / 応答は $D384 から
                ; プレーンマスク $01 (B) → $02 (R) → $04 (G) の 3 周
                LDA     #$01
                STA     <WK_GB_TGTCOL            ; 走査モード = プレーンマスク
                BSR     gb_scan
                LDA     #$02
                STA     <WK_GB_TGTCOL
                BSR     gb_scan
                LDA     #$04
                STA     <WK_GB_TGTCOL

;------------------------------------------------------------------------------
; gb_scan — 矩形走査エンジン ($1B/$1D 共用)
;   WK_RECT_X0/Y0-X1/Y1 を走査し、ビット判定結果を 8 bit/byte に集約して
;   cbg_emit (有界エミッタ) へ流す。WK_GB_TGTCOL=0 なら色リスト一致 ($1B)、
;   非 0 ならその値をプレーンマスクとして抽出 ($1D)。
;   ビット詰めは参考書籍の図どおり行末でバイト境界へ
;   切上げない連続詰め (1 行 = 幅ぶんのビットがそのまま連なる)。
;   最終バイトの端数ビットは 0 で埋める。
;   フラッシュは $1B は全行の最後に 1 回、$1D は各プレーンの最後に 1 回
;   (= gb_scan 呼出単位)。
;   出力は共有 RAM の応答領域 ($D384-$D3FF) に厳密に閉じる (cbg_emit が
;   124 byte 毎に応答側継続ハンドシェイクを行う)。
;
;   ビット集約は「番兵ビット」方式: WK_GB_BITACC を $01 で初期化し、
;   画素ビットを ROL で下から入れる。番兵が bit7 から押し出された時点
;   (= C=1) が 1 byte 確定で、A にはちょうど 8 画素ぶんが揃っている。
;   残ビット数の別変数を持たずに済み、端数の有無は BITACC が $01 か
;   どうかで判る。
;------------------------------------------------------------------------------
gb_scan:
                LDA     #$01                    ; 番兵ビットのみ = 端数なし
                STA     <WK_GB_BITACC
                LDX     <WK_RECT_Y0
                STX     <WK_PX_Y
gb_yloop:
                LDX     <WK_PX_Y
                CMPX    <WK_RECT_Y1
                BHI     gb_scan_done
                LDX     <WK_RECT_X0              ; 以降 X = 走査中の X 座標
gb_xloop:
                STX     <WK_PX_X
                CMPX    <WK_RECT_X1
                BHI     gb_endrow
                BSR     gb_putpixbit            ; 1 画素 = 1 bit 詰め (X は保存される)
                LEAX    1,X
                BRA     gb_xloop
gb_scan_done:
                ; 走査終了: 端数があれば 0 を詰めて 1 byte にする
                LDA     <WK_GB_BITACC
                CMPA    #$01
                BEQ     gb_flush_done           ; 番兵だけ = 端数なし
gb_pad1:
                ASLA                            ; 0 を詰める (番兵が出るまで)
                BCC     gb_pad1
                BSR     cbg_emit                ; 有界出力 (満杯で分割応答)
gb_flush_done:
                RTS
gb_endrow:
                ; 行末はバイト境界へ切上げず次行へ連続 (動作観察のビット連続詰め)
                LDX     <WK_PX_Y
                LEAX    1,X
                STX     <WK_PX_Y
                BRA     gb_yloop

;--- gb1_match — A=ピクセル色が指定色リストのどれかに一致なら C=1 -------------
gb1_match:
                PSHS    B,X
                LDX     #WK_GB_COLS
                LDB     <WK_GB_NCOL
gb1m_loop:
                CMPA    ,X+
                BEQ     gb1m_hit
                DECB
                BNE     gb1m_loop
                ANDCC   #$FE                    ; C=0 (不一致)
                PULS    B,X,PC
gb1m_hit:
                ORCC    #$01                    ; C=1 (一致)
                PULS    B,X,PC
        ENDC

;--- cbg_emit — A を GET 応答バッファへ追加 (満杯で分割応答) ------------------
cbg_emit:
                PSHS    A,X
                LDX     <CB_RPTR
                CMPX    #$D400                  ; 共有 RAM 終端
                BCS     cbg_st
                BSR     cbg_wait64              ; チャンク公開 → $64 待ち
                LDX     #$D384
cbg_st:
                LDA     ,S
                STA     ,X+
                STX     <CB_RPTR
                INC     >SH_PARAM               ; $D383 (計数)++
                PULS    A,X,PC

        IFNDEF SUBSYS_TB
;--- gb_putpixbit — WK_PX_X/Y の 1 画素を判定して集約バッファへ 1 bit 詰める ---
;   1 byte 揃ったら cbg_emit へ流して番兵を張り直す。
;   戻り: C=1 なら「今の 1 bit で 1 byte 確定」。壊す: A (X/Y/U は被呼出側で保存)
gb_putpixbit:
                JSR     get_pixel_color
                TST     <WK_GB_TGTCOL
                BEQ     gbp_clist
                ANDA    <WK_GB_TGTCOL            ; プレーンビットだけ抽出
                ADDA    #$FF                    ; C=1 (点: ビットが立っている) / C=0 (非点)
gbp_acc:
                LDA     <WK_GB_BITACC
                ROLA                            ; C を下から入れ、番兵を上へ押す
                STA     <WK_GB_BITACC
                BCC     gbp_ret                 ; 番兵はまだ中 = 端数のまま
                BSR     cbg_emit                ; 8 画素揃った A を出力
                LDA     #$01
                STA     <WK_GB_BITACC            ; 番兵を張り直す
                ORCC    #$01                    ; C=1 (1 byte 確定) を呼出元へ
gbp_ret:
                RTS
gbp_clist:
                BSR     gb1_match               ; C=1 なら指定色のどれかに一致
                BRA     gbp_acc                 ; 判定結果はそのまま C に載っている
        ENDC

;==============================================================================
; $20 Character Line — 文字でラインを引く (LINE@ 文字形式、動作観察)
;   $D383=属性, $D384=文字コード,
;   $D386/$D388/$D38A/$D38C = X0,Y0,X1,Y1 (文字座標、各 16bit 枠の下位 byte),
;   $D38D = ボックスフラグ (0=直線 / 1=矩形の 4 辺枠 / 2=矩形塗潰し)。
;   両端点を検証し、フラグ分岐 (chl_disp、ROM 空き領域) から直線サブルーチン
;   cl_line を呼び分ける。cl_line は DDA (主軸駆動) で各セルへ文字を打つ。
;   属性は既存セルの bit7 (論理行頭マーカ) を保存して合成する (動作観察)。
;==============================================================================
chline_entry:
                LDA     3,U                     ; $D386 = X0
                LDB     5,U                     ; $D388 = Y0
                JSR     cb_valid
                BCC    cb_coorderr             ; 座標範囲外 (参考書籍)
                LDA     7,U                     ; $D38A = X1
                LDB     9,U                     ; $D38C = Y1
                JSR     cb_valid
                BCC     cb_coorderr
                LDA     0,U                     ; $D383 = 属性
                STA     <CL_ATR
                LDA     1,U                     ; $D384 = 文字
                STA     <CL_CHR
                JMP     chl_disp                ; ボックスフラグ ($D38D) で分岐

;--- cbget_entry — $06/$08 本体 (コンソールバッファ → 応答) -------------------
;--- cb_resp0 — 応答の初期化 ($D383 = 計数 0、書込み位置 = $D384) ---------------
;   $06/$08/$1B/$1D の入口が共用する。
cb_resp0:
                CLR     0,U
                LEAX    1,U
                STX     <CB_RPTR
                RTS

cbget_entry:
                JSR     cb_parse
                BCC     cbget_bad
                BSR     cb_resp0        ; $D383 = 計数 0 / 応答は $D384 から
cbget_loop:
                BSR     cb_next_cell
                BCC     cbget_fin
                TST     <CB_FLAG
                BEQ     cbget_chr
                LDA     CONBUF_ATTR-CONBUF_CHAR,X ; 形式2: 先に属性
                BSR     cbg_emit
cbget_chr:
                LDA     ,X                      ; 文字
                BSR     cbg_emit
                BRA     cbget_loop
cbget_bad:
                CLR     0,U                     ; 範囲外 → 計数 0 の空応答
cb_coorderr:
                LDA     #$3D                    ; コンソール座標値の誤り (参考書籍)
                JMP     cmd_err

;--- cbg_wait64 — 部分応答を公開し継続コマンドを待つ (参考書籍) ---
;   $D381 bit7 =「継続チャンクあり」をメインへ提示し BUSY を落として待つ。
cbg_wait64:
                BSR     sh_cont_set
                ; ここで $D382 を清算してはならない。メイン側の完了判定は
                ;   「$D382 が 0 になったら応答領域を読み戻す」であり、受理直後に
                ;   0 へ戻すと、次チャンクをまだ 1 byte も書いていない走査中に
                ;   完了と誤判定され、未完成の応答領域が読み取られる。
                CLR     >SH_PARAM               ; 新チャンク計数 = 0
cbget_fin:
cbput_fin:
                RTS

;--- cb_next_cell — 現セルの文字バッファアドレスを X に返し走査を進める -------
;   現セル座標を CB_DCOL/CB_DROW へ複写 (描画用)。全セル終了で C=0。
;   走査順は動作観察どおり 行内 左→右、行 上→下。
cb_next_cell:
                LDA     <CB_ROW
                CMPA    <CB_RMAX
                BHI     cbn_done                ; 最終行を越えた → 終了
                LDB     <CB_COL
                STB     <CB_DCOL
                STA     <CB_DROW
                LDB     <WK_SCREEN_W
                MUL                             ; D = 行 * 画面幅
                ADDB    <CB_COL
                JSR     cb_mkx          ; X = 文字バッファアドレス
                LDA     <CB_COL                 ; 次セルへ前進
                INCA
                CMPA    <CB_CMAX
                BLS     cbn_col
                LDA     <CB_CMIN                ; 行送り
                INC     <CB_ROW
cbn_col:
                STA     <CB_COL
                ORCC    #$01
                RTS
cbn_done:
                ANDCC   #$FE
                RTS

;------------------------------------------------------------------------------
; cmd_06-$09 — キャラクタブロック GET@/PUT@ (動作観察)
;   $06/$07 = 形式1 (文字のみ)、$08/$09 = 形式2 (属性+文字)。
;   $D383=X1, $D384=Y1, $D385=X2, $D386=Y2 (文字座標の矩形)。
;   本体は ROM 末尾空き領域 (cbget_entry / cbput_entry)。
;   応答の形式は GET@/PUT@ の呼出 (動作観察) に従う。矩形描画の本体
;   (do_rect) は line15_entry から使う別ルーチンである。
;------------------------------------------------------------------------------
cmd_06_cget1:
                CLRA                            ; 形式1 = 文字のみ
                FCB     $8C                     ; CMPX #imm の opcode。続く 2 byte
                                                ;   (LDA #$FF) を即値として食い、
                                                ;   A=0 のまま cbget_stub へ落ちる
cmd_08_cget2:
                LDA     #$FF                    ; 形式2 = 属性+文字
cbget_stub:
                STA     <CB_FLAG
                BRA     cbget_entry
cbput_f1:
                LDA     <CB_FATR                ; 形式1: 固定属性
cbput_at:
                TFR     A,B                     ; B = 属性
                BSR     pbs_next                ; A = 文字
                BSR     cb_drawcell             ; セル描画 + conbuf 記録
                BRA     cbput_loop
cmd_07_cput1:
                CLRA
                FCB     $8C                     ; 同上 (LDA #$FF を即値として食う)
cmd_09_cput2:
                LDA     #$FF
cbput_stub:
                STA     <CB_FLAG

;--- cbput_entry — $07/$09 本体 (データ → conbuf + VRAM 描画) -----------------
cbput_entry:
                JSR     cb_parse
                BCC    cb_coorderr             ; 座標範囲外 (参考書籍)
                LDA     4,U                     ; $D387 = 形式1 固定属性
                STA     <CB_FATR
                LDA     5,U                     ; $D388 = データ計数
                STA     <PBS_CNT
                LEAX    6,U             ; データ先頭
                STX     <PBS_PTR
cbput_loop:
                BSR     cb_next_cell
                BCC     cbput_fin
                TST     <CB_FLAG
                BEQ     cbput_f1
                BSR     pbs_next                ; 形式2: ストリームから属性
                BRA     cbput_at

;--- sh_cont_set / sh_cont_ack — 継続コマンドの提示と受理 (参考書籍) ---
;   sh_cont_set: $D381 bit7 =「継続チャンクあり」をメインへ提示してから受理を待つ。
;   sh_cont_ack: 提示を伴わない受理だけの入口 (次チャンクの要求)。
;   いずれも受理の後に相対 0 を 0 へ戻す。$D381 の bit7 は保持する
;   (継続コマンドの S′ をそのまま読む)。A 破壊。
sh_cont_set:
                LDA     >SH_CNT
                ORA     #$80
                STA     >SH_CNT                 ; $D381 |= $80 (継続あり)
sh_cont_ack:
                BSR     sh_rereq_c              ; 受理済コマンドを清算して (BUSY 中で
                                                ;   安全) BUSY を落とし次を待つ
                CLR     >SH_ERR
                RTS

;==============================================================================
; pbs_init / pbs_next — PutBlock パターンストリーム読出 + 継続プロトコル ($64)
;   $D38D = 本パケットで供給されたパターン byte 数 (動作観察)。ブロック全体に
;   満たないとき、相対 1 の S′ が 1 なら、サブは BUSY を落として次チャンクを
;   要求し、メインは継続コマンド $64 ($D381 bit7=S′, $D383=計数, $D384〜=データ)
;   を書いて分割転送を継続する。S′ が 0 なら継続は来ないので、計数を使い切った
;   先 (と共有 RAM の終端の先) のパターンは 0 のビットとする。
;   $64 以外のコマンドが来たらブロックを放棄し、その新コマンドを食わずに
;   そのまま再ディスパッチする。
;   共有 RAM 終端 ($D400) より先は読まない (I/O レジスタ不可侵の防御)。
;==============================================================================
pbs_init:
                LEAX    11,U            ; パターン先頭
                STX     <PBS_PTR
                LDA     10,U                    ; データ byte 数 (拡張アドレス指定:
                                                ;   外から直接呼ばれた場合も DP=$D0 で
                                                ;   来るため DP 非依存にする)
                STA     <PBS_CNT
                RTS
pbs_guard:                                      ; 共有 RAM の終端に達した
pbs_end:                                        ; チャンクの計数を使い切った
                TST     >SH_CNT                 ; S′=1 なら次のチャンクを要求。S′=0 なら
                BMI     pbs_refill              ;   継続は来ないので、この先のパターンは
                CLRA                            ;   0 のビットとする
                PULS    DP,X,PC
;--- 継続要求: ATN 清算 → BUSY 落とし (= 次チャンク要求) → 継続コマンド待ち ---
;   (参考書籍。相対 3 = 今回のバイト数、相対 4〜 = データ)
pbs_refill:
                BSR     sh_cont_ack             ; 清算して BUSY を落とし次を待つ
                CLR     >SH_CMD                 ; $D382 = 0 (消費済)
                LDX     #SH_PARAM               ; 新チャンク: $D383 = 計数、
                LDA     ,X+                     ;   $D384〜 = データ (固定のアドレスから読む)
                STA     <PBS_CNT
                STX     <PBS_PTR
                BRA     pbs_loop
pbs_next:                                       ; A = 次パターンバイト
                ; 本ルーチンは作業 RAM を DP=$D0 で直接参照するため、呼出元の DP を
                ; 問わずここで確立し、戻るときに呼出元の DP を戻す。
                PSHS    DP,X
                LDA     #$D0
                TFR     A,DP
pbs_loop:
                LDA     <PBS_CNT
                BEQ     pbs_end                 ; チャンク枯渇
                DEC     <PBS_CNT
                LDX     <PBS_PTR
                CMPX    #$D400
                BCC     pbs_guard               ; 終端越え
                LDA     ,X+
                STX     <PBS_PTR
                PULS    DP,X,PC                 ; 呼出元 DP を復元して復帰

;--- cb_drawcell — A=文字, B=属性 を CB_DCOL/CB_DROW のセルへ描画 -------------
;   カーソル桁/行を退避してセルへ移動し、属性をセルへ置いてから
;   draw_char_at_cursor (conbuf 記録 + 3 プレーン描画 + 40桁/20行対応) で描き、
;   復元する。SF の一連の文字列の外として描く (セルの属性で描く)。A/B/X 保存。
cb_drawcell:
                PSHS    A,B,X
                LDX     <WK_CURSOR_X             ; (桁,行) 16bit 一括退避
                STX     <CB_SAVE
                LDX     <WK_SF_ADR
                STX     <CB_SAVE+2
                CLR     <WK_SF_ADR               ; 一連の外として描く
                CLR     <WK_SF_ADR+1
                LDD     <CB_DCOL                ; A=桁, B=行
                STD     <WK_CURSOR_X
                JSR     con_cell                ; X = セル
                LDA     1,S                     ; 属性
                STA     CONBUF_ATTR-CONBUF_CHAR,X
                LDA     ,S                      ; 文字
                JSR     draw_char_at_cursor
                LDX     <CB_SAVE+2
                STX     <WK_SF_ADR
                LDX     <CB_SAVE
                STX     <WK_CURSOR_X
                PULS    A,B,X,PC

;--- sh_rereq — 部分応答を提示して次のコマンドを待つ (分割転送の共通部) --------
;   共有 RAM 相対 0 の ATN ビットを清算し、BUSY を落として (= メインへ提示)、
;   $D382 に次のコマンドが書かれるまで待つ。待っている間に ATN が立ったら
;   提示からやり直す。復帰時 A = 受け取ったコマンドコードで、BUSY は再セット
;   済み。送信側 (パターン分割受取) と受信側 (応答分割提示) が共用する。
;   sh_rereq_c は受理済コマンド (相対 2) を消費してから待つ清算つき入口
;   (分割転送・出力継続・分割応答の待ちが使う。ATN のやり直しで再清算しない
;   よう、ループの戻り先 sh_rereq とは分けてある)。
;   参考書籍は継続コマンド ($64) を送り続けることを求めるので、
;   受け取ったコマンドコードは検査しない。
;   BUSY を落とす前に相対 2 を見る。参考書籍の時系列では、メインは
;   BUSY が落ちたのを見て HALT し、コマンドを置いて HALT を解除する。サブはその間 BUSY の
;   ままコマンドを実行する (参考書籍)。HALT を解いた時点でコマンドが置かれていれば、
;   BUSY を落とさずにそのまま受け取る (落とすとメインが実行済みと読み、次のコマンドで
;   上書きする)。
sh_rereq_c:
                CLR     >SH_CMD                 ; 受理済コマンドを消費してから待つ
sh_rereq:
                LDA     >SH_ERR                 ; ATN bit (bit7) 清算: 左へ押し出してから
                ASLA                            ;   右へ戻す (bit6-0 は不変、bit7 = 0)。
                LSRA                            ;   共有 RAM への書込みは 1 回 (途中の値を見せない)
                STA     >SH_ERR
                LDA     >SH_CMD                 ; コマンドが置かれていれば BUSY のまま受け取る
                BNE     sh_rqtake
                LDA     IO_BUSY                 ; BUSY 落とし = 提示 / 要求 (読出しで落ちる。A は直後に上書き)
                BRA     sh_rqwait
sh_rqatn:
                TST     >SH_ERR                 ; コマンド未着: ATN が立ったらやり直し
                BMI     sh_rereq
sh_rqwait:
                LDA     >SH_CMD                 ; 次コマンド待ち (コマンドを先に見る)
                BEQ     sh_rqatn
sh_rqtake:
                TST     >SH_ERR                 ; 相対 0 の MSB = READY REQ (参考書籍
                BMI     sh_rqatn                ;   )。立っている間は「HALT にしたが
                                                ;   新しいコマンドは置かない」印なので受理しない
                STA     IO_BUSY                 ; BUSY 再セット = 受理
                RTS

;==============================================================================
; $01 INIT / $0D ERASE 2 (参考書籍。ROM 末尾空き領域)
;==============================================================================

;--- c01_ext — $01 INIT 本体 ----------------------------------------------------
;   パラメータ: 相対 3=BC 背景色(0-7) / 4=NC 桁数(80|40) / 5=NL 行数(25|20)
;              6=SL スクロール開始行 / 7=NS スクロール行数 / 8=FD ファンクションキー表示
;              9=ERS 初期設定後の画面の消去 / 10=GR 単色表示
;   検証 (不合格なら $3C を返し、RESET 時のパラメータで初期化する):
;     桁数∈{80,40} / 行数∈{25,20} / 背景色≤7 / 範囲行数 bit7=0 /
;     範囲上端 < 使用可能行数 / 範囲上端+範囲行数 ≤ 使用可能行数
;     (使用可能行数 = 行数 - (ファンクションキー表示 ? 2 : 0))
;   GR (相対 10) を保持し、非 0 なら単色表示にする。
c01_ext:
        IFDEF SUBSYS_TB
;   Type-B の位置に置く ROM はどのパラメータも受け付けず、相対 0 に $3C、相対 3〜11 に RESET
;   時のパラメータ (BC 2 バイト = 0 / 桁数 40 / 行数 25 / 範囲 0,25 / 機能キー行 0 / 消去 1 /
;   単色 0) を置いて、背景色 0・文字の属性 7 で画面を消し、カーソルをホームへ戻す (動作観察)。
                LDA     #$3C
                STA     >SH_ERR
                CLR     ,U
                CLR     1,U
                LDD     #40*256+25
                STD     2,U
                LDD     #25
                STD     4,U
                LDD     #1
                STD     6,U
                CLR     8,U
                LDA     #$07
                STA     <WK_TXT_COL
                CLR     <WK_BGCOL
                CLR     <TB_BG
                CLR     <TB_BG+1
                JSR     pc_home
                CLRA
                LDB     <WK_SCREEN_H
                BRA     erase_rows
        ELSE
                LDA     1,U             ; $D384 = 桁数
                CMPA    #80
                BEQ     c01x_w_ok
                CMPA    #40
                BNE     c01x_rej
c01x_w_ok:
                LDB     2,U             ; $D385 = 行数
                CMPB    #25
                BEQ     c01x_h_ok
                CMPB    #20
                BNE     c01x_rej
c01x_h_ok:
                LDA     ,U              ; $D383 = 背景色 (0-7 のみ)
                CMPA    #7
                BHI     c01x_rej
                LDA     4,U             ; $D387 = 範囲行数 (bit7=不正)
                BMI     c01x_rej
                TST     5,U             ; $D388 = ファンクション行表示
                BEQ     c01x_nofn
                SUBB    #2                      ; 使用可能行数 = 行数 - 2
c01x_nofn:
                PSHS    B                       ; ,S = 使用可能行数
                LDA     3,U             ; $D386 = 範囲上端
                CMPA    ,S
                BCC     c01x_rejp               ; 上端 >= 使用可能 → 不合格
                ADDA    4,U             ; + 範囲行数
                CMPA    ,S+
                BHI     c01x_rej                ; 上端+行数 > 使用可能 → 不合格
                ; ---- 合格: 画面モード設定 ----
                LDA     ,U              ; $D383 = 背景色を確定
                STA     <WK_BGCOL               ;   (PRESET 描画の参照元)
                LDA     1,U             ; 桁数
                STA     <WK_SCREEN_W
                LDB     #$FF
                CMPA    #40                     ; 40桁 = 倍幅 (16px)
                BEQ     c01x_dbl
                CLRB                            ; 80桁 = 8px
c01x_dbl:
                STB     <WK_CHAR_DBL
                LDA     2,U             ; 行数
                STA     <WK_SCREEN_H
                LDB     #$FF
                CMPA    #20                     ; 20行 = 10px 行ピッチ
                BEQ     c01x_r20
                CLRB                            ; 25行 = 8px
c01x_r20:
                STB     <WK_ROW20
                LDX     #640                    ; 1 行のバイト数 (8px 行)
                TSTB
                BEQ     c01x_lb
                LDX     #800                    ; 10px 行
c01x_lb:
                STX     <WK_LINE_BYTES
                ; ---- スクロール範囲 ($D386=上端/$D387=行数) を格納・適用 ----
                ;   範囲は検証するだけでなく必ず適用する。適用しないと範囲指定時も
                ;     文出力が全画面へ流れ、スクロールが画面全体に及んでしまう。
                ; ---- 相対 10 (GR) を控える (draw_char_at_cursor が参照) ----
                LDA     7,U             ; $D38A = 単色表示 (参考書籍の GR)
                STA     <WK_MONO
                ; ---- 相対 8 (FD) を控える (win_setup が参照) ----
                LDA     5,U             ; $D388 = 機能キー行表示 (参考書籍の FD)
                STA     <WK_PF_SHOW
                BSR     win_setup
                ; ---- 消去指定 ERS (相対 9) ----
                LDA     6,U             ; 0 = 再初期化のみ (VRAM/カーソル不変)
                BEQ     c01x_done
                JSR     pc_home                 ; カーソルホーム (範囲左上へ)
                LDA     #$07                    ; 既定の文字属性
                STA     <WK_TXT_COL
                CLRA                            ; 全画面 [0, 行数) を背景色で消す
                LDB     <WK_SCREEN_H
                JMP     erase_rows              ; 消去の出口で機能キー行も描く (pf_show)
c01x_rejp:
                LEAS    1,S
c01x_rej:
                ; 参考書籍: 誤りがあったときは RESET 時の
                ;   パラメータで初期化する。誤りの番号は応答データの相対 0 へ置く。
                LDA     #$3C                    ; INIT のパラメタの誤り (参考書籍)
                STA     >SH_ERR
                LDX     #c01x_rstp
                LDY     #SH_PARAM               ; 複写は Y で行う。U は共有 RAM の
c01x_cp:                                        ;   パラメータ欄 ($D383) を指したまま
                LDA     ,X+                     ;   でないと、入り直した先の n,U の
                STA     ,Y+                     ;   参照が 8 バイトずれる
                CMPX    #c01x_rstp+8
                BNE     c01x_cp
                JMP     c01_ext                 ; RESET 時のパラメータで初期化し直す
c01x_done:
                JMP     pf_show                 ; 消去しない場合も機能キー行の表示指定を反映

;--- win_setup — $01 の範囲パラメータ ($D386=上端/$D387=行数) を実効行へ変換 ----
;   範囲パラメータの行単位は「そのとき設定した行数 ($D385) の行」である
;   (上端 × 行ピッチ が区画の上端になる。動作観察)。
;   よって行ピッチによる読み替えは行わず、指定された行番号をそのまま採る。
;   結果が不正 (top>=end) の場合は画面全体 [0,行数) へ退行。
;   入力: DP=$D0 (作業 RAM 直接参照)、WK_ROW20/WK_SCREEN_H 設定済であること。
;   A,B 破壊。c01_ext から JSR。
win_setup:
                LDA     <WK_SCREEN_H             ; 使用可能行数 L = 行数 (機能キー行表示なら -2)
                TST     <WK_PF_SHOW
                BEQ     ws_lim
                SUBA    #2
ws_lim:
                PSHS    A                       ; ,S = L
                LDA     3,U             ; 範囲上端
                LDB     4,U             ; 範囲行数
                ADDB    3,U             ; B = top+rows (下端+1)。A = 上端のまま
                CMPB    ,S                      ; 下端は L へクランプ
                BLS     ws_c2
                LDB     ,S
ws_c2:
                PSHS    B                       ; 0,S = 下端 / 1,S = L
                CMPA    ,S
                BCS     ws_ok                   ; 上端 < 下端 → 採用
                CLRA                            ; 不正 → 使用可能な全行へ退行
                LDB     1,S
                STB     ,S
ws_ok:
                STA     <WK_WIN_TOP
                PULS    B
                STB     <WK_WIN_END
                LEAS    1,S
                RTS

        ENDC
;--- c0d_ext — $0D ERASE 2 本体 (参考書籍) --------------------
;   相対 3 = W 範囲 / 4 = BC 背景色 (W = 0 のときのみ有効) / 5 = FC 文字色 (0〜15)。
c0d_ext:
        IFDEF SUBSYS_TB
                LDA     4,U                     ; Type-B の位置: 相対 7 = FC (相対 4〜6 = BC の B,R,G)
        ELSE
                LDA     2,U                     ; FC ($D385) = 消去後の文字色 (0〜15)
        ENDC
                ANDA    #$0F                    ;   アトリビュート文字の CL + R
                STA     <WK_TXT_COL
        IFNDEF SUBSYS_TB
        IFNDEF STAGE_ALT_RAM
c02_ext:                                ; $02 も W = 0 のときだけ BC を背景色にする
        ENDC
        ENDC
                LDA     0,U                     ; W
        IFDEF SUBSYS_TB
                CMPA    #2
                LBCC    c_e3c                   ; Type-B の位置: W = 2 以上は $3C
                TSTA
        ENDC
                BNE     erase_cmd               ; BC は W = 0 のときのみ有効
;--- c02_ext — $02 ERASE 本体 (参考書籍) ---------------------
;   相対 3 = W 消去範囲 / 4 = BC 背景色。$0D の W = 0 の経路と同じ処理なのでここから入る。
;   Type-B の位置でない ROM は、W が 0 でないとき BC を使わず、今の背景色で消す (動作観察)。
;   その ROM の入口は上の c0d_ext の中 (STAGE_ALT_RAM はここ)。
        IFDEF SUBSYS_TB
c02_ext:
        ELSE
        IFDEF STAGE_ALT_RAM
c02_ext:
        ENDC
        ENDC
        IFDEF SUBSYS_TB
                LEAX    1,U                     ; Type-B の位置: 相対 4〜6 = BC の B,R,G (各 4 ビット)
                JSR     tb_rgb
                STD     <TB_BG
        ENDC
                LDA     1,U                     ; BC ($D384) = 消去後の背景色
                ANDA    #$07                    ; 色は下位 3bit のみ有効
                STA     <WK_BGCOL               ; 背景色を確定 (PRESET 描画の参照元)

;--- erase_cmd — $02 / $0D の共通処理: W ($D383) の指す範囲を消す ---------------
;   W = 0 全画面 / 1 スクロールモード画面 / 2 ページモード 1 画面 /
;   3 ページモード 2 画面 (参考書籍)。
;   全画面のときはバッファアドレスをホームへ戻す。一覧に無い W は何もしない。
erase_cmd:
                BSR     ec_body                 ; W の範囲を消してから
;--- f_head — 画面の先頭のセルのアトリビュートに F を立てる (動作観察)。A 破壊 ------
;   リセットの初期化、$02 / $0D の消去 (W によらない)、行送りのスクロールの後に呼ぶ。
;   EA オーダの消去 (pc_cls) は呼ばない。文字を描くときは draw_char_at_cursor が立てる。
f_head:
                LDA     >CONBUF_ATTR
                ORA     #$80
                STA     >CONBUF_ATTR
                RTS
ec_body:
                LDB     <WK_SCREEN_H
                LDA     0,U                     ; W (Z = W が 0)
                BEQ     ec_home                 ; W = 0: [0, 行数)
                CMPA    #3
                BEQ     ec_p2                   ; W = 3: [スクロール下端, 行数)
                LDB     <WK_WIN_END
                DECA
                BEQ     ec_sc                   ; W = 1: [スクロール上端, 下端)
                LDB     <WK_WIN_TOP
                DECA
                BEQ     ec_top0                 ; W = 2: [0, スクロール上端)
                CLRB                            ; 一覧に無い W = 空の範囲
                BRA     ec_top0
ec_sc:
                LDA     <WK_WIN_TOP
                BRA     erase_rows
ec_home:
                JSR     pc_home                 ; 全画面はバッファアドレスをホームへ
ec_top0:
                CLRA
                BRA     erase_rows
ec_p2:
                LDA     <WK_WIN_END

;--- erase_rows — 行 [A, B) をコンソールバッファと VRAM から消す ----------------
;   A = 上端行 / B = 下端行+1 (A >= B なら何もしない)。X / Y / U を壊す
;   (putchar 契約の B と X は呼出元 pc_cls が退避する)。
;   コンソールバッファは Null コード ($00) + 現在の文字属性、VRAM は背景色
;   (WK_BGCOL) で埋める。画面の先頭から消すときは表示開始位置も 0 へ戻す
;   (表示開始位置はスクロール範囲が画面全体のときだけ動くので、範囲を分けて
;   いるときは 0 のままであり、書き戻しても表示位置は変わらない)。
erase_rows:
                PSHS    A,B                     ; 0,S = 上端行 / 1,S = 下端行+1
                CMPA    1,S
                BCC     er_ret                  ; 空の範囲
                ; --- コンソールバッファ ---
                LDA     1,S
                JSR     row_addr                ; X = 範囲の終りのセル
                PSHS    X                       ; 0,S = 終りのセル
                LDA     2,S
                JSR     row_addr                ; X = 範囲の先頭セル
                LDB     <WK_TXT_COL
                CLR     <WK_SF_ADR              ; フィールドの構成が消えるので
                CLR     <WK_SF_ADR+1            ;   SF の一連の文字列も終える
er_cb:
                CLR     ,X                      ; 文字 = Null コード
                STB     CONBUF_ATTR-CONBUF_CHAR,X ; アトリビュート
                LEAX    1,X
                CMPX    ,S
                BNE     er_cb
                LEAS    2,S
                ; --- VRAM ---
                LDA     ,S                      ; 上端行
                BNE     er_part
                LDX     #0                      ; 画面の先頭から消すときは
                STX     IO_VRAM_OFS_HI          ;   表示開始位置を 0 へ
                STX     <WK_VRAM_OFS
er_part:
                BSR     su_ofs                  ; D = 先頭オフセット
                PSHS    A,B
                LDA     3,S                     ; 下端行+1
                BSR     su_ofs                  ; D = 終端オフセット
                PULS    X                       ; X = 先頭オフセット
                BSR     vfb_range
er_ret:
                PULS    A,B,PC

;--- vfb_range — 各プレーンの [X, D) を背景色 (WK_BGCOL) のビットで充填 --------
;   プレーン k (B=bit0 / R=bit1 / G=bit2) を、ビットが 1 なら $FF、0 なら $00 で
;   埋める。X = 先頭オフセット / D = 終端オフセット (どちらも偶数で X < D)。
;   3 プレーンを 1 周で書くため、充填値を作業域へ 3 組並べてから D / Y / U へ
;   載せる。画面消去はカーソルの反転も一緒に消すので、反転状態の控えを落とす。
;   全レジスタを壊す。呼出しの深さを浅く保つため、退避はスタックではなく作業域で
;   行う (この経路は文字出力の奥から呼ばれ、利用者の常駐データの手前まで積む)。
vfb_range:
        IFDEF SUBSYS_TB
;   Type-B の位置: 12 の面の [X, D) を、文字の属性 WK_TXT_COL の地の色で埋める。終端が
;   画面の終り (8000) なら面の終り ($2000) まで埋める。
                CMPD    #8000
                BNE     tvf_e
                LDD     #$2000
tvf_e:
                STD     <ER_END
                STX     <ER_SRC
                LDA     <WK_TXT_COL
                JSR     tb_col
                LDX     #g2b_tab
tvf_pl:
                JSR     tb_pg                   ; D = 面の先頭
                PSHS    X
                JSR     tb_rng
                CLRA
                ASL     <TB_BW+1
                ROL     <TB_BW
                BCC     tvf_lp
                COMA
tvf_lp:
                STA     ,X+
                CMPX    <TB_E
                BNE     tvf_lp
                PULS    X
                CMPX    #g2b_tab+12
                BNE     tvf_pl
                STA     >IO_VRAM_GATE
                JSR     tb_prst
                JMP     pf_show
        ELSE
                STD     <ER_END                 ; 終端オフセット
                STX     <ER_SRC                 ; 先頭オフセット
                LDA     IO_VRAM_GATE            ; VRAM gate ON (読出で開く)
                LDB     <WK_BGCOL
                LDX     #ER_V
vfb_mk:
                CLRA
                LSRB                            ; carry = このプレーンのビット (B → R → G)
                BCC     vfb_m0
                COMA                            ; ビット 1 → $FF
vfb_m0:
                STA     ,X+                     ; 充填値 2 byte
                STA     ,X+
                CMPX    #ER_V+6
                BNE     vfb_mk
                LDD     <ER_V                   ; D = B プレーンの充填値
                LDY     <ER_V+2                 ; Y = R プレーン
                LDU     <ER_V+4                 ; U = G プレーン
                LDX     <ER_SRC                 ; X = 先頭オフセット
vfb_loop:
                STD     ,X
                STY     $4000,X
                STU     $8000,X
                LEAX    2,X
                CMPX    <ER_END
                BNE     vfb_loop
                STA     IO_VRAM_GATE            ; gate OFF (書込で閉じる)
                CLR     <WK_CURSOR_ON            ; 反転は消した範囲ごと消えた
                JMP     pf_show                 ; 消した後は機能キー行を描き直す
        ENDC
;--- su_padd — D += 1 行のバイト数 (WK_LINE_BYTES) ------------------------------
su_padd:
                ADDD    <WK_LINE_BYTES
                RTS

        IFNDEF SUBSYS_TB
;--- c01x_rstp — $01 の誤り時に使う RESET 時のパラメータ -----------------------
;   参照は `LDX #c01x_rstp` の即値のみで位置に依存しない。区画の末尾へ置いてある。
c01x_rstp:
                ; RESET 時のパラメータ (参考書籍)
                FCB     0                       ; BC  背景色
                FCB     80                      ; NC  桁数
                FCB     25                      ; NL  行数
                FCB     0                       ; SL  スクロール開始行
                FCB     25                      ; NS  スクロール行数
                FCB     0                       ; FD  機能キー行の表示
                FCB     1                       ; ERS 初期設定後の画面の消去
                FCB     0                       ; GR  単色表示
        ENDC


;--- su_ofs — A=行番号 → D=行先頭 VRAM オフセット (行バイト 640/800) ----------
;   scroll_up / cursor_toggle / draw_char_at_cursor 共用。A,B のみ破壊。
;   8px 行 = A*80*8 = A*640 / 10px 行 = A*100*8 = A*800。
su_ofs:
        IFDEF SUBSYS_TB
                LDB     #40                     ; Type-B の位置: 1 ライン 40 バイト
        ELSE
                LDB     <WK_ROW20                ; $00 (8px 行) / $FF (10px 行)
                ANDB    #20                     ; → 0 / 20
                ADDB    #80                     ; → 1 行のバイト数 80 / 100
        ENDC
                ASLA
                ASLA
                ASLA                            ; A = 行 * 8 (行数は 25/20 に限るので
                                                ;   行 <= 24、8bit に収まる)
                MUL                             ; D = 行 * 8 * 行バイト数
                RTS

;--- pc_lf_ext — putchar 行送りの本体 (putchar_lf から JMP) --------------------
;   スクロールモード画面 (参考書籍) の最終行から出るときは範囲内を 1 行
;   上へスクロールし、ページモード画面 (参考書籍) の最終行から出るときは
;   その画面の先頭へ戻る。ページモード画面の最終行での行送りはページリミット
;   (参考書籍) の判定へ。範囲外の行は 1 行下がるだけ。
;   X/B (呼出元 cmd_03_console のポインタ/文字数カウンタ) を破壊しないこと。
pc_lf_ext:
                LDA     <WK_CURSOR_Y
                INCA
                CMPA    <WK_WIN_TOP
                BEQ     pcl_p1                  ; ページモード 1 画面の最終行から出た → 先頭へ
                CMPA    <WK_WIN_END
                BEQ     pcl_scr                 ; スクロールモード画面の最終行から出た → スクロール
                CMPA    <WK_SCREEN_H
                BCS     pcl_set                 ; 画面内 → そのまま下がる
                LDA     <WK_WIN_END              ; ページモード 2 画面の最終行から出た → 先頭へ
                BRA     pcl_pw
pcl_p1:
                CLRA
pcl_pw:
                STA     <WK_CURSOR_Y
                JMP     con_page_wait           ; ページウェイト指定なら待機
pcl_set:
                STA     <WK_CURSOR_Y
                RTS

;--- vrows_clear — VRAM の行 A から行 B の手前までを 0 に (3 プレーン)。A,B,X,U 破壊 ---
vrows_clear:
                PSHS    B
                BSR     su_ofs                  ; D = 上端行のオフセット
                TFR     D,X
                PULS    A
                BSR     su_ofs                  ; D = 下端のオフセット
                TFR     D,U
                LDA     IO_VRAM_GATE            ; gate ON
                CLRA
                CLRB
vrc_lp:
                STD     ,X++
                STD     $3FFE,X
                STD     $7FFE,X
                PSHS    U
                CMPX    ,S++
                BCS     vrc_lp
                STA     IO_VRAM_GATE            ; gate OFF
                RTS
        IFNDEF SUBSYS_TB
sw_sub:
                LDA     <WK_WIN_TOP
                BSR     su_ofs
                TFR     D,X                     ; X = 移動先 (範囲先頭行)
                BSR     su_padd
                TFR     D,U                     ; U = 移動元 (範囲 2 行目)
                BRA     su_q
        ENDC
pcl_scr:

;------------------------------------------------------------------------------
; scroll_up — スクロール範囲内を 1 テキスト行 上へスクロール (3 プレーン)
;   コンソール出力が範囲最下行を超えたとき putchar_lf から呼ぶ。
;   ・画面全体 (既定: 上端 0 / 下端 = 行数): 表示オフセット
;     ($D40E/$D40F) を 1 行ぶん加算し、最下行のみクリアする (高速)。
;   ・部分範囲 (範囲指定時): オフセットは動かせない (画面全体に及ぶ) ため、
;     範囲 [WK_WIN_TOP..WK_WIN_END) の行のみ 1 行 (8px:640 / 10px:800 byte)
;     上へメモリ移動し、範囲最下行をクリアする (範囲外の図形は動かない。動作観察)。
;   ※ 呼出元 (cmd_03 の文字数カウンタ B など) を壊さぬよう A,B,X,U を保存。
;   ※ 描画系と同じく VRAM ゲートを開閉する。
;------------------------------------------------------------------------------
scroll_up:
        IFDEF SUBSYS_TB
;   Type-B の位置: 範囲の 2 行目から最終行までを 12 の面で 1 行 (320 バイト) 前へ写し、
;   最終行を地の色で埋める (動作観察)。
                PSHS    A,B,X,U
                BSR     conbuf_scroll
                LDA     <WK_WIN_TOP
                BSR     su_ofs
                STD     <ER_SRC                 ; 写し先の先頭
                LDA     <WK_WIN_END
                DECA
                BSR     su_ofs
                STD     <ER_END                 ; 写し先の終端
                LDX     #g2b_tab
tsu_pl:
                JSR     tb_pg
                PSHS    X
                JSR     tb_rng
                LEAU    320,X
                BRA     tsu_q
tsu_mv:
                LDD     ,U++
                STD     ,X++
tsu_q:
                CMPX    <TB_E
                BNE     tsu_mv
                PULS    X
                CMPX    #g2b_tab+12
                BNE     tsu_pl
                STA     >IO_VRAM_GATE
                LDD     <ER_END                 ; 最終行を地の色で埋める
                TFR     D,X
                ADDD    #320
                JSR     vfb_range
                PULS    A,B,X,U,PC
        ELSE
                PSHS    A,B,X,U
                BSR     conbuf_scroll           ; コンソールバッファも 1 行上へ (参考書籍)
                LDA     >IO_VRAM_GATE           ; gate ON (read で開く)
                                                ; D を使う計算より先に開くこと
                                                ;   (A=D 上位の破壊事故防止)
                LDA     <WK_WIN_END
                BSR     su_ofs                  ; D = 範囲下端オフセット (排他)
                PSHS    A,B                     ; ,S = 範囲下端オフセット
                LDA     <WK_WIN_TOP
                BNE     sw_sub                  ; 部分範囲 → メモリ移動
                LDA     <WK_WIN_END
                CMPA    <WK_SCREEN_H
                BNE     sw_sub
                ; --- 画面全体: 表示の起点を 1 行ぶん進める ---
                ;   VRAM を動かさずに上スクロールする手立ては、表示の起点を持つ
                ;   レジスタ ($D40E/$D40F) を書き換えることだけである (ハードウェアの
                ;   制約)。加算は X で行い、書く順は「ハードウェア → 控え」にする。
                LDX     <WK_VRAM_OFS
                LDD     <WK_LINE_BYTES          ; 1 行のバイト数 (640 / 800)
                LEAX    D,X
                STX     IO_VRAM_OFS_HI          ; $D40E(hi)/$D40F(lo) → 上スクロール
                STX     <WK_VRAM_OFS
                LDA     <WK_WIN_END
                DECA
                BSR     su_ofs                  ; D = 最下行オフセット
                TFR     D,X                     ; X = クリア開始
                BRA     su_clr0
su_mv:
                ; 3 プレーンを 2 byte ずつ前進コピー。ポインタの前進は先頭の
                ;   プレーンの読み書きに畳み込み、残る 2 プレーンは前進ぶんを
                ;   差し引いた相対 ($3FFE/$7FFE) で読み書きする (範囲は小さいので
                ;   単純ループで足りる)。
                LDD     ,U++
                STD     ,X++
                LDD     $3FFE,U
                STD     $3FFE,X
                LDD     $7FFE,U
                STD     $7FFE,X
su_q:
                CMPU    ,S                      ; 移動元が範囲下端に達するまで
                BNE     su_mv
su_clr0:
                LDD     #0                      ; --- (範囲) 最下行をクリア (X が先頭) ---
su_clr:
                STD     ,X++
                STD     $3FFE,X
                STD     $7FFE,X
                CMPX    ,S
                BNE     su_clr
                STA     >IO_VRAM_GATE           ; gate OFF (write で閉じる。A=0)
                LEAS    2,S                     ; 範囲下端オフセット破棄
                PULS    A,B,X,U,PC
        ENDC

;--- conbuf_scroll — スクロール範囲のコンソールバッファを 1 行上へ -------------------
;   参考書籍: 第 2 行目から最終行までを 1 行上へ、最下段の行に Null を挿入。
;   文字とアトリビュートの両方を動かし、最下段の行のアトリビュートからは F と M
;   を落とす。全レジスタ保存
conbuf_scroll:
                PSHS    A,B,X,Y,U
                LDA     <WK_WIN_END
                JSR     row_addr
                PSHS    X                       ; 0,S = 範囲の終り (文字)
                LDA     <WK_WIN_TOP
                JSR     row_addr                ; X = 範囲の先頭 (文字)
                TFR     X,Y
                LDX     ,S
                BSR     cs_pass                 ; 文字を 1 行上へ。Y = 最下段の行
                CLRA
                CLRB
cs_nul:
                STD     ,Y++                    ; 最下段の行に Null
                CMPY    ,S
                BCS     cs_nul
                LDA     <WK_WIN_TOP
                JSR     row_addr
                LEAY    CONBUF_ATTR-CONBUF_CHAR,X ; Y = 範囲の先頭 (属性)
                LDX     ,S
                LEAX    CONBUF_ATTR-CONBUF_CHAR,X ; X = 範囲の終り (属性)
                STX     ,S
                BSR     cs_pass                 ; 属性を 1 行上へ。Y = 最下段の行
cs_fm:
                LDA     ,Y
                ANDA    #$3F                    ; F と M を落とす
                STA     ,Y+
                CMPY    ,S
                BCS     cs_fm
                JSR     f_head                  ; 画面の先頭のセルは F (動作観察)
                LEAS    2,S
                PULS    A,B,X,Y,U,PC

;--- pbb_fetch — FIFO へ 1 バイト補給 (PBB_CNT += 8)。A,B,X,Y 破壊 / U 保存 ----
;   挿入 ACC |= (byte:$00) >> CNT のシフトは共有シフタへの計算入口
;   (エントリ = d_rsh8 + (7-CNT)*2)。供給バイトは高速経路をインラインし、
;   枯渇/終端は pbs_next ($D38D 計数・$D400 ガード・$64 分割転送継続) へ委譲。
;   ※ 全呼出元 (pbb_take/prw_mid) で X は死んでいるため保存しない (高速化)。
pbb_fetch:
                LDB     <PBB_CNT
                ASLB                            ; ×2 (1 シフト = LSRA+RORB の 2 byte)
                LDX     #d_rsh8+14
                NEGB
                LEAX    B,X                     ; X = チェーン入口 (CNT シフトぶん)
                LDA     <PBS_CNT                ; --- pbs_next 高速経路のインライン ---
                BEQ     pfh_slow                ;   (計数あり・共有 RAM 終端内のみ)
                LDY     <PBS_PTR
                CMPY    #$D400
                BCC     pfh_slow
                DECA
                STA     <PBS_CNT
                LDA     ,Y+
                EORA    <PBF_INV                ; fn5: パターン反転 (供給点で適用)
                STY     <PBS_PTR
                CLRB                            ; D = byte:$00
                BRA     pfh_ins
pfh_slow:
                JSR     pbs_next                ; 枯渇/終端: $64 継続含む正規経路
                EORA    <PBF_INV                ; fn5: パターン反転 (供給点で適用)
                CLRB
pfh_ins:
                JSR     ,X                      ; (byte:$00) >> CNT
                ORA     <PBB_ACC
                ORB     <PBB_ACC+1
                STD     <PBB_ACC
                LDB     <PBB_CNT
                ADDB    #8
                STB     <PBB_CNT
                RTS
;--- cs_pass — [Y+桁数, X) を [Y, ...) へ (4 byte ずつ。行の長さは 4 の倍数) -----
;   復帰時 Y = 最下段の行の先頭。X 保存
cs_pass:
                PSHS    X
                TFR     Y,D
                ADDB    <WK_SCREEN_W
                ADCA    #0
                TFR     D,U                     ; U = 移動元 (範囲の 2 行目)
csp_lp:
                CMPU    ,S
                BCC     csp_done
                PULU    A,B,X
                STD     ,Y
                STX     2,Y
                LEAY    4,Y
                BRA     csp_lp
csp_done:
                PULS    X,PC

;------------------------------------------------------------------------------
; cmd_0d_nerase — ERASE2 / NERASE ($0D): 画面消去 + 文字色変更
;   入口の受け渡し: $D383=W(消去範囲, 0=全画面), $D384=BC(背景色), $D385=FC(文字色)。
;   BASIC の CLS はこのコマンド ($D383=0 で全画面消去) で行う。
;   WK_ATTR_MODE はどの経路からも参照されないので設定しない。
;------------------------------------------------------------------------------
                ; 本体は ROM 末尾 c0d_ext。W ($D383) を必ず見てから消去する。
                ;   W≠0 (画面切替/部分消去系) では全画面を消さない — 消すと
                ;   ロード済のグラフィックを壊してしまう。

;--- pfm_wch — prw_fast 行末書戻し用 8bit 左シフトチェーン (空き領域) ----------
;   入口 = pfm_wch + k (ABX)。A を (8-k) 回左シフト → FIFO 残量 (MSB 詰め、
;   k=0 は 8 シフトで空) を書き戻して行末バイト処理 (prw_last) へ。
pfm_wch:
                ASLA
                ASLA
                ASLA
                ASLA
                ASLA
                ASLA
                ASLA
                ASLA
                STA     <PBB_ACC
                CLR     <PBB_ACC+1              ; 有効 bit 以下は 0 (汎用不変条件)
                BRA     prw_last

;--- pbb_take — ビット FIFO から B (1..8) ビット取り出し (行端用の汎用形) ------
;   出力: A = 取り出しビット (MSB 詰め。下位の余りは後続ビットで、呼出元が
;   端マスクで落とす)。B,X,Y 破壊 / U 保存。
pbb_take:
                PSHS    B                       ; ,S = 要求ビット数 n
ptk_chk:
                LDB     <PBB_CNT
                CMPB    ,S
                BCC     ptk_rdy                 ; FIFO に n ビットある
                BSR     pbb_fetch
                BRA     ptk_chk
ptk_rdy:
                SUBB    ,S                      ; CNT -= n
                STB     <PBB_CNT
                PULS    B                       ; B = n
                LDA     <PBB_ACC                ; 戻り値 = ACC 上位バイト (シフト前)
                PSHS    A
                CMPB    #8
                BNE     ptk_slow
                LDA     <PBB_ACC+1              ; n=8: ACC <<= 8 はバイト移動 (シフト不要)
                CLRB
                STD     <PBB_ACC
                PULS    A,PC
ptk_slow:
                ASLB                            ; n<8: ACC <<= n (展開チェーン、
                NEGB                            ;   エントリ = ptk_ch + (7-n)*2)
                LDX     #ptk_ch+14
                LEAX    B,X
                LDD     <PBB_ACC
                JMP     ,X                      ; → ptk_ch (STD/PULS まで継続)

;--- pbb_row — 1 行分のブリット (ビット FIFO → 端マスク付きバイト書込) ---------
;   3 フェーズ構成: 先頭バイト (LM) / 中間バイト (マスクなし、インライン take8) /
;   末尾バイト (RM、取得 QN ビット)。行頭で X0&7 ぶんのダミービットを FIFO 先頭へ
;   挿入し、ビット連続詰めのデータをバイト境界出力へ位相合せする (ダミーは端マスク
;   で必ず落ちる)。
;   入力: PBF_ROWVR=行左端 (B 面基準)、PBF_PHASE/NB/LMASK/RMASK/PBB_QN、
;         PBF_PMASK (0=$1C / 非0=$1E パス)、PBF_PLOFS ($1E)、PBF_DB/DR/DG ($1C)。
;   A,B,X,U 破壊。
pbb_row:
                LDB     <PBF_PHASE
                BEQ     prw_np
                TFR     B,A
                ADDA    <PBB_CNT
                STA     <PBB_CNT                ; CNT += phase
                ASLB                            ; ACC >>= phase は共有シフタ
                NEGB                            ;   (d_rsh8) への計算入口で (ループ廃止)
                LDX     #d_rsh8+14
                LEAX    B,X
                LDD     <PBB_ACC
                JSR     ,X                      ; ACC >>= phase
                STD     <PBB_ACC
prw_np:
                LDU     <PBF_ROWVR
        IFNDEF SUBSYS_TB
                TST     <PBF_PMASK              ; $1E: U は行を通してプレーン実アドレス
                BEQ     prw_u0                  ;   (putb/put1e の毎回加算を行頭に集約)
                TFR     U,D
                ADDD    <PBF_PLOFS
                TFR     D,U
prw_u0:
        ENDC
                LDB     <PBF_NB
                CMPB    #1
                BNE     prw_f
                LDA     <PBF_LMASK              ; --- NB==1: 左右合成マスク・取得 QN ---
                ANDA    <PBF_RMASK
                STA     <PBB_EM
                LDB     <PBB_QN
                BSR     pbb_take
                BRA     pbb_putb                ; (RTS 継承で行完結)
prw_f:
                LDA     <PBF_LMASK              ; --- 先頭バイト: 取得 8・マスク LM ---
                STA     <PBB_EM
                LDB     #8
                BSR     pbb_take
                BSR     pbb_putb
                LEAU    1,U
                LDA     <PBF_NB                 ; --- 中間バイト (NB-2 個) ---
                SUBA    #2
                BEQ     prw_last
                STA     <PBB_BCNT
                BRA     prw_fastmid             ; 行内インライン高速ループへ
        IFNDEF SUBSYS_TB
prw_m1e:
                BSR     pbb_put1e
        ENDC
prw_mnx:
                LEAU    1,U
                DEC     <PBB_BCNT
                BNE     prw_mid
prw_last:
                LDA     <PBF_RMASK              ; --- 末尾バイト: 取得 QN・マスク RM ---
                STA     <PBB_EM
                LDB     <PBB_QN
                JSR     pbb_take

;--- pbb_putb — 端バイト書込 (A=パターン, PBB_EM=端マスク)。U 保存 -------------
pbb_putb:
                ANDA    <PBB_EM                 ; 共通: パターン∧端マスク
        IFNDEF SUBSYS_TB
                TST     <PBF_PMASK
                BNE     ppb_1e
        ENDC
                STA     <PBF_MASK               ; $1C: 書込マスク = 端∧パターン
                JMP     pbf_store3              ; 3 面 RMW (RTS 継承)
        IFNDEF SUBSYS_TB
ppb_1e:
                LDB     <PBB_EM                 ; U = プレーン実アドレス (行頭で加算済)
                STB     <PBF_MASK               ; 合成マスク = 端マスク
                BRA     pb1e_op                 ; 演算指定を適用 (RTS 継承)
                                                ;   (適用不可は prw_mid へ戻る)
        ENDC
prw_mid:
                LDB     <PBB_CNT                ; インライン take(8)
                CMPB    #8
                BCC     prw_m8
                JSR     pbb_fetch
                LDB     <PBB_CNT
prw_m8:
                SUBB    #8
                STB     <PBB_CNT
                LDD     <PBB_ACC                ; A = 取得バイト
                STB     <PBB_ACC                ; ACC <<= 8 (バイト移動)
                CLR     <PBB_ACC+1
        IFNDEF SUBSYS_TB
                TST     <PBF_PMASK
                BNE     prw_m1e
        ENDC
                STA     <PBF_MASK               ; $1C: 書込マスク = パターン
                JSR     pbf_store3
                BRA     prw_mnx

;--- pbb_put1e — $1E 中間バイト書込 (A=パターン, マスク=$FF)。U 保存 -----------
        IFNDEF SUBSYS_TB
pbb_put1e:
                LDB     #$FF                    ; 中間バイトは全ビットが矩形内
                STB     <PBF_MASK
        ENDC

;------------------------------------------------------------------------------
; pb1e_op — $1E 矩形書込み 2 の 1 byte 合成 (演算指定を 1 箇所に集約)
;   PUT@ 形式2 は動作型 PSET / OR / AND / XOR を採り、
;   3 プレーンそれぞれへ独立に適用する。矩形の外へはみ出す端バイトのビットは
;   どの動作型でも画面の値を保つ。
;     PSET … 画面 ← パターン        OR  … 画面 ← 画面 | パターン
;     AND  … 画面 ← 画面 & パターン  XOR … 画面 ← 画面 ^ パターン
;   入力: A = パターン∧端マスク / PBF_MASK = 端マスク (矩形内=1) /
;         U = 対象プレーンの VRAM 実アドレス / WK_PB_FN = 動作型コード
;   出力: [U] 更新。A,B 破壊。U 保存。
;   合成は new = old ^ ((old ^ t) & mask) の共通形で行い、t だけを動作型で
;   切り替える。マスク外ビットは old のまま残る。
;------------------------------------------------------------------------------
pb1e_op:
                LDB     <WK_PB_FN
                CMPB    #2
                BEQ     p1o_or
                CMPB    #3
                BEQ     p1o_and
                CMPB    #4
                BEQ     p1o_xor
                BRA     p1o_st                  ; PSET ほか: t = パターン
p1o_and:
                ANDA    ,U                      ; t = 画面 & パターン
                BRA     p1o_st
p1o_or:
                ORA     ,U                      ; t = 画面 | パターン
                BRA     p1o_st
p1o_xor:
                EORA    ,U                      ; t = 画面 ^ パターン
p1o_st:
                EORA    ,U                      ; A = t ^ old
                ANDA    <PBF_MASK               ;   矩形内ビットに限定
                EORA    ,U                      ;   = new
                STA     ,U
                RTS

;------------------------------------------------------------------------------
; prw_fastmid — $1C/$1E 任意幅ブリットの中間バイト高速化 (入口ガード+経路選択)
;   pbb_row の中間バイトループ (1 出力バイト毎に JSR pbb_fetch/書込ヘルパを回す
;   汎用 FIFO 経路) を、行内定数シフト k=PBB_CNT のインラインループへ差替える。
;   行に必要な NB-2 バイトのソースが現チャンク内 (計数・共有 RAM 終端) に揃って
;   いる場合のみ適用し、それ以外は汎用ループ (prw_mid) へ落ちる。
;   出力バイト = (直前ソース:現ソース) >> k (k = 行内不変) で、汎用 FIFO が
;   生成する値と恒等 (FIFO 残量 = 直近消費バイトの下位 k bit、という不変条件)。
;   入力: A = NB-2 (>0)、U = 中間先頭の B 面 VRAM アドレス、PBB_CNT = k (0..7)。
;   本体ループは prw_fast_go (別領域)。ここはガードと経路選択のみ。
;------------------------------------------------------------------------------
prw_fastmid:
                CMPA    <PBS_CNT                ; ソース残量 < NB-2 → 汎用へ
                BHI     pfm_gen
                TST     <PBF_INV                ; fn5 (パターン反転) は FIFO 供給点で
                BNE     pfm_gen                 ;   反転するため汎用ループ限定
                LDY     <PBS_PTR                ; Y = ソース読出ポインタ
                TFR     Y,X
                LEAX    A,X
                CMPX    #$D400                  ; 共有 RAM 終端を越える読出 → 汎用へ
                BHI     pfm_gen
        IFNDEF SUBSYS_TB                        ; Type-B の位置の $1E は tb1e_entry が
                TST     <PBF_PMASK              ;   受けるので PBF_PMASK は常に 0 ($1C)
                BEQ     pfm_1c                  ; ($1E の U は行頭でプレーン加算済)
                LDA     <WK_PB_FN
                BEQ     pfm_e_ow                ; PSET = 上書きチェーン
                CMPA    #2
                BNE     pfm_gen                 ; AND/XOR ほかは汎用ループ (pb1e_op)
                LDX     #pfm_ch_or+14           ; $1E OR 合成チェーン
                BRA     prw_fast_go
pfm_gen:
                BRA     prw_mid                 ; 汎用 FIFO ループ
        ENDC
pfm_1c:
                LDX     #pfm_ch_1c+14           ; $1C 3 面 RMW チェーン
                BRA     prw_fast_go
        IFDEF SUBSYS_TB
pfm_gen:
                BRA     prw_mid                 ; 汎用 FIFO ループ
        ENDC
        IFNDEF SUBSYS_TB
pfm_ch_ow:                                      ; --- $1E 上書き ---
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
pfm_st:
                STB     ,U+
                DEC     <PBB_BCNT
                BNE     prw_fast_lp
                BRA     pfm_done_c
pfm_ch_or:                                      ; --- $1E OR 合成 ---
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                ORB     ,U                      ; OR (bit=0 は画面保持)
                BRA     pfm_st
pfm_e_ow:
                LDX     #pfm_ch_ow+14           ; $1E 上書きチェーン
        ENDC

;------------------------------------------------------------------------------
; prw_fast_go — $1C/$1E 中間バイト インラインループ本体 (prw_fastmid から JMP)
;   X = ケース別シフトチェーン入口 (pfm_ch_xx+14、ここから k 段ぶん手前へ補正)、
;   Y = ソースポインタ、U = 書込先 ($1E はプレーン実アドレス / $1C は B 面基準)、
;   PBB_BCNT = 残バイト数。1 バイトの合成は LDD -1,Y (直前:現) → 右 k シフト →
;   下位バイト B が出力。ループヘッド (prw_fast_lp) は 3 ケース共用で、
;   JMP ,X だけがケース別チェーンへ分岐する。
;------------------------------------------------------------------------------
prw_fast_go:
                LDB     <PBB_CNT                ; k (行内不変、0..7)
                ASLB
                NEGB
                LEAX    B,X                     ; X = チェーン入口 (k 段実行)
prw_fast_lp:
                LDD     -1,Y                    ; A:B = 直前:現ソースバイト
                LEAY    1,Y
                JMP     ,X                      ; → 右 k シフト後 B = 出力バイト
pfm_ch_1c:                                      ; --- $1C: 3 面 RMW (マスク=パターン) ---
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                LSRA
                RORB
                STB     <PBF_MASK               ; 書込マスク = パターンバイト
                BSR     pbf_store3              ; 3 面 RMW (U=B 面基準、U/X/Y 保存)
                LEAU    1,U
                DEC     <PBB_BCNT
                BNE     prw_fast_lp
pfm_done_c:
                STY     <PBS_PTR                ; ソース消費 (NB-2 byte) を清算
                LDA     <PBS_CNT
                SUBA    <PBF_NB
                ADDA    #2
                STA     <PBS_CNT
                LDA     -1,Y                    ; FIFO 残量を汎用形式で書戻し:
                LDB     <PBB_CNT                ;   ACC = 最終消費バイトの下位 k bit
                LDX     #pfm_wch                ;   を MSB 詰め (k=0 は 8 シフトで空)
                ABX
                JMP     ,X                      ; → ASLA ×(8-k) → 書戻し → prw_last

;--- pbf_row — 1 行分のバイトブリット ----------------------------------------
;   入力: PBF_* 固定ワーク一式。U/Y/A/B 破壊。
pbf_row:
                LDU     <PBF_ROWVR
                LDY     <PBF_ROWST
                CLR     <PBF_PREV
                LDA     <PBF_NB
                STA     <PBF_CNT
prb_byte:
                LDA     <PBF_PHASE          ; アラインパターンバイト生成:
                STA     <PBF_SHC            ;   B = (prev:cur) >> phase の下位
                LDB     ,Y+
                LDA     <PBF_PREV
                STB     <PBF_PREV
                TST     <PBF_SHC
                BEQ     prb_al
prb_sh:
                LSRA
                RORB
                DEC     <PBF_SHC
                BNE     prb_sh
prb_al:
                LDA     #$FF                    ; 書込マスク: 中間=FF/左端=lmask/右端=&rmask
                STA     <PBF_MASK
                LDA     <PBF_CNT
                CMPA    <PBF_NB
                BNE     prb_m1
                LDA     <PBF_LMASK
                STA     <PBF_MASK
prb_m1:
                LDA     <PBF_CNT
                CMPA    #1
                BNE     prb_m2
                LDA     <PBF_MASK
                ANDA    <PBF_RMASK
                STA     <PBF_MASK
prb_m2:
        IFNDEF SUBSYS_TB
                TST     <PBF_PMASK          ; 0=$1C / 非0=$1E
                BNE     prb_1e
        ENDC
                ; $1C: 書込マスクを端マスク∧パターンへ絞り、pbf_store3 が面別
                ;   (A,D) 定数 (pb1_decode が展開済) で描画ファンクションを適用する。
                TFR     B,A
                ANDA    <PBF_MASK
                STA     <PBF_MASK
                BSR     pbf_store3
                BRA     prb_nx
        IFNDEF SUBSYS_TB
prb_1e:
                ; $1E: 対象プレーンのみへ端マスク付き合成 (他プレーンには触れない)。
                ;   演算指定の適用は pb1e_op に集約 (4 経路で同一の意味論)。
                PSHS    B,U                 ; パターンと行走査ポインタ (B 面基準)
                TFR     U,D                 ;   ※ TFR U,D は B を壊すので退避が要る
                ADDD    <PBF_PLOFS
                TFR     D,U                 ; U = 対象プレーン実アドレス
                PULS    A                   ; A = 退避したパターン
                ANDA    <PBF_MASK           ;   ∧端マスク
                JSR     pb1e_op
                PULS    U
        ENDC
prb_nx:
                LEAU    1,U
                DEC     <PBF_CNT
                BNE    prb_byte
                RTS

;--- pbf_store3 — 3 プレーンへ 1 byte 書込 (pbf_row から BSR) ------------------
;   面ごとに new = old ^ (((old & A) ^ D ^ old) & m) を適用する (m=PBF_MASK、
;   A=PBF_CB/CR/CG、D=PBF_DB/DR/DG。pb1_decode が fn×色から展開済)。
;   色置換系 (PBF_FF 非0 = fn0/1/5) のマスク $FF は直書き。U = B プレーン (保存)。
pbf_store3:
                LDA     <PBF_MASK
                BEQ     pst_skip                ; マスク 0 = 書くビットなし (画面保持)
                CMPA    #$FF
                BNE     pst_slow
                TST     <PBF_FF                 ; 直書きは色置換系のみ可
                BEQ     pst_slow                ;   (保持/反転面は RMW が必要)
                LDB     <PBF_DB
                STB     ,U
                LDB     <PBF_DR
                STB     $4000,U
                LDB     <PBF_DG
                STB     $8000,U
                RTS
pst_slow:
                LDA     ,U                  ; B プレーン: old
                ANDA    <PBF_CB
                EORA    <PBF_DB             ; (old & A) ^ D = 書込候補
                EORA    ,U
                ANDA    <PBF_MASK           ; 変化ビットを m に限定
                EORA    ,U                  ; = new
                STA     ,U
                LDA     $4000,U             ; R プレーン
                ANDA    <PBF_CR
                EORA    <PBF_DR
                EORA    $4000,U
                ANDA    <PBF_MASK
                EORA    $4000,U
                STA     $4000,U
                LDA     $8000,U             ; G プレーン
                ANDA    <PBF_CG
                EORA    <PBF_DG
                EORA    $8000,U
                ANDA    <PBF_MASK
                EORA    $8000,U
                STA     $8000,U
pst_skip:
                RTS

;--- pc_curmove — カーソル移動オーダ (参考書籍) --------------------
;   $1C = 右 / $1D = 左 / $1E = 上 / $1F = 下 (参考書籍のキャラクタコード表)。
;   参考書籍のとおり端でラップする: 右端の次は同じ行の左端、左端の前は同じ行の
;   右端、上端の上は同じ桁の最下段、下端の下は同じ桁の最上段。
;   オーダコードの bit1 が軸 (0 = 桁 / 1 = 行)、bit0 と bit1 が一致していれば
;   増加方向を表す。桁数・行数 (WK_SCREEN_W / WK_SCREEN_H) はカーソル座標
;   (WK_CURSOR_X / WK_CURSOR_Y) の 2 byte 手前に同じ並びで置いてあるので、
;   軸を選ぶ 1 つの変位で座標とその上限の両方に届く。
pc_curmove:
                PSHS    B,X                     ; 呼出元の計数とポインタを守る
                TFR     A,B
                LSRB
                ANDB    #$01                    ; 0 = 桁 / 1 = 行
                LDX     #WK_CURSOR_X
                ABX                             ; ,X = 対象座標 / -2,X = その上限
                PSHS    A
                LSRA
                EORA    ,S+                     ; bit0 ^ bit1
                LSRA                            ; C = 1 なら減少方向
                BCS     pcm_dec
                LDA     ,X
                INCA
                CMPA    -2,X
                BCS     pcm_st
                CLRA                            ; 端を越えた → 先頭へラップ
                BRA     pcm_st
pcm_dec:
                LDA     ,X
                BNE     pcm_d1
                LDA     -2,X                    ; 先頭から戻った → 末尾へラップ
pcm_d1:
                DECA
pcm_st:
                STA     ,X
                PULS    B,X,PC

;--- tab_at — 現在の桁が TAB 停止位置なら C=1 で戻る --------------------------
;   TAB_TABLE は 10 byte = 80 桁ぶんのビット列で、先頭バイトの最上位ビットが
;   画面の左端に対応する (参考書籍)。全レジスタ温存。
tab_at:
                PSHS    A,B,X
                LDX     #TAB_TABLE
                LDB     <WK_CURSOR_X
                PSHS    B
                LSRB
                LSRB
                LSRB                            ; B = 桁 / 8 = 表のバイト位置
                ABX
                PULS    B
                ANDB    #$07
                INCB                            ; 桁のビットを最上位へ寄せる回数
                LDA     ,X
tat_sh:
                ASLA                            ; C = その桁の停止ビット
                DECB
                BNE     tat_sh
                PULS    A,B,X,PC

;--- pc_ht — HT オーダ (参考書籍) ---------------------------------------
;   コンソール動作制御が TAB 動作 (参考書籍の CF bit2) を許している
;   ときだけ、次の TAB 停止位置までバッファアドレスを進め、その移動した範囲を
;   SPACE コード ($20) で埋める。停止位置は $0B TAB SET が置いた 80 ビットの表
;   (参考書籍)。行を折返したらそこで止める (停止位置が 1 つも無い表でも
;   必ず終わる)。
pc_ht:
                LDA     <WK_CSCTL       ; コンソール動作制御の現在値
                BITA    #$04                    ; bit2 = TAB 動作
                BEQ     pht_ret
pht_lp:
                LDA     #$20                    ; 移動した範囲は SPACE で埋める
                JSR     putchar                 ; 1 桁ぶん進む (折返しも同じ規則で)
                LDA     <WK_CURSOR_X
                BEQ     pht_ret                 ; 行を折返した → そこで止める
                BSR     tab_at
                BCC     pht_lp
pht_ret:
                RTS

;--- pc_home — HOME オーダ (参考書籍) -----------------------------------
;   バッファアドレスを現在のモード画面の先頭へ移す。画面消去 ($01/$02/$0C/$0D)
;   の出口でも同じ位置へ戻す。
pc_home:
                CLR     <WK_CURSOR_X
                LDA     <WK_WIN_TOP
                STA     <WK_CURSOR_Y
                RTS

;--- pc_el — EL オーダ (参考書籍) ---------------------------------------
;   現在のバッファアドレスから、そのフィールドの終りまでを Null コード ($00) で
;   埋める。このオーダによってバッファアドレスは変化しない。本体は el_body。
pc_el:
                PSHS    A,B,X,U
                JSR     el_body
                PULS    A,B,X,U,PC

;==============================================================================
; オーダ (副指令) の追加分 — 参考書籍のオーダ一覧のうち、putchar の
;   区画で判定していないコードをここで受ける。入口 pc_ord_far から JMP。
;   A = オーダコード ($00〜$1F)。呼出元 cmd_03_console の文字数カウンタ (B) と
;   文字列ポインタ (X) は壊さないこと。
;==============================================================================
pc_ord_ext:
                CMPA    #$07            ; BEL (参考書籍)
                BEQ     pc_bel
                CMPA    #$05                    ; EL (参考書籍)
                BEQ     pc_el
                CMPA    #$09                    ; HT (参考書籍)
                BEQ     pc_ht
                CMPA    #$0B                    ; HOME (参考書籍)
                BEQ     pc_home
                CMPA    #$1C                    ; カーソル移動 (参考書籍)
                BCS     pc_noop
                CMPA    #$1F
                BLS     pc_curmove
pc_noop:
                RTS                             ; 一覧に無いコードは何も動作しない。
pc_bel:
                TST     >$D403          ; ベルを鳴らす (参考書籍:
                RTS                     ;   $D403 の読出しの負パルスがブザー)

        IFDEF SUBSYS_AV
;--- c45_entry — $45 KEYBOARD CONTROL (FM77AV の Type-A / Type-B の位置に置く ROM だけ) ---
;   並びは参考書籍による。共有 RAM 相対 2 =
;   コマンドコード $45、相対 3 = 副命令コード (CMD) $00〜$05、相対 4〜5 = パラメータ
;   (PARAM。副命令ごとに意味が変わり、$00 / $02 / $04 は上位バイト = 相対 4 だけを使う)。
;   応答は相対 3 = DATA (副命令ごとに意味が変わる)。
;     $00 キーコード系の設定      (PARAM 上位 $00 JIS9bit / $01 FM16β 準拠 / $02 スキャンコード)
;     $01 キーコード系の読み取り  (パラメータなし。DATA に上と同じ 3 値)
;     $02 LED の点灯・消灯        (PARAM 上位 $00 CAP 点灯 / $01 CAP 消灯 / $02 かな 点灯 / $03 かな 消灯)
;     $03 LED の点灯状態の読み取り (パラメータなし。DATA の bit0 = CAP / bit1 = かな、1 = 点灯)
;     $04 オートリピートの選択    (PARAM 上位 $00 行う / $01 行わない)
;     $05 オートリピートの動作時間 (PARAM 上位 = 開始時間 / 下位 = 動作時間。単位 10 ms)
;       ($05 は上位・下位のどちらかが $00 のとき、キーエンコーダ側が既定の時間
;        (開始 0.7 秒 / 動作 0.07 秒) を採る。当 ROM は値をそのまま渡す)
;   副命令とパラメータをデータレジスタ $D431 へ 1 バイトずつ書き、書くたびに
;   ステータスレジスタ $D432 の bit0 (ACK。0 = 応答中) が 1 に戻るまで待つ。
;     $00 / $02 / $04 … 2 バイト (副命令 + PARAM 上位)
;     $05 … 3 バイト (副命令 + PARAM 上位 + PARAM 下位)
;     $01 / $03 … 1 バイト書いた後、$D432 の bit7 (0 = 受信データあり) を待って
;       $D431 から 1 バイト読み、相対 3 へ置く
;   $06 以上の副命令、範囲を超えるパラメータはキーエンコーダへ何も書かず、
;   相対 0 に $45 (コマンドまたはパラメータの誤り) を置く。
;   U = 共有 RAM の読み位置 (入口では相対 3)、X = $D431 (レジスタの基点)。
c45_entry:
                LDX     #$D431
                LDA     ,U                      ; A = キーエンコーダへの命令
                LDB     #3                      ; B = 書くバイト数
                CMPA    #5
                BHI     c45_err                 ; $06 以上は誤り
                BEQ     c45_send                ; $05: 3 バイト
                DECB                            ; 以下 2 バイト
                LSRA                            ; C = 奇数 ($01/$03)、A = 命令 / 2
                BCS     c45_get
                EORA    #2                      ; 引数の上限: $00→2 / $02→3 / $04→0 (→1)
                BNE     c45_lim
                INCA
c45_lim:
                CMPA    1,U
                BHS     c45_send
c45_err:
                JMP     gr_err_param            ; 相対 0 ← $45
c45_get:
                DECB                            ; 1 バイト
                BSR     c45_send
c45_rx:
                LDA     1,X                     ; $D432 bit7 = 0 (受信データあり) を待つ
                BMI     c45_rx
                LDA     ,X                      ; $D431 から応答を読み
                STA     -1,U                    ;   相対 3 へ置く
                RTS
c45_send:
                LDA     ,U+
                STA     ,X                      ; $D431 へ 1 バイト
c45_ack:
                LDA     1,X                     ; $D432 bit0 (ACK) が 1 に戻るまで待つ
                LSRA
                BCC     c45_ack
                DECB
                BNE     c45_send
                RTS

;==============================================================================
; FM77AV の描画 ($15 LINE / $18 PAINT、$39、$22)
;   Type-A / Type-B の位置に置く ROM だけが持つ。並びと振る舞いは動作観察による。
;   SUBSYS_TB (Type-B の位置): 320×200 の画面の 1 画素の色は B・R・G 各 4 ビットで、
;   ビット 3 / 2 / 1 / 0 が処理ページ 0 のオフセット $0000 / 処理ページ 0 のオフセット
;   $2000 / 処理ページ 1 の $0000 / 処理ページ 1 の $2000 に置かれた B・R・G の 3 面にある。
;   直線はラインドロワ ($D420-$D42B) でこの 4 組に 1 回ずつ引く。座標は負を 0、X を 319、
;   Y を 199 に丸める。
;   SUBSYS_TB 未定義 (Type-A の位置): 色は 1 バイト (bit0 = B / bit1 = R / bit2 = G) で、
;   直線はラインドロワで 1 回引く。座標は負を 0、X を 639、Y を 199 に丸める。
;==============================================================================
IO_ALU_CMD      EQU     $D410   ; ALU の命令 (bit7 = 使う、下位 3 ビット = 演算。0 = PSET / 7 = 比較)
IO_ALU_COL      EQU     $D411   ; ALU の色 (bit0 = B / bit1 = R / bit2 = G)
IO_ALU_MSK      EQU     $D412   ; ALU のマスク (1 のビットは書かない)
IO_ALU_CMP      EQU     $D413   ; 書 = 比較データ 0 ($D414-$D41A が 1〜7) / 読 = 比較の結果
IO_ALU_DIS      EQU     $D41B   ; ALU の面の禁止
IO_LD_BASE      EQU     $D420   ; ラインドロワ: オフセット / 線種 / X0 / Y0 / X1 / Y1 (各 2 バイト)
IO_AV_MISC      EQU     $D430   ; 書 = bit6 表示ページ / bit5 処理ページ、読 = bit4 ラインドロワの空き / bit2 VSYNC
AV_MISC         EQU     $D0E7   ; 1: $D430 へ書く値の控え ($22 が更新する)
AV_PG0          EQU     $D0E8   ; 1: 描画中に $D430 へ書く値 (bit1 を立てる。Type-B の位置は控えの bit5 も落とし、640×200 は $22 の処理ページのまま描く)
AV_ALU          EQU     $D0E9   ; 1: 線を引くときの $D410 の値
AV_STY          EQU     $D0EA   ; 2: 線種 ($D422-$D423)
AV_COL          EQU     $D0EC   ; 4: 4 回ぶんの $D411 の値 (色のビット 3 → 0 の順)
AV_T            EQU     $D0F0   ; 3: 色の分解の作業域
AV_CL           EQU     $D3A4   ; PAINT の色の比較値 (1 色 4 バイト × 最大 9 色。共有 RAM の引数の後ろ)
        IFDEF SUBSYS_TB
AV_XMAX         EQU     319     ; X の上限
AV_NP           EQU     4       ; 1 本の線を引く回数
AV_STY34        EQU     $00FF   ; 形状 3・4 の線種
G2_ACC          EQU     AV_STY  ; 2: $1D の 1 画素の 12 ビット ($1D は線を引かないので線種の場所を使う)
G2_ROW          EQU     AV_COL  ; 2: $1D の行の先頭のオフセット
G2_MASK         EQU     AV_T    ; 1: $1D の画素のビット
TB_BG           EQU     $D0F3   ; 2: BC (BBBBRRRR GGGG0000)。$01 / $02 / $0D が置く
TB_FW           EQU     $D0F5   ; 2: 字の色 / 画素の色 (BBBBRRRR GGGG0000)
TB_BW           EQU     $D0F7   ; 2: 地の色 (同)
TB_M            EQU     $D0F9   ; 1: 字形の描画の地のビットの値
TB_D            EQU     $D0FA   ; 1: 字形の描画の字と地で違うビット
TB_E            EQU     $D0FB   ; 2: 12 の面の範囲の終端 (消去とスクロール)
        ELSE
AV_XMAX         EQU     639     ; X の上限
AV_NP           EQU     1       ; 1 本の線を引く回数
AV_STY34        EQU     $0000   ; 形状 3・4 の線種
        ENDC

;--- line15_entry (SUBSYS_AV) — $15 LINE ----------------------------------------
;   SUBSYS_TB: 相対 3〜5 = 色 B,R,G (各 4 ビット) / 相対 6 = 演算 / 相対 7〜14 =
;   X0,Y0,X1,Y1 / 相対 15 = 形状。Type-A の位置: 相対 3 = 色 / 相対 4 = 演算 /
;   相対 5〜12 = X0,Y0,X1,Y1 / 相対 13 = 形状 (U を 2 戻して同じ位置で読む)。
;   演算は 0 PSET / 1 PRESET / 2 OR / 3 AND / 4 XOR (5 以上は $40)。形状は 0 直線 /
;   1 矩形 / 2 塗った矩形 / 3 線種 AV_STY34 の直線 / 4 線種 AV_STY34 の矩形 / 5 以上は
;   直線。PRESET は色 0 で引く。
line15_entry:
        IFNDEF SUBSYS_TB
                LEAU    -2,U
        ENDC
                JSR     av_begin_e      ; 終わったら $D430 と ALU を戻す
                LDA     3,U
                CMPA    #4
                BHI     avl_efn
                CMPA    #2
                BCS     avl_col                 ; 0 / 1 は PSET
                ORA     #$80
                STA     <AV_ALU
avl_col:
        IFDEF SUBSYS_TB
                TFR     U,X
                JSR     av_col4c
        ELSE
                LDA     2,U
                STA     <AV_COL
        ENDC
                LDA     3,U
                DECA
                BNE     avl_xy
                CLRB                            ; PRESET: 色 0
                STD     <AV_COL
                STD     <AV_COL+2
avl_xy:
                LDB     12,U                    ; 形状
                CMPB    #5
                BCC     avl_ln                  ; 5 以上は直線
                CMPB    #3
                BCS     avl_sh
                LDX     #AV_STY34
                STX     <AV_STY
                SUBB    #3
avl_sh:
                DECB
                BMI     avl_ln                  ; 直線
                PSHS    B
                LEAX    4,U
                JSR     av_sort                 ; 矩形の隅は丸めずに並べる
                PULS    B
                TSTB
                BNE     av_fill                 ; 塗った矩形
                JMP     av_box                  ; 矩形
avl_ln:
                LEAX    4,U                     ; 端点は丸めずに写し、avd_line が描画範囲で切り取る
                LDY     #WK_LN_X0
                LDB     #4
avl_cp:
                LDU     ,X++
                STU     ,Y++
                DECB
                BNE     avl_cp
                BRA     avd_line
avl_efn:
                JMP     gr_err_fn

;--- av_fill — WK_RECT_* の矩形を描画範囲に丸め、上の行から水平線で塗る -------------
av_fill:
                LDX     #WK_RECT_X0
                JSR     av_xy2
                LDD     <WK_RECT_X0
                STD     <WK_LN_X0
                LDD     <WK_RECT_X1
                STD     <WK_LN_X1
                LDD     <WK_RECT_Y0
afl_lp:
                STD     <WK_LN_Y0
                STD     <WK_LN_Y1
                BSR     avd_nc
                LDD     <WK_LN_Y0
                CMPD    <WK_RECT_Y1
                BCC     afl_end
                ADDD    #1
                BRA     afl_lp
afl_end:
                RTS

;--- avd_line — WK_LN_X0/Y0/X1/Y1 の直線を描画範囲で切り取って 4 回引く ----------------
;   色 AV_COL (4 回ぶん)、線種 AV_STY、$D410 に AV_ALU。回ごとに処理ページ
;   ($D430 bit5: 1・2 回目 0 / 3・4 回目 1) とオフセット ($D420: 1・3 回目 $00 /
;   2・4 回目 $10) を替える。線種と端点は 1 回だけ書き、回ごとに $D42B へ Y1 の下位を
;   書き直して引く。書く前と引く前にラインドロワの空き ($D430 読みの bit4) を待つ。
;   avd_nc は切り取りを済ませた線を引く入口 (塗りの区間・矩形の辺と塗り)。
;   X/Y/U 保存、A/B 破壊。
avd_line:
                PSHS    X,Y,U
                JSR     av_clip                 ; 描画範囲の外を切り取る (動作観察)
                BCC     adl_go
                PULS    X,Y,U,PC                ; 線は範囲の外
avd_nc:
                PSHS    X,Y,U
adl_go:
                LDX     #IO_LD_BASE             ; X = $D420
adl_w0:
                LDA     16,X                    ; $D430 bit4 = 1 (空き) を待ってから書く
                BITA    #$10
                BEQ     adl_w0
                LDA     <AV_ALU
                STA     -16,X                   ; $D410
                LDD     <AV_STY
                STD     2,X                     ; $D422-$D423 = 線種
                LDD     <WK_LN_X0
                STD     4,X
                LDD     <WK_LN_Y0
                STD     6,X
                LDD     <WK_LN_X1
                STD     8,X
                LDA     <WK_LN_Y1
                STA     10,X                    ; Y1 の上位
                LDU     #AV_COL
adl_lp:
                LDA     16,X                    ; 空きを待つ
                BITA    #$10
                BEQ     adl_lp
                TFR     U,D
                SUBB    #AV_COL&$FF             ; B = 何回目か (0〜3)
                LDA     <AV_PG0
                BITB    #2
                BEQ     adl_pg
                ORA     #$20
adl_pg:
                STA     16,X                    ; $D430
                ANDB    #1
                BEQ     adl_of
                LDB     #$10
adl_of:
                TFR     B,A
                CLRB
                STD     ,X                      ; $D420-$D421 = オフセット
                LDA     ,U+
                STA     -15,X                   ; $D411 = 色
                LDA     <WK_LN_Y1+1
                STA     11,X                    ; $D42B の書込みで引く
                CMPU    #AV_COL+AV_NP
                BCS     adl_lp
                PULS    X,Y,U,PC

;--- av_seg — 辺 1 本 (WK_LN_*、水平か垂直) を引く ----------------------------------
;   動かない方の座標が描画範囲の外なら引かない。動く方の座標は描画範囲に丸める。
;   引いた後、線種を辺の画素数だけ左へ回す (次の辺へ模様を続ける)。
av_seg:
                LDX     #WK_LN_X0
                LDD     ,X
                CMPD    4,X
                BNE     asg_y
                CMPD    <WK_VIEW_X0
                BLT     abx_r                   ; 垂直の辺の X が描画範囲の外
                CMPD    <WK_VIEW_X1
                BGT     abx_r
asg_y:
                LDD     2,X
                CMPD    6,X
                BNE     asg_draw
                CMPD    <WK_VIEW_Y0
                BLT     abx_r                   ; 水平の辺の Y が描画範囲の外
                CMPD    <WK_VIEW_Y1
                BGT     abx_r
asg_draw:
                JSR     av_xy2
                JSR     avd_nc
                LDD     <WK_LN_X1               ; 画素数 = |(X1-X0)+(Y1-Y0)| + 1 (16 で割った余り)
                SUBD    <WK_LN_X0
                ADDD    <WK_LN_Y1
                SUBD    <WK_LN_Y0
                BPL     asg_cnt
                NEGB
asg_cnt:
                INCB
                ANDB    #$0F
                BEQ     abx_r
asg_rot:
                ASL     <AV_STY+1
                ROL     <AV_STY
                BCC     asg_rn
                INC     <AV_STY+1
asg_rn:
                DECB
                BNE     asg_rot
abx_r:
                RTS

;--- abx_v / abx_h — 垂直の辺 (X,D)-(X,Y) / 水平の辺 (D,Y)-(X,Y) を av_seg で引く ---
abx_v:
                STD     <WK_LN_Y0
                STY     <WK_LN_Y1
                STX     <WK_LN_X0
                STX     <WK_LN_X1
                BRA     av_seg
abx_h:
                STD     <WK_LN_X0
                STX     <WK_LN_X1
                STY     <WK_LN_Y0
                STY     <WK_LN_Y1
                BRA     av_seg

;--- av_box — WK_RECT_* (小 / 大の順) の枠を上辺・右辺・下辺・左辺の順に引く ------
;   上辺 (X0,Y0)-(X1,Y0)。Y0 = Y1 ならここまで。右辺 (X1,Y0+1)-(X1,Y1)。X0 = X1 なら
;   ここまで。下辺 (X1-1,Y1)-(X0,Y1)。左辺 (X0,Y1-1)-(X0,Y0+1) は高さが 2 以上のとき。
;   隅は -1 / 320 / 200 でもよく、画面の外の辺は引かない (av_seg)。
av_box:
                LDD     <WK_RECT_X0
                LDX     <WK_RECT_X1
                LDY     <WK_RECT_Y0
                BSR     abx_h                   ; 上辺
                LDD     <WK_RECT_Y0
                CMPD    <WK_RECT_Y1
                BEQ     abx_r
                ADDD    #1
                LDX     <WK_RECT_X1
                LDY     <WK_RECT_Y1
                BSR     abx_v                   ; 右辺
                LDD     <WK_RECT_X1
                CMPD    <WK_RECT_X0
                BEQ     abx_r
                SUBD    #1
                LDX     <WK_RECT_X0
                LDY     <WK_RECT_Y1
                BSR     abx_h                   ; 下辺
                LDD     <WK_RECT_Y1
                SUBD    #1
                PSHS    D
                SUBD    <WK_RECT_Y0
                PULS    D                       ; (PULS は CC を変えない)
                BLE     abx_r                   ; 高さが 2 未満
                LDX     <WK_RECT_Y0
                LEAY    1,X
                LDX     <WK_RECT_X0             ; 左辺
                BRA     abx_v

;--- av_xy — X が指す座標 1 組を描画範囲 (WK_VIEW_*) に丸めて Y へ書く (X / Y は 4 進む) ----
av_xy:
                PSHS    U
                LDU     #WK_VIEW_X0
                BSR     axy_1                   ; X の組、続けて Y の組 (U が Y の下限を指す)
                BSR     axy_1
                PULS    U,PC

;--- av_xy2 — 座標 2 組をまとめて丸める (av_xy を 2 回)。av_xy2 は Y = X から始める ---
;   av_fill / av_seg / $39 が共用する。
av_xy2:
                TFR     X,Y
av_xy2y:
                BSR     av_xy
                BRA     av_xy

axy_1:
                LDD     ,X++
                CMPD    ,U                      ; 下限
                BGE     axy_lo
                LDD     ,U
axy_lo:
                CMPD    4,U                     ; 上限 (X1 / Y1 は 4 バイト先)
                BLE     axy_hi
                LDD     4,U
axy_hi:
                STD     ,Y++
                LEAU    2,U
                RTS

;--- av_sort — X が指す X0,Y0,X1,Y1 を小さい方 / 大きい方の順 (符号付き) に WK_RECT_* へ ---
av_sort:
                LDY     #WK_RECT_X0
                BSR     avs_1                   ; X の組、続けて Y の組
avs_1:
                LDD     ,X
                CMPD    4,X
                BLE     avs_2
                LDD     4,X
                STD     ,Y
                LDD     ,X
                BRA     avs_3
avs_2:
                STD     ,Y
                LDD     4,X
avs_3:
                STD     4,Y
                LEAX    2,X
                LEAY    2,Y
                RTS

        IFDEF SUBSYS_TB
;--- av_col4 — X が指す色 B,R,G を 4 回ぶんの値に分けて Y の手前 4 バイトへ置く ----
;   値の bit0 = B / bit1 = R / bit2 = G。ビット 0 の値から後ろ向きに置く (Y は 4 戻る)。
;   X 保存、A/B 破壊。
av_col4c:                                       ; Y = AV_COL+4 で呼ぶ入口 (4 か所が同じ)
                LDY     #AV_COL+4
av_col4:
                LDD     ,X
                STD     <AV_T
                LDA     2,X
                STA     <AV_T+2
                LDB     #4
acl_lp:
                CLRA
                LSR     <AV_T+2                 ; G
                ROLA
                LSR     <AV_T+1                 ; R
                ROLA
                LSR     <AV_T                   ; B
                ROLA
                STA     ,-Y
                DECB
                BNE     acl_lp
                RTS
        ENDC

;--- av_end — FM77AV の LINE / PAINT / $39 の共通の出口 ---------------------------
;   $D430 を控えの値に戻し、ALU を止める (他の命令は ALU を使わずに VRAM へ書く)。
av_end:
                LDA     <AV_MISC
                STA     >IO_AV_MISC
                CLR     >IO_ALU_CMD
                RTS

;--- av_begin — ALU の面の禁止を解き、描画中の $D430 の値・PSET・実線を用意する ---
;--- av_begin_e — 戻り先の下に av_end を積んでから av_begin へ落ちる ----------------
;   JSR で呼ぶと、呼出元の RTS が av_end ($D430 と ALU の後始末) へ行く。4 か所が
;   共用する。
av_begin_e:
                PULS    X               ; X = 呼出元への戻り先
                LDD     #av_end
                PSHS    D               ; その下に av_end を積み
                PSHS    X               ;   戻り先を積み直す
av_begin:
                LDA     #$08
                STA     >IO_ALU_DIS
                LDA     <AV_MISC
        IFDEF SUBSYS_TB
                ANDA    #$DF                    ; 320×200 は層 0・1 を処理ページ 0 に置く
        ENDC
                ORA     #$02
                STA     <AV_PG0
                LDA     #$80
                STA     <AV_ALU
                LDD     #$FFFF
                STD     <AV_STY
                RTS

;--- c39_entry — $39 描画範囲の設定と枠付きの矩形の塗り ------------------------------
;   相対 3〜10 = X0,Y0,X1,Y1。SUBSYS_TB: 相対 11〜13 = 枠の色 B,R,G / 相対 14〜16 = 塗る色
;   B,R,G。Type-A の位置: 相対 11 = 枠の色 / 相対 12 = 塗る色。
;   1 画素外側に枠を引いてから、範囲 (両端を含む) を塗り、その矩形 (画面に丸めたもの) を
;   以後の $15 / $16 / $18 の描画範囲 (WK_VIEW_*) にする。枠と塗りは描画範囲で切らない
;   (動作観察)。
c39_entry:
                BSR     av_begin_e      ; 終わったら $D430 と ALU を戻す
                BSR     av_view_full    ; 枠と塗りは画面全体に描く
        IFDEF SUBSYS_TB
                LEAX    8,U
                BSR     av_col4c
        ELSE
                LDA     8,U
                STA     <AV_COL
        ENDC
                BSR     c39_rect
                LDX     #WK_RECT_X0             ; 枠は 1 画素外側
                LDD     #$FFFF
                BSR     c39_adj
                BSR     c39_adj
                LDD     #1
                BSR     c39_adj
                BSR     c39_adj
                LBSR    av_box
        IFDEF SUBSYS_TB
                LEAX    11,U
                BSR     av_col4c
        ELSE
                LDA     9,U
                STA     <AV_COL
        ENDC
                BSR     c39_rect
                LDX     #WK_RECT_X0             ; 描画範囲 = 塗る矩形 (画面に丸めたもの)
                LDY     #WK_VIEW_X0
                JSR     av_xy2y
                LBRA    av_fill
c39_adj:
                PSHS    D
                ADDD    ,X
                STD     ,X++
                PULS    D,PC
c39_rect:
                TFR     U,X
                LBRA    av_sort

;--- c22_entry — $22 表示ページ・処理ページの選択 ----------------------------------
;   相対 3 = 変える所 (bit0 / bit1 / bit2)、相対 4〜6 = P0 / P1 / P2。
;   bit1: $D430 bit5 (処理ページ) ← P1 の bit0 を書く。bit2: VSYNC ($D430 読みの
;   bit2 = 1) を待って $D430 bit6 (表示ページ) ← P2 の bit0 を書く。bit0: $D430
;   bit0 ← P0 の bit0。最後に控えを $D430 へ書く。控えの初期値は 0。
c22_entry:
                LDX     #IO_AV_MISC
                LDB     <AV_MISC
                LDA     ,U
                BITA    #$02
                BEQ     c22_disp
                ANDB    #$DF
                LDA     2,U
                LSRA
                BCC     c22_wa
                ORB     #$20
c22_wa:
                STB     ,X
c22_disp:
                LDA     ,U
                BITA    #$04
                BEQ     c22_b0
c22_vs:
                LDA     ,X
                BITA    #$04
                BEQ     c22_vs
                ANDB    #$BF
                LDA     3,U
                LSRA
                BCC     c22_wd
                ORB     #$40
c22_wd:
                STB     ,X
c22_b0:
                LDA     ,U
                LSRA
                BCC     c22_end
                ANDB    #$FE
                LDA     1,U
                LSRA
                ADCB    #0
c22_end:
                STB     <AV_MISC
                STB     ,X
                RTS

;--- av_view_full — 描画範囲を画面全体にする (リセット時と $39 の枠・塗りの前) ---------
av_view_full:
                CLRA
                CLRB
                STD     <WK_VIEW_X0
                STD     <WK_VIEW_Y0
                LDD     #AV_XMAX
                STD     <WK_VIEW_X1
                LDD     #199
                STD     <WK_VIEW_Y1
                RTS

;--- av_clip — WK_LN_* の直線を描画範囲 WK_VIEW_* で切り取る (動作観察) -------------------
;   範囲の外の端点を線に沿って縁へ寄せる。交点の座標は 0 へ向けて切り捨て、X の縁を先に
;   Y の縁を後に見る。相手の端点は元の値を使う。C=1 なら線は範囲の外 (引かない)。
;   作業域は直線のスクラッチ (直接ページ $D0 の $64〜$6D)。A/B/X/Y/U 破壊。
CL_P0           EQU     $64             ; 4: 元の始点 X,Y
CL_P1           EQU     $68             ; 4: 元の終点 X,Y
CL_M            EQU     $6C             ; 1: 主座標の変位 (0 = X の縁 / 2 = Y の縁)
CL_S            EQU     $6D             ; 1: 商の符号
av_clip:
                LDX     #WK_LN_X0
                LDY     #$D000+CL_P0
                LDB     #4
acl_cp:
                LDU     ,X++                    ; 元の 2 点を控える
                STU     ,Y++
                DECB
                BNE     acl_cp
                LDX     #WK_LN_X0
                LDY     #$D000+CL_P1
                BSR     acl_pt                  ; 始点を元の終点に向けて寄せる
                BCS     acl_r
                LDX     #WK_LN_X1
                LDY     #$D000+CL_P0            ; 終点を元の始点に向けて寄せる
acl_pt:
                CLR     <CL_M                   ; X の縁
                BSR     acl_edge
                BCS     acl_r
                LDB     #2
                STB     <CL_M                   ; Y の縁
                BSR     acl_edge
                BCS     acl_r
                LDD     ,X                      ; Y の縁へ寄せると X も動くので見直す
                CMPD    <WK_VIEW_X0
                BLT     acl_out
                CMPD    <WK_VIEW_X1
                BGT     acl_out
                ANDCC   #$FE                    ; C=0 範囲の中
acl_r:
                RTS
acl_out:
                ORCC    #$01                    ; C=1 範囲の外
                RTS

;--- acl_edge — P (X) の主座標 (変位 CL_M) が範囲の外なら Q (Y) との線に沿って縁へ寄せる ----
;   P.従 += (Q.従 - P.従) × (縁 - P.主) / (Q.主 - P.主)、P.主 = 縁。C=1 なら寄せられない。
acl_edge:
                PSHS    X,Y
                LDB     <CL_M
                LEAX    B,X                     ; X → P.主
                LEAY    B,Y                     ; Y → Q.主
                LDU     #WK_VIEW_X0
                LEAU    B,U                     ; U → 範囲の下限。上限は 4,U
                LDD     ,X
                CMPD    ,U
                BGE     ace_hi
                LDD     ,U                      ; 縁 = 下限
                BRA     ace_t
ace_hi:
                CMPD    4,U
                BLE     ace_in                  ; 範囲の中
                LDD     4,U                     ; 縁 = 上限
ace_t:
                PSHS    A,B                     ; 4,S = 縁
                SUBD    ,X
                PSHS    A,B                     ; 2,S = 縁 - P.主
                LDD     ,Y
                SUBD    ,X
                PSHS    A,B                     ; 0,S = Q.主 - P.主
                LDB     #2
                SUBB    <CL_M
                SUBB    <CL_M                   ; B = 従座標の変位 (+2 = 主が X / -2 = 主が Y)
                LEAU    B,X                     ; U → P.従
                LEAY    B,Y                     ; Y → Q.従 (Y は入口で退避済み)
                LDD     ,Y
                SUBD    ,U                      ; D = Q.従 - P.従
                PSHS    U
                BSR     av_mdiv                 ; D = D × (縁 - P.主) / (Q.主 - P.主)
                PULS    U
                BCS     ace_out
                ADDD    ,U
                STD     ,U                      ; P.従 += 商
                LDD     4,S
                STD     ,X                      ; P.主 = 縁
                LEAS    6,S
ace_in:
                ANDCC   #$FE                    ; C=0
                PULS    X,Y,PC
ace_out:
                LEAS    6,S
                ORCC    #$01                    ; C=1
                PULS    X,Y,PC

;--- av_mdiv — D = D × B1 / C1 (0 へ向けて切り捨て。B1 = 6,S、C1 = 4,S。符号付き 16 ビット) ----
;   B1 と C1 の符号が違う (相手も同じ側の外)、|B1| > |C1|、C1 = 0 (縁に平行) は C=1。
;   Y/U 破壊 (X は保つ)。
av_mdiv:
                LEAS    -6,S                    ; 0,S = 積 (4) / 4,S = |A1| ; 10,S = C1 / 12,S = B1
                STD     4,S
                LDA     4,S
                EORA    10,S
                EORA    12,S
                STA     <CL_S                   ; 商の符号 (bit7)
                LDA     10,S
                EORA    12,S
                BMI     amd_bad
                LEAU    4,S
                BSR     amd_abs                 ; |A1|
                LEAU    10,S
                BSR     amd_abs                 ; |C1|
                BEQ     amd_bad
                LEAU    12,S
                BSR     amd_abs                 ; |B1|
                CMPD    10,S
                BHI     amd_bad
                CLRA                            ; 積 = |A1| × |B1| (32 ビット)
                CLRB
                STD     ,S
                LDA     5,S
                LDB     13,S
                MUL
                STD     2,S                     ; 下位 × 下位
                LDA     4,S
                LDB     13,S
                MUL
                ADDD    1,S
                STD     1,S                     ; 上位 × 下位 (8 ビット上へ)
                BCC     amd_m2
                INC     ,S
amd_m2:
                LDA     5,S
                LDB     12,S
                MUL
                ADDD    1,S
                STD     1,S                     ; 下位 × 上位
                BCC     amd_m3
                INC     ,S
amd_m3:
                LDA     4,S
                LDB     12,S
                MUL
                ADDD    ,S
                STD     ,S                      ; 上位 × 上位 (16 ビット上へ)
                LDY     #16                     ; 商 = 積 / |C1| (引き戻し法。商は 16 ビットに収まる)
amd_dv:
                ASL     3,S
                ROL     2,S
                ROL     1,S
                ROL     ,S
                LDD     ,S
                SUBD    10,S
                BCS     amd_dn
                STD     ,S
                INC     3,S                     ; 商のビット
amd_dn:
                LEAY    -1,Y
                BNE     amd_dv
                LDD     2,S                     ; 商
                TST     <CL_S
                BPL     amd_pos
                NEGA                            ; D = -D
                NEGB
                SBCA    #0
amd_pos:
                ANDCC   #$FE                    ; C=0
amd_x:
                LEAS    6,S
                RTS
amd_bad:
                ORCC    #$01                    ; C=1
                BRA     amd_x
;--- amd_abs — U の指すワード (2 バイト) を絶対値にする。D = 絶対値 (Z = 0 のとき) ---------------------
amd_abs:
                LDD     ,U
                BPL     ama_r
                NEGA                            ; D = -D
                NEGB
                SBCA    #0
                STD     ,U
ama_r:
                RTS

        IFDEF SUBSYS_TB
;--- c_nocode — Type-B の位置に置く ROM が受け付けない命令 ($1B) -------------------
;   相対 0 に $46 (コマンドコードの誤り) を置く。
c_e3c:
                LDA     #$3C                    ; $3C
                FCB     $8C                     ; 次の LDA #$46 を即値として読み飛ばす
c_nocode:
                LDA     #$46
                JMP     cmd_err

;--- gb2b_entry — $1D 矩形読出し 2 (Type-B の位置に置く ROM) -----------------------
;   相対 3〜10 = X0,Y0,X1,Y1 (逆順は並べ替える。X が 0〜319・Y が 0〜199 を外れると $3F)。
;   1 画素を 2 バイト (0000BBBB / RRRRGGGG) で、左上から右へ、上から下へ返す。各色の
;   4 ビットのビット 3〜0 は処理ページ 0 のオフセット $0000・$2000、処理ページ 1 の
;   $0000・$2000 の面にある (320×200 の画面)。応答は相対 3 = 計数、相対 4 から
;   データで、124 バイトごとに分割する (cbg_emit)。
;--- g2b_rect — U の X0,Y0,X1,Y1 を並べ替えて WK_RECT_* へ置き、X が 0〜319・Y が 0〜199 を
;   外れたら呼出元へ戻らず $3F で終える ($1D と Type-B の位置の $1E が使う)
g2b_rect:
                TFR     U,X
                LBSR    av_sort                 ; WK_RECT_X0/Y0 = 小、X1/Y1 = 大
                LDD     <WK_RECT_X1
                CMPD    #AV_XMAX
                BHI     g2b_ec
                LDD     <WK_RECT_Y1
                CMPD    #199
                BHI     g2b_ec
                LDA     <WK_RECT_X0             ; 小さい方が負
                ORA     <WK_RECT_Y0
                BPL     g2b_rr
g2b_ec:
                LEAS    2,S
                JMP     gr_err_coord
g2b_rr:
                RTS
gb2b_entry:
                BSR     g2b_rect
                JSR     cb_resp0        ; 相対 3 = 計数 0 / 応答は $D384 から
                LDD     <WK_RECT_Y0
g2b_y:
                STD     <WK_PX_Y
                LDA     <WK_PX_Y+1
                LDB     #40                     ; 1 行 40 バイト
                MUL
                STD     <G2_ROW
                LDD     <WK_RECT_X0
g2b_x:
                STD     <WK_PX_X
                JSR     d_lsr3
                ADDD    <G2_ROW
                TFR     D,Y                     ; Y = 面の中のバイト位置
                JSR     gpa_mask        ; PBF_MASK = $80 >> (X & 7)
                LDX     #g2b_tab
g2b_l:
                BSR     tb_pg           ; 面の処理ページを選び、D = 面の先頭
                LEAU    D,Y
                LDA     ,U
                ANDA    <PBF_MASK
                ADDA    #$FF                    ; C = 点が立っている
                ROL     <G2_ACC+1
                ROL     <G2_ACC
                CMPX    #g2b_tab+12
                BNE     g2b_l
                LDA     <G2_ACC
                ANDA    #$0F
                JSR     cbg_emit
                LDA     <G2_ACC+1
                JSR     cbg_emit
                LDD     <WK_PX_X
                CMPD    <WK_RECT_X1
                BCC     g2b_ny
                ADDD    #1
                BRA     g2b_x
g2b_ny:
                LDD     <WK_PX_Y
                CMPD    <WK_RECT_Y1
                BCC     g2b_end
                ADDD    #1
                BRA     g2b_y
g2b_end:
                JMP     tb_prst         ; $D430 を控えの値に戻して戻る
g2b_tab:
                FCB     $00,$20,$01,$21         ; B (ビット 3〜0)
                FCB     $40,$60,$41,$61         ; R
                FCB     $80,$A0,$81,$A1         ; G

;--- tb_pg — X の指す g2b_tab の 1 項目の処理ページを選び、D = 面の先頭 (X は 1 進む) ----
tb_pg:
                LDA     >IO_VRAM_GATE           ; VRAM を開く (読出で開く)
                LDB     <AV_MISC
                ANDB    #$DF
                LDA     ,X+
                LSRA
                BCC     tpg_0
                ORB     #$20
tpg_0:
                STB     >IO_AV_MISC
                ASLA
                CLRB
                RTS

;--- tb_rng — D = 面の先頭 → X = 先頭 + ER_SRC、TB_E = 先頭 + ER_END ---------------------
tb_rng:
                LDX     <ER_END
                LEAX    D,X
                STX     <TB_E
                ADDD    <ER_SRC
                TFR     D,X
                RTS

;--- tb_rgb — X の指す B,R,G (各 4 ビット) を D = BBBBRRRR GGGG0000 にする。X 保存 -------
tb_rgb:
                LDA     1,X
                ANDA    #$0F
                LDB     ,X
                ASLB
                ASLB
                ASLB
                ASLB
                PSHS    B
                ORA     ,S+
                LDB     2,X
                ASLB
                ASLB
                ASLB
                ASLB
                RTS

;--- tb_col — 属性 A (bit0-2 = 色、bit3 = 反転) の字の色 TB_FW と地の色 TB_BW を作る ------
;   字の色は色の B・R・G の各 4 ビットを全部 1 か 0 にしたもの、地の色は BC (TB_BG)。
;   反転なら入れ替える (動作観察)。X 破壊。
tb_col:
                PSHS    A
                CLRA
                CLRB
                LSR     ,S
                BCC     tcl_1
                ORA     #$F0
tcl_1:
                LSR     ,S
                BCC     tcl_2
                ORA     #$0F
tcl_2:
                LSR     ,S
                BCC     tcl_3
                ORB     #$F0
tcl_3:
                LDX     <TB_BG
                LSR     ,S+                     ; C = 反転
                BCC     tcl_4
                EXG     D,X
tcl_4:
                STD     <TB_FW
                STX     <TB_BW
                RTS

;--- tb_glyph — U の字形 8 ラインを、X (1 ライン 40 バイトの面の中の位置) から 12 の面へ描く ---
;   A = 属性。字形のビットが 1 の画素は字の色、0 の画素は地の色 (動作観察)。
tb_glyph:
                PSHS    X
                BSR     tb_col
                LDX     #g2b_tab
tgl_pl:
                BSR     tb_pg
                PSHS    X,U                     ; ,S = 表 / 2,S = 字形 / 4,S = 位置
                ADDD    4,S
                TFR     D,Y
                CLRA
                ASL     <TB_BW+1
                ROL     <TB_BW
                BCC     tgl_b
                COMA
tgl_b:
                STA     <TB_M                   ; 地のビットの値
                CLRB
                ASL     <TB_FW+1
                ROL     <TB_FW
                BCC     tgl_f
                COMB
tgl_f:
                EORB    <TB_M
                STB     <TB_D                   ; 字と地で違うビット
                LDB     #8
tgl_rw:
                LDA     ,U+
                ANDA    <TB_D
                EORA    <TB_M
                STA     ,Y
                LEAY    40,Y
                DECB
                BNE     tgl_rw
                PULS    X,U
                CMPX    #g2b_tab+12
                BNE     tgl_pl
                STA     >IO_VRAM_GATE
                PULS    X
tb_prst:
                LDA     <AV_MISC                ; $D430 を控えの値に戻す
                STA     >IO_AV_MISC
                RTS

;--- tb_pix — (WK_PX_X, WK_PX_Y) の 1 画素に色 TB_FW を演算 WK_PB_FN で 12 の面へ置く -------
;   1 ライン 40 バイト。TB_FW は送り出して壊す。A/B/X/U/WK_TMP 破壊。
tb_pix:
                JSR     gr_pixaddr              ; U = Y*80 + X/8、PBF_MASK = 画素のビット
                LDA     <WK_PX_Y+1
                LDB     #40
                MUL
                PSHS    D
                TFR     U,D
                SUBD    ,S
                STD     ,S                      ; ,S = Y*40 + X/8
                LDX     #g2b_tab
tpx_pl:
                JSR     tb_pg
                ADDD    ,S
                TFR     D,U
                CLRA
                ASL     <TB_FW+1
                ROL     <TB_FW
                BCC     tpx_z
                LDA     <PBF_MASK
tpx_z:
                JSR     pb1e_op
                CMPX    #g2b_tab+12
                BNE     tpx_pl
                STA     >IO_VRAM_GATE
                PULS    D
                BRA     tb_prst

;--- tb1e_entry — $1E 矩形書込み 2 (Type-B の位置) ------------------------------------
;   相対 3〜10 = X0,Y0,X1,Y1 (逆順は並べ替える。範囲外は $3F) / 12 = 演算 (0 PSET / 2 OR /
;   3 AND / 4 XOR。1 と 5 以上は $40) / 13 = データの計数 / 14〜 = 1 画素 2 バイト
;   (0000BBBB / RRRRGGGG) を左上から右へ、上から下へ。計数の残りが 2 バイトに満たない画素は 0 (動作観察)。
tb1e_entry:
                JSR     g2b_rect
                LDA     9,U
                CMPA    #4
                BHI     t1e_ef
                CMPA    #1
                BEQ     t1e_ef
                STA     <WK_PB_FN
                JSR     pbs_init
                LDD     <WK_RECT_Y0
t1e_y:
                STD     <WK_PX_Y
                LDD     <WK_RECT_X0
t1e_x:
                STD     <WK_PX_X
                LDB     <PBS_CNT                ; 残りが 2 バイトに満たない画素は 0
                CMPB    #2
                LDD     #0                      ; (LDD は C を変えない)
                BCS     t1e_z
                JSR     pbs_next
                PSHS    A
                JSR     pbs_next
                TFR     A,B
                PULS    A
                ASLB
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
                ASLB
                ROLA
t1e_z:
                STD     <TB_FW
                BSR     tb_pix
                LDD     <WK_PX_X
                CMPD    <WK_RECT_X1
                BCC     t1e_ny
                ADDD    #1
                BRA     t1e_x
t1e_ny:
                LDD     <WK_PX_Y
                CMPD    <WK_RECT_Y1
                BCC     t1e_r
                ADDD    #1
                BRA     t1e_y
t1e_ef:
                JMP     gr_err_fn
t1e_r:
                RTS

        ENDC
        ENDC

;==============================================================================
; ベクタテーブル ($FFF0-$FFFF、参考書籍)
;==============================================================================

                ; 残領域を $00 で埋める
                ZMB     $FFF0-*

                ; 6809 ベクタ ($FFF0-$FFFF)
                FDB     reset_entry             ; $FFF0 (Reserved、慣例で RESET と同じに)
        IFDEF SUBSYS_AV
                ; Type-A / Type-B の位置: 参考書籍のとおり RAM の
                ;   割込みフックテーブル ($D2A8-) を指す (リセットで JMP を敷く)
                FDB     HOOK_TABLE+0    ; $FFF2 SWI3
                FDB     HOOK_TABLE+3    ; $FFF4 SWI2
                FDB     HOOK_TABLE+6    ; $FFF6 FIRQ
                FDB     HOOK_TABLE+9    ; $FFF8 IRQ
                FDB     HOOK_TABLE+12   ; $FFFA SWI
                FDB     HOOK_TABLE+15   ; $FFFC NMI
        ELSE
                FDB     hdlr_SWI3               ; $FFF2 SWI3
                FDB     hdlr_SWI2               ; $FFF4 SWI2
                FDB     hdlr_FIRQ               ; $FFF6 FIRQ
                FDB     hdlr_IRQ                ; $FFF8 IRQ
                FDB     hdlr_SWI                ; $FFFA SWI
                FDB     hdlr_NMI                ; $FFFC NMI
        ENDC
                FDB     reset_entry             ; $FFFE RESET
