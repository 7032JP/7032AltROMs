; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; CMT 低レベル I/O ルーティン
;
; FM-7 CMT ハードウェア:
;   $FD00 write bit1: モーターON (1=ON, 0=OFF)
;   $FD02 read  bit7: テープ信号レベル ($80=HIGH / $00=LOW)
;
; テープ信号エンコーディング (FM-7 標準):
;   bit 0 → 2400 Hz キャリア (短パルス)
;   bit 1 → 1200 Hz キャリア (長パルス)
;   フレーム: スタートビット(0) + 8データビット(LSB先) + 2ストップビット(1)
;
; ブロックフォーマット (バイトストリーム。参考書籍の規定):
;   $FF... ギャップ (モータ ON のとき 10 バイト / OFF のとき 255 バイト)
;   $01    識別子1
;   $3C    識別子2
;   type   ブロックタイプ: $00=ヘッダ, $01=データ, $FF=エンド
;   len    データバイト数 (本 ROM が書くヘッダは BASIC 10 / 機械語 11、エンドは 0)
;   data   len バイト
;   csum   チェックサム (ギャップと識別子を除く全データの 2 進加算 mod 256)
;   $FF×4  エンドギャップ
;==============================================================================


;------------------------------------------------------------------------------
; cmt_delay_x: X * 12サイクルのディレイ (汎用ディレイ)
;   壊すレジスタ: X
;------------------------------------------------------------------------------
cmt_delay_x:
                LEAX    -1,X            ; 5cy
                CMPX    #1              ; 4cy
                BHS     cmt_delay_x     ; 3cy = 12cy/iteration (X が 0 になったら抜ける)
                RTS

;------------------------------------------------------------------------------
; cmt_measure_high: 立ち上がりエッジを待ち、HIGH 半周期の長さを計測する。
;   入力なし。出力: X = HIGH が続いた間のループ反復回数 (= パルス幅の指標)
;           キャリーは不定
;   壊すレジスタ: B, X (A は保つ)
;
; 【待ちに打切りを置かない】信号が変わるまで待ち続け、利用者は中断キーで抜ける。
;   中断キーは内側の計数が一周するごとに見る。
;
; 【途中経過は RAM に置かない】ビット読取の途中経過はレジスタとシステムスタックだけで持ち、
;   低位ページを書かない。
;
; 1200baud FSK では 1 ビットが HIGH 半周期 1 つに対応し、その長さで
; 2400Hz(短) と 1200Hz(長) を区別する。固定サンプル点方式はテープ毎の
; パルス幅のばらつきに弱いため、HIGH→LOW 遷移までの実時間をその場で
; 計数して短/長を動的に閾値判定する (適応パルス幅判定)。
;
;   計数ループ 1 反復 = LEAX(5)+BEQ(3)+LDB(4)+BMI(3) = 15cy。
;   2400Hz の HIGH 半周期は約 28 反復
;   1200Hz の HIGH 半周期は約 51 反復
;   閾値 (CRB_THRESH) はこの中間に置き、短/長を分離する。
;------------------------------------------------------------------------------
cmt_measure_high:
                ; まず立ち上がり: LOW のうちは待つ (上限なし。内側 16bit の計数が一周したら中断キーを見直す)。
                ; 計数ループは 1 反復 15cy を保つ (閾値定数はこの反復コストで決めてある)。ここだけ DP を $FD に置き、
                ; 抜けるときに元へ戻す。
                PSHS    DP              ; 呼出元の直接ページを保存
                LDB     #$FD
                TFR     B,DP            ; DP = $FD (以降 <$02 が $FD02 を指す)
                LDB     #CMH_WAIT_OUTER
                PSHS    B               ; 外周カウンタ = スタック枠 1B (低位ページのワークを使わない)
cmh_wr_outer:
                ; --- 中断キー検査 (読取待ちからの脱出) ---
                ;   $FD04 (PORT_FIRQ) bit1 = 中断キー (0=押下)。立ち上がり待ちに
                ;   入る前の 1 回だけ見るので、計測区間 (cmh_count) の反復コストは
                ;   変わらず、パルス幅判定に影響しない。押下ならモータを止めて
                ;   直接モードへ戻る (無音のテープや存在しないファイルを待ち続けて
                ;   操作を受け付けなくなるのを防ぐ)。
                ;   ここは計数が一周した場合 (HIGH が続いたまま・LOW が続いたまま
                ;   のどちらも) の再入口でもある。HIGH が続いていれば下の立ち上がり
                ;   待ちが即 HIGH を見て cmh_rise_ok へ落ちるので、待ち続ける動作
                ;   そのものは変わらない。中断キーを見る間隔は、内側 16bit の
                ;   一周 (立ち上がり待ちで 18cy × 65535 ≒ 118 万サイクル、計数側で
                ;   15cy × 65536 ≒ 98 万サイクル) ごとになる。
                LDB     >PORT_FIRQ
                BITB    #$02
                LBEQ    brk_stop
                LDX     #$FFFF          ; 内側カウンタ (16bit)
cmh_wait_rise:
                LDB     PORT_CMT
                ANDB    #$80
                BNE     cmh_rise_ok     ; HIGH なら立ち上がり検出 → 計測へ
                LEAX    -1,X            ; 内側カウントダウン
                BNE     cmh_wait_rise   ; 未満了 → 待機継続
                DEC     ,S              ; 内側満了 → 外周の周回数を刻む
                BRA     cmh_wr_outer    ; 一周しても終端とせず、中断キーの検査へ戻る
cmh_rise_ok:
                ; HIGH を確認。ここから LOW になるまで反復回数を数える。
                LDX     #0
cmh_count:
                LEAX    1,X             ; 反復カウント++ (5cy)
                BEQ     cmh_wr_outer    ; 3cy: 計数が一周 → 中断キーの検査を通して計測をやり直す
                LDB     <$02            ; 4cy: 1 回目読み取り (DP=$FD → $FD02)
                BMI     cmh_count       ; 3cy: HIGH → 継続 (計 15cy = 判定閾値が前提とする値)
                ; 3 回連続 LOW を真の立ち下がりとしてグリッチを吸収する。
                LDB     PORT_CMT        ; 2 回目確認
                ANDB    #$80
                BNE     cmh_count       ; 実は HIGH (グリッチ) → 継続
                LDB     PORT_CMT        ; 3 回目確認
                ANDB    #$80
                BNE     cmh_count       ; 実は HIGH (グリッチ) → 継続
                ; --- クロック周波数の切替への追従 ---
                ;   計数は CPU のサイクルで進むので、動作クロックが 1.2 MHz (参考書籍:
                ;   $FD00 bit0 = 0) のときは同じ半周期でも反復数が 2 MHz の 0.614 倍
                ;   (1.2288 / 2.0) にしか届かない。ここで 1.625 倍 (= 1 + 1/2 + 1/8)
                ;   に伸ばして 2 MHz の目盛に揃え、後段の閾値 (CRB_*) を共通にする
                ;   (クロック周波数に応じた切替。参考書籍)。
                LDB     <$00            ; $FD00 bit0 = 0: 1.2 MHz / 1: 2 MHz (DP = $FD)
                LSRB
                BCS     cmh_fin         ; 2 MHz → そのまま
                STA     ,S              ; A を外周カウンタの枠へ控える (この先は枠を数えに使わない)
                TFR     X,D
                LSRA
                RORB                    ; D = X / 2
                LEAX    D,X             ; X = 1.5 X
                LSRA
                RORB
                LSRA
                RORB                    ; D = X / 8
                LEAX    D,X             ; X = 1.625 X
                PULS    A,DP,PC         ; A を戻して復帰
cmh_fin:
                PULS    B,DP,PC         ; 3 回連続 LOW = 真の立ち下がり。X = 半周期長指標

; スタートビットを「短(2400Hz)」と認める上限 (cmt_measure_high の反復数)。
;   計数ループ 1 反復 ≈ 15cy。短(2400Hz,~416cy)≈28, 長(1200Hz,~768cy)≈51。
;   パルス幅がばらついても分離できるよう、両者の中間に置く。
CRB_THRESH      EQU     40

; 適応閾値のクランプ範囲。スタートビット幅 W から W×1.5 を作り、この範囲に
; 収める。短(2400Hz)≈28・長(1200Hz)≈51 を分離しつつ、テープ毎の偏りに追従。
;   下限 38: 短側の幅が広がっても短を短と判定できる値。
;   上限 44: 長半周期が ~45 反復まで縮んでも長側へ含める値。
CRB_THRESH_LO   EQU     38
CRB_THRESH_HI   EQU     44

; スタートビットとして認める短 HIGH 半周期の下限 (cmt_measure_high の反復数)。
;   ブロック間のリーダー/無音領域には幅の極めて狭い HIGH のヒゲ (数十サイクル
;   = 反復数 1〜8 程度) が乗ることがある。これを短(2400Hz)スタート
;   ビットと誤認すると無音域からフレーム化を始めてしまい、本来のリーダーに
;   一度も同期できず読み取りが総崩れになる。正規の短半周期(~416cy)は反復数
;   16 以上になるため、明らかに細すぎる HIGH (< CRB_MIN) はスタートビット
;   候補から除外し、確かなキャリアが現れるまで待つ。
;   下限 12: 正規の短 (16 以上) と ヒゲ (~8 以下) を分離する中間値。
CRB_MIN         EQU     12

; cmt_measure_high 立ち上がり待ちの外周カウンタ初期値。
;   スタック枠 1 バイトを確保する初期値。
CMH_WAIT_OUTER  EQU     16

;------------------------------------------------------------------------------
; cmt_read_byte: テープから1バイト読み取り (半周期の幅で分類)
;   出力: A = 読み取ったバイト, キャリー = 0。
;   壊すレジスタ: A, B, X, Y
;
; 【途中経過は RAM に置かない】適応閾値はシステムスタックの 2 バイト枠、
;   組立中のバイトは A に持つ。テープ読取中に低位ページを 1 バイトも書かない
;   ための規約 (根拠は cmt_measure_high の頭書き)。
;
; アルゴリズム:
;   スタートビットの短半周期の長さ W を測り、W×1.5 を閾値として HIGH 半周期の幅で短/長を分類する。
;------------------------------------------------------------------------------
cmt_read_byte:
                ; 各ビットセル = HIGH 半周期 1 個 (短=2400Hz='0' / 長=1200Hz='1') +
                ; LOW 半周期 1 個。フレーム = スタートビット(0) + 8データ(LSB先) +
                ; ストップビット(1)×2。
                ;
                ; ビット判定は cmt_measure_high で HIGH 半周期長を計測して行う。
                ; スタートビットは CRB_THRESH で、データビットはスタートビットの幅から
                ; 求めた適応閾値 (スタックに置く) で短/長を分ける。cmt_measure_high は HIGH を
                ; 1 個ぶん消費して立ち下がり(LOW)で復帰するため、各ビットの境界に
                ; ぴったり揃い、バイト間・ストップビット境界でも位相がずれない。
                ;
                ; スタートビット待ち: 短 HIGH(='0') が来るまで読み飛ばす。
crb_start:
                BSR    cmt_measure_high ; X = 直近 HIGH 半周期長 (反復数)
                CMPX    #CRB_THRESH
                BHS     crb_start        ; 長 HIGH → スタートビットでない → 測り直し
                CMPX    #CRB_MIN
                BLO     crb_start        ; 細すぎる HIGH (リーダー/無音のヒゲ) → 無視
                ; 短 HIGH = スタートビット。立ち下がりで復帰済み (LOW)。
                ; 適応閾値を算出: スタートビット(=短='0')の計測幅 W から
                ; 閾値 = W × 1.5 を作る (長='1' は短の約2倍幅なので W と 2W の中点)。
                ; テープ毎にパルス幅分布が偏っても短/長の境界に追従できる。
                ; 安全範囲 [CRB_THRESH_LO, CRB_THRESH_HI] にクランプする。
                ; (X = 受理したスタートビット幅。B を作業に使う)
                TFR     X,D              ; D = W (A:B, W<256 なので B=幅下位, A=0)
                LSRB                     ; B = W/2 (A は 0 のまま)
                ABX                      ; X = W + W/2 = W*1.5 (X は元々 W)
                ; X = W + floor(W/2)。安全範囲にクランプ。
                JSR     cmt_thresh_clamp
                BRA     cdt_store

;------------------------------------------------------------------------------
; SWAP <変数>, <変数> (参考書籍)
;
;   参考書籍の規定どおり、整数・単精度・倍精度・文字のいずれの型でも交換できるが、
;   2 つの変数の型は同じでなければならず、一致しないときは Type Mismatch とする。
;   交換は値の記録域どうしをそのまま入れ換える。文字変数は記述子 (長さ+実体
;   ポインタ) を入れ換えるので、実体の複写も確保も起きない。
;
;   第 1 変数のアドレスは let_tgt に置く (vars_make_room が追従させる)。
;
;------------------------------------------------------------------------------
impl_swap:
                JSR     get_var_ref_chk         ; U = 第1変数の値アドレス
                STU     let_tgt
                LDA     VARTYP
                PSHS    A                       ; 第1変数の型
                JSR     skip_comma
                LBCC    errc_2                  ; ',' が無い → Syntax Error
                JSR     get_var_ref_chk         ; U = 第2変数の値アドレス
                LDA     VARTYP
                CMPA    ,S+
                LBNE    err_tmm                 ; 型が違う → Type Mismatch
                PSHS    X                       ; 本文ポインタ (vn_valsize が壊す)
                JSR     vn_valsize              ; B = 値のバイト数 (型別)
                TFR     U,X                     ; X = 第2変数
                LDU     let_tgt                 ; U = 第1変数 (動いていれば追従済)
                JSR     swap_bytes
                PULS    X,PC
cdt_store:
                PSHS    X                ; 適応閾値 = スタック枠 2B (低位ページを使わない)
                LDA     #$80             ; A = 結果ビットの組立用。bit7 の 1 は番兵で、
                                         ;   8 回の回転で C へ押し出されたら 8 ビット揃い
crb_bit:
                LBSR    cmt_measure_high ; このビットの HIGH 半周期長を計測 (A は保存)
                ; 短(< 適応閾値) → C=1, 長(>= 適応閾値) → C=0 (ビットの反転が C に出る)
                CMPX    ,S               ; X < 閾値 で C=1 (短=0), X>=閾値 で C=0 (長=1)
                RORA                     ; 反転したビット → A bit7 (LSB 先収集)、bit0 → C
                BCC     crb_bit          ; 番兵が C に出るまで (8 ビット) 継続
                COMA                     ; 8 ビットまとめて反転して結果バイトにする
                ; 8ビット収集完了 (結果バイトは A にある)
                ; ストップビット×2 (長 HIGH) を消費し、次バイトの境界へ揃える。
                LBSR    cmt_measure_high ; ストップビット1 を消費 (立ち下がりで復帰)
                LBSR    cmt_measure_high ; ストップビット2 を消費 (立ち下がりで復帰)
                ANDCC   #$FE             ; キャリークリア (成功)
                PULS    X,PC             ; 閾値枠を捨てて復帰 (X は破壊対象)

;------------------------------------------------------------------------------
; cmt_read_block: テープから1ブロック読み取り
;   リーダー ($FF×5以上) + 同期 $01 $3C を検出し、
;   ブロックタイプ/長さ/データ/チェックサムを読み取る
;
;   入力: U = データ格納先
;   出力:
;     cmt_blk_type = ブロックタイプ (00=ヘッダ, 01=データ, FF=エンド)
;     cmt_blk_len  = データ長
;     U            = 書き込んだバイト数だけ進む
;   戻り値:
;     キャリー = 0 → 成功
;     キャリー = 1 → チェックサムの誤り ($02D5 = 1)。テープの終わりは検出しない
;                    (cmt_read_byte は信号の来ない間は中断キーまで待ち続ける)。
;   壊すレジスタ: A, B, X, Y, U (データ書込後に進む)
;   格納の上限: ヘッダ探索は cmt_read_hdr (20 byte まで) から入る。通常の入口は
;   上限なし (255 byte)。上限を超えた分は読み飛ばし、チェックサムには加える。
;------------------------------------------------------------------------------
; cmt_hdr_show — 読み終えたヘッダブロックの中身を 1 行で画面に出す。
;   出力は FILES の 1 行 (if_pe_body) と同じ `ファイル名 ファイル形式 属性` の並び。
;   呼び出しはブロックの読取の途中ではなく、ヘッダブロックを読み終えた後に
;   行う (読取のパルス幅の計測に割り込ませない)。
;   ヘッダ緩衝の +10 以降は照合に使わないので、+8 / +9 を if_pe_body が見る
;   位置 (+11 / +12) へ写してから渡す。
;   13 文字は impl_putchar の捕捉モード (pc_capt / pc_capptr) でステージングへ写し取り、
;   サブシステムのコンソール出力 (コマンド $03) で 1 回に渡す。改行は disk_crlf に任せる。
;   捕捉モードは文の途中 (STR$ / PRINT USING) でしか立たないので、文として
;   実行される LOAD / LOADM / FILES の入口では pc_capt = 0 である (INC で 1)。
;   破壊: A, B, X, Y
;------------------------------------------------------------------------------
cmt_hdr_show:
                LDD     cmt_hdr_buf+8   ; ファイル形式 (+8) と属性 (+9) をワード (2 バイト) で
                STD     cmt_hdr_buf+11
                LDY     #cmt_hdr_buf
                LDX     #SUBCMD_CMD     ; ステージングの相対 2
                LDD     #$0300+13       ; 相対 2 = $03 (コンソール出力) / 相対 3 = 13 文字
                STD     ,X++            ; X = 相対 4 (文字列の先頭)
                STX     pc_capptr       ; 捕捉の書込み先
                INC     pc_capt         ; 捕捉モードへ (0 → 1)
                JSR     if_pe_body      ; ファイル名 + ファイル形式 + 属性 (13 文字を捕捉)
                CLR     pc_capt         ; 捕捉モードを解く
                JSR     impl_sub_send_fast ; $03 を 1 回で送る (即送入口)
                JMP     disk_crlf       ; 改行 (末尾呼び)
;------------------------------------------------------------------------------
; cmt_read_hdr: ヘッダ探索用の入口。格納を cmt_hdr_buf の大きさ (20 byte) までに
;   限り、それを超える分は緩衝へ書かずに読み飛ばす (チェックサムには加える)。
;   ヘッダ探索はブロックの種別を確かめる前に読み込むので、別ファイルの本文
;   ブロック (最大 255 byte) に出会っても、緩衝を溢れて後続のワークを潰さないようにする。
;   ヘッダブロックのデータ (本 ROM が書くのは 10 / 11 byte) は欠けずに入る。
;   戻り値は cmt_read_block と同じ。
cmt_read_hdr:
                LDB     #20             ; 格納の上限 = cmt_hdr_buf の大きさ
                BRA     crb_begin
cmt_read_block:
                LDB     #$FF            ; 上限なし (データ長は最大 255)
crb_begin:
                PSHS    B               ; ,S = まだ格納できる byte 数
                ; パルス幅の計測はタイマ割込のジッタに敏感なため、
                ; ブロック読取り中は周期タイマを止める (出口で再開)。
                JSR     time_pause
                ; リーダー検出: $FF を5バイト連続で読む
                ; 注: cmt_read_byte はパルス幅計測の中で B を破壊するため、
                ;     連続カウンタは RAM (cmt_lead_cnt) に保持する。
crb_leader:
                LDA     #5
                STA     cmt_lead_cnt    ; 連続 $FF カウンタ初期化
crb_lead_lp:
                BSR     crb_getb   ; 1バイト読む (B 破壊)
                CMPA    #$FF
                BNE     crb_leader      ; $FF 以外 → カウントリセット
                DEC     cmt_lead_cnt
                BNE     crb_lead_lp     ; 5個連続で OK
                ; 残りの $FF をスキップして $01 同期バイトを探す
crb_skip_ff:
                BSR     crb_getb
                CMPA    #$FF
                BEQ     crb_skip_ff     ; 続く $FF はスキップ
                ; $01 か確認
                CMPA    #$01
                BNE     crb_leader      ; $01 以外 → やり直し
                ; $3C か確認
                BSR     crb_getb
                CMPA    #$3C
                BNE     crb_leader      ; $3C 以外 → やり直し
                ; ブロックタイプ読み取り
                BSR     crb_getb
                STA     cmt_blk_type
                ; 偽 sync 弾き (LOADM の 2 個目以降ヘッダ探索限定)。
                ;   cmt_strict_hdr が立っているときだけ、ブロックタイプが範囲外なら
                ;   リーダー再探索へ戻す。
                TST     cmt_strict_hdr
                BEQ     crb_type_ok
                CMPA    #2
                BLO     crb_type_ok     ; 00 / 01
                CMPA    #$FF
                BNE     crb_leader      ; FF 以外 = 偽 sync → リーダー再探索
crb_type_ok:
                ; データ長読み取り
                BSR     crb_getb
                STA     cmt_blk_len
                ; チェックサム計算開始: type + len
                LDB     cmt_blk_type
                ADDB    cmt_blk_len
                STB     cmt_chk_sum
                ; データバイト読み取り。
                ; カウンタは cmt_lead_cnt (リーダー検出後は空き) に置き、
                ; cmt_blk_len は破壊しない。
                LDA     cmt_blk_len
                BEQ     crb_no_data     ; 長さ0 → データなし
                STA     cmt_lead_cnt    ; 残りバイト数カウンタ
crb_data_lp:
                BSR     crb_getb   ; A = 読んだバイト
                TST     ,S              ; 格納の上限に達したら書かずに読み飛ばす
                BEQ     crb_nostore
                DEC     ,S
                STA     ,U+             ; バッファに格納
crb_nostore:
                ; チェックサム更新: cmt_chk_sum += A
                ADDA    cmt_chk_sum
                STA     cmt_chk_sum
                DEC     cmt_lead_cnt
                BNE     crb_data_lp
crb_no_data:
                ; チェックサムバイト検証
                BSR     crb_getb
                CMPA    cmt_chk_sum
                BNE     crb_cksum_err
                CLR     >CAS0_ERR       ; CAS0 エラー種別 = エラーなし
                JSR     time_resume     ; タイマ再開 (シャドウの状態へ)
                ANDCC   #$FE            ; 成功
                PULS    A,PC            ; 格納上限の枠を捨てて復帰 (A は破壊対象)
; crb_getb — テープから 1 バイト読む (cmt_read_byte への末尾分岐)。
;   ブロック読取りの各所の共通の入口。
crb_getb:
                LBRA    cmt_read_byte   ; (末尾分岐。cmt_read_byte はキャリー 0 で戻る)

crb_cksum_err:
                LDA     #1              ; CAS0 エラー種別 = チェックサムエラー
                STA     >CAS0_ERR       ;   (参考書籍)
crb_fail:
                JSR     time_resume     ; タイマ再開 (シャドウの状態へ。
                                        ;   CC は変化するので直後に ORCC で立て直す)
                ORCC    #$01            ; キャリーセット = エラー
                PULS    A,PC            ; 格納上限の枠を捨てて復帰 (CC は保つ)

; skip_spc: X が指す位置から空白 (SPACE $20) を読み飛ばす。
;   引数解析の前に空白を除去する。
;   入力/出力: X = 最初の非空白を指す。A と CC を壊す (B/Y/U は保つ)。
;   実体は式評価器の skip_sp_x (7t_expr.inc) で、ここはその別名 (EQU)。
skip_spc        EQU     skip_sp_x

;------------------------------------------------------------------------------
; impl_load — LOAD ["ファイル名"] (LOADM も判定する)
;   ヘッダブロックを探してファイル名を照合し、本文を行ごとに原文のまま挿入する。
;------------------------------------------------------------------------------
impl_load:
                ; オーディオカセット系コマンドは演奏を中断する (参考書籍)
                JSR     play_stop
                CLR     cmt_strict_hdr          ; 偽 sync 弾きは既定 OFF (LOADM で立てる)
                ; LOADM 判定: LOAD トークン直後に 'M' があれば機械語ロード。
                LDA     ,X
                CMPA    #'M'
                LBEQ    impl_loadm
                ; LOAD? : テープ上のチェックサムの照合 (参考書籍 LOAD?)。
                ; '?' は行変換器が PRINT の省略形のトークンにするので、その値で見る
                CMPA    #TOK_QMARK
                LBEQ    impl_loadq
                JSR     skip_spc                ; LOAD と引数の間の空白を除去
                ; ファイル名解析 + ",R" (ロード後自動実行) 解析を一括で行う。
                ; impl_load_fname_r は impl_parse_fname を呼んだ後、続く ",R" を解析して
                ; mlm_run (0=無/1=有) に記録する。
                JSR     impl_load_fname_r       ; ファイル名 + ,R 解析 → cmt_filename/dl_device/mlm_run
                ; 装置分岐: dl_device bit7=1 (ディスク) ならディスク経路へ。
                ; カセット経路 (bit7=0) は以降このままテープ経路。ディスク経路は
                ; disk_load_body 内で展開・再リンクまで完結して RTS する。
                LDA     dl_device
                LBMI    disk_load_body
                ; ディスパッチャ復帰用 X (引数末尾) を先に積む。ブロック読取
                ; (cmt_read_block) が X を壊すため、以降の全経路はここで積んだ
                ; 値を出口 (il_legacy の末尾 / ilm_done) で戻す。誤りの出口は積んだ値を
                ; 誤り処理のスタックの巻戻しに委ねる。
                PSHS    X
                ; モーター ON
                JSR     cmt_motor_on    ; モータ ON (控えも更新)
                ; ヘッダブロック待ちループ
il_header:
                LDU     #cmt_hdr_buf            ; ヘッダデータ一時バッファ (20B, RAM)
                JSR     cmt_read_hdr            ; 格納は 20B まで (ヘッダでない本文
                                                ;   ブロックで後続のワークを潰さない)
                LBCS    isk_ioerr              ; チェックサムの誤り → モータ OFF +
                                               ;   Device I/O Error (53。LOAD? と同じ扱い)
                LDA     cmt_blk_type
                BNE     il_header               ; type≠0 → ヘッダブロックでない → 再試行
                ; ヘッダブロック取得成功
                ; ファイル名照合 (cmt_filename が空なら任意 = 無条件受け入れ)
                JSR     cmt_name_chk
                BCS     il_header               ; 不一致 → 別のブロックを探す
                JSR     cmt_hdr_show            ; ヘッダの中身 (名前 / ファイル形式 / 属性) を表示
                ; ヘッダのファイル形式 (+8) と属性 (+9) で受理する形式を決める。
                ;   $00 $FF = BASIC ソースのアスキー形式、$00 $FE = 同じく保護 → 行取込へ。
                ;   それ以外 (機械語ファイル $02、バイナリ形式 $00 $00 ほか) は
                ;   Bad Data In File (58)。機械語ファイルは LOADM で読む。
                LDD     cmt_hdr_buf+8
                STD     >FIL_TYPE       ; ファイルタイプ / アスキーフラグ
                                        ;   (参考書籍)
                COMB                            ; 属性 $FF→$00 / $FE→$01 (保護)
                CMPD    #$0001                  ; 形式 $00 かつ属性 $FF/$FE だけ受理
                LBHI    bdf_err                 ; モータ停止 + Bad Data In File
                STB     prot_flag               ; 保護状態を本文へ引き継ぐ
;------------------------------------------------------------------------------
; il_legacy: BASIC プログラムのテープ LOAD 本体。
;
;   テープの本文は連続バイト列を 1 データブロック (最大255バイト) ずつに詰めた形で
;   あり (参考書籍)、1 ブロックに複数行が入り、1 行がブロック境界を跨ぐこともある。
;   本実装は il_get_byte (type=1 ブロックを跨いで論理 1 バイトずつ供給し、
;   type≠1 でキャリー) でバイト列を読み、共通の行取込 (stream_lines) で原文のまま
;   挿入する。
;     il_legacy = アスキー形式 (ヘッダの形式 $00 / 属性 $FF か保護の $FE)
;------------------------------------------------------------------------------
il_legacy:
                BSR     il_stream_prep          ; 緩衝初期化 + 供給 = テープ (MERGE と共用)
                BSR     prog_clear_new          ; プログラムを空に (NEW 相当)
                BSR     stream_lines            ; CR 区切りで各行を原文のまま挿入
                JSR     cmt_motor_off           ; モーター OFF
                JSR     il_ret_run              ; ,R 指定なら自動 RUN (戻らない)
                PULS    X,PC ; ディスパッチャ復帰用 X

;------------------------------------------------------------------------------
; stream_lines — 論理バイトストリームを CR($0D) 区切りで 1 行ずつ取り出し、
;   行頭が数字の行のみ impl_insert_line で原文のまま挿入する共通行取込。
;   テープ (il_get_byte) とディスク (disk_get_byte) の両 LOAD、および MERGE が
;   共用する。
;   入力: U = 論理バイト供給ルーチン (A=バイト, C=1 終端。U を保存する規約)
;   終端: 供給の尽き (C=1) または終端記号 $1A。LF($0A) は無視。1 行がブロック/
;   セクタ境界を跨いでもよい (供給側が吸収)。プログラム消去 (NEW) は行わない
;   (呼出側が用途に応じて事前処理: LOAD は全置換、MERGE は併合)。
;   行組立の書込ポインタは供給側が X/Y を壊すため RAM (mlm_offset) に保持する。
;   破壊: A,B,X,mlm_offset (U は保存)
;------------------------------------------------------------------------------
sl_eol:
                BSR     sl_line                 ; 行確定 (数字始まりのみ挿入)
                ; fallthrough: 次の行頭へ
stream_lines:
sl_newline:
                LDX     #str_work_buf           ; 行組立バッファ先頭
                STX     mlm_offset              ; 行書込ポインタ (RAM 保持)
sl_getc:
                JSR     ,U                      ; A=論理バイト, CS=データ終端
                BCS     sl_eof
                CMPA    #$1A                    ; 終端記号 (SAVE が本文の末尾に置く)
                BEQ     sl_eof
                CMPA    #$0D                    ; CR → 行確定
                BEQ     sl_eol
                CMPA    #$0A                    ; LF → 無視
                BEQ     sl_getc
                ; 通常文字 → 行バッファへ追記 (オーバフロー保険)
                LDX     mlm_offset
                CMPX    #str_work_buf+255       ; 論理行は最大 255 文字
                BHS     sl_getc                 ; 上限超過分は捨てる
                STA     ,X+
                STX     mlm_offset
                BRA     sl_getc
sl_eof:
                ; データ終端。CR 無しで残った最終行があれば挿入して終了
                ; (残りが無ければ sl_line 内の空行判定がそのまま何もしない。
                ;  sl_line の RTS が呼出元への復帰を兼ねる = 末尾呼び)。
sl_line:
                ; 行確定: $00 終端 → 数字始まりなら挿入
                LDX     mlm_offset
                CLR     ,X
                LDA     str_work_buf
                BEQ     sl_ret                  ; 空行はスキップ
                CMPA    #'0'
                BLO     sl_ret
                CMPA    #'9'
                BHI     sl_ret
                ; 挿入。impl_insert_line が使う作業は mlm_offset だけで、
                ; ストリーム状態 (mlm_bptr/mlm_brem/dl_*/da_bptr) とは非干渉。
                ; mlm_offset は行頭 (sl_newline) で毎行設定し直すので安全。
                LDX     #str_work_buf
                JMP     impl_insert_line
sl_ret:
                RTS



;------------------------------------------------------------------------------
; prog_clear_new — プログラムを NEW 直後と同じ状態へ戻す。
;   本文を空に (TXTTAB へ [00 00]、VARTAB = 直後) したうえで、NEW と同じ 2 つの
;   後始末を行う。
;     vars_clear_all … 全変数・配列・文字列領域と DEF FN 定義を空に (既定型宣言も既定へ)
;     cont_clear     … 本文が入れ替わるので CONT の再開位置は無効
;   LOAD は読み込む行が 0 本でも変数と DEF FN 定義を消す (参考書籍)。
;   テープ/ディスクの LOAD 本体 (il_legacy / dal_new) と NEW (impl_new) が共用。
;   呼出側は U に本文の供給ルーチンを載せたまま呼ぶ。ここから呼ぶ 2 本はいずれも
;   U と Y を保つ (vars_clear_all は str_maybe_gc → str_gc も含めて U/Y/X を積んで
;   戻す。cont_clear は何も使わない)。
;   破壊: D, X (U, Y は保つ)
;------------------------------------------------------------------------------
prog_clear_new:
                LDX     TXTTAB
                CLR     ,X
                CLR     1,X
                LEAX    2,X
                STX     VARTAB
                JSR     vars_clear_all  ; 変数・配列・文字列領域を空に
                JMP     cont_clear      ; 本文が入れ替わった後は CONT で再開できない

;------------------------------------------------------------------------------
; il_stream_prep — テープ本文ストリームの初期化 (LOAD と MERGE が共用)。
;   復帰時は U = il_get_byte (続けて行取込に使える)。
;------------------------------------------------------------------------------
il_stream_prep:
                CLR     mlm_brem                ; バイトストリームの緩衝を空に
                LDU     #il_get_byte            ; 本文供給 = テープ
                RTS

;------------------------------------------------------------------------------
; il_get_byte: BASIC テキストのデータブロック列から論理 1 バイトを取り出す。
;   mlm_get_byte と同じ規約 (A=バイト, C=1 終端。type≠1 で終端) だが、ブロック
;   緩衝に DA_SECBUF を使う点だけが違う。
;   データブロックのチェックサムの誤りは Device I/O Error (53) で中止する (戻らない)。
;   出力: A = バイト, C=0 成功 / C=1 終端。破壊: A,B,X,Y (U は保存)
;------------------------------------------------------------------------------
il_get_byte:
                PSHS    U                       ; 供給ベクタ (,U 呼出) を保全
                LDA     mlm_brem
                BNE     igb_serve               ; 緩衝に残りあり
igb_fill:
                LDU     #DA_SECBUF
                JSR     cmt_read_block
                LBCS    isk_ioerr               ; チェックサムの誤り → モータ OFF +
                                                ;   Device I/O Error (53)
                LDA     cmt_blk_type
                CMPA    #1
                BNE     igb_eof                 ; type≠1 → データ終端
                LDA     cmt_blk_len
                BEQ     igb_fill                ; 長さ0 → 次ブロック
                STA     mlm_brem
                LDD     #DA_SECBUF
                STD     mlm_bptr
igb_serve:
                LDU     mlm_bptr
                LDA     ,U+
                STU     mlm_bptr
                DEC     mlm_brem
                ANDCC   #$FE                    ; C=0 成功
                PULS    U,PC
igb_eof:
                ORCC    #$01                    ; C=1 終端
                PULS    U,PC

;------------------------------------------------------------------------------
; impl_loadm: LOADM ["ファイル名"][,アドレス][,R]
;   機械語 (file-type=$02) ファイルをテープから読み、所定アドレスへ展開する。
;   入力: X = LOAD トークン直後の 'M' を指す
;
;   参考書籍に記載の機械語テープ形式:
;     ヘッダブロック (type=$00): name[8], fileType[1]=$02, ...
;     データブロック (type=$01) の本文を論理的に連結すると以下のセグメント列:
;       [$00][lenHi][lenLo][addrHi][addrLo] (len バイトのデータ) …繰り返し
;       [$FF][$00][$00][execHi][execLo]  (終端 + 実行開始アドレス)
;   セグメントの埋め込みアドレスを基準に展開する。",&H値" 指定時は、その値を
;   オフセットとして全セグメントに加算する。
;   ",R" 指定時はロード後に実行開始アドレス (+オフセット) へ JSR する。
;   ",R" 無しのときは BASIC へ復帰し、後続 EXEC 等が実行される。
;   X はディスパッチャ復帰用に引数末尾へ進めて返す。
;------------------------------------------------------------------------------
impl_loadm:
                JSR     play_stop               ; カセット系コマンドは演奏を中断する (参考書籍)
                JSR     skip1_sp_x
                JSR     impl_parse_fname        ; ファイル名解析 → cmt_filename, X 前進
                CLRA
                CLRB
                STD     cmt_load_ptr
                STD     mlm_offset              ; 相対オフセット = 0 (既定)
                STD     mlm_exec                ; 実行開始アドレス = 未設定 (0)
                ; 残りの作業変数初期化 (mlm_first_addr / ilm_have_addr / mlm_run)。
;------------------------------------------------------------------------------
; ilm_init_tail — LOADM の作業変数初期化の後半。
;   D は呼出側で使わないので破壊可。
;------------------------------------------------------------------------------
ilm_init_tail:
                LDD     #$FFFF
                STD     mlm_first_addr          ; 先頭ロードアドレス = 未設定
                CLR     ilm_have_addr           ; アドレス指定有無フラグ = 0
                CLR     mlm_run                 ; ,R フラグ = 0
                ; オプション引数列 ",アドレス" および ",R" を解析
                JSR     skip_spc                ; 引数記号前の空白を除去
                BRA     ilm_opts                ; 引数解析へ

;------------------------------------------------------------------------------
; dl_seq_init — 開始クラスタからの逐次読取を初期化する。ASCII 読込と機械語
;   読込の共通の入口。破壊: A,B (D)。
;------------------------------------------------------------------------------
dl_seq_init:
                LDA     dl_start_clu
                STA     dl_cluster              ; 現クラスタ = 開始クラスタ
                CLR     dl_nsec                 ; 残セクタ0 → 最初の要求で新クラスタを設定
                CLR     dl_clu_adv              ; 初回は開始クラスタをそのまま使う
                CLR     dl_clu_cnt              ; 鎖周回ガード初期化
                LDD     #DA_SECBUF_END
                STD     da_bptr                 ; バッファ空 (末尾) → 最初の要求で読取
                RTS

ilm_opts:
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     ilm_args_done
                JSR     skip1_sp_x
                ; 実行指定 R は 2 個目のカンマの後で受ける (参考書籍)。
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BEQ     ilm_arg2                ; ",,..." → アドレス省略, 第2引数位置へ
                TSTA
                BEQ     ilm_args_done
                CMPA    #':'
                BEQ     ilm_args_done
                ; アドレス式 (ここに来る 'R' 等は変数=アドレス式として評価する)
                JSR     eval_to_addr   ; アドレスの規約 (-65536..65535)。基数付き定数は
                                       ;   符号なしなので &HE000 も 16 ビットのアドレスに収まる
                STD     cmt_load_ptr
                STD     mlm_user_addr           ; 指定アドレスを保持
                LDA     #1
                STA     ilm_have_addr
                ; 続く ",R" を確認
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     ilm_args_done
                LEAX    1,X
ilm_arg2:
                ; 2個目の引数位置: ',' のあとが 'R' なら実行指定
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     ilm_arg2_chk
                LEAX    1,X
ilm_arg2_chk:
                JSR     skip_spc                ; 2個目の ',' 後の空白を除去
                LDA     ,X
                CMPA    #'R'
                BEQ     ilm_set_run
                CMPA    #'r'
                BNE     ilm_args_done
ilm_set_run:
                LEAX    1,X                     ; 'R' を読み飛ばす
                LDA     #1
                STA     mlm_run
ilm_args_done:
                PSHS    X                       ; ディスパッチャ復帰用 X を退避
                ; --- 装置分岐 ---
                ; ファイル名解析で確定した装置 (dl_device) を判定する。ディスク指定
                ; (DL_DISK0/DL_DISK1 = bit7=1) なら、テープ待ちに入らずディスク経路
                ; (disk_loadm_body) へ分岐する。カセット (dl_device=$00) はそのまま
                ; 以下のテープ経路。オプション (,アドレス / ,,R) 解析は共通で済ませて
                ; あり、mlm_user_addr/ilm_have_addr/mlm_run/mlm_offset はディスク経路でも
                ; 尊重される。disk_loadm_body (7t_diskboot.inc) はディスク基盤
                ; (disk_dir_find/disk_read_sector) で機械語セグメント列を解析し、展開後は
                ; 本ルーチンの共通尾部 (ilm_done: オフセット/実行アドレス/,R 実行/実行アドレスワーク複写/
                ; モータ停止) へ合流する。退避済みの X も ilm_done が戻す。
                LDA     dl_device
                STA     mlm_dev                 ; 解析中も生き残る控え (dl_device は
                                                ;   テープ読取のチェックサムに潰される)
                LBMI    disk_loadm_body         ; bit7=1 (ディスク) → 別経路
                ; 偽 sync 弾きを有効化 (LOADM 実行中のみ)。
                LDA     #1
                STA     cmt_strict_hdr
                ; モーター ON
                JSR     cmt_motor_on    ; モータ ON (控えも更新)
                ; ヘッダブロック待ち (ファイル名照合 + file-type=$02 確認)
ilm_header:
                LDU     #cmt_hdr_buf
                JSR     cmt_read_hdr            ; 格納は 20B まで (il_header と同じ)
                BCS     ilmh_ioerr              ; チェックサムの誤り → 53 (LOAD のヘッダと同じ)
                LDA     cmt_blk_type
                BNE     ilm_header              ; type≠0 → ヘッダでない
                ; ファイル名照合 (大文字小文字をそのまま比較。指定なしは無条件 =
                ; 次の ML を採用)
                JSR     cmt_name_chk
                BCS     ilm_header              ; 不一致 → 別ヘッダを探す
ilm_name_ok:
                JSR     cmt_hdr_show            ; ヘッダの中身 (名前 / ファイル形式 / 属性) を表示
                ; ヘッダ確定 = データ読取フェーズへ。偽 sync 弾きはここで解除する。
                CLR     cmt_strict_hdr
                ; ヘッダのファイル形式 (+8) が機械語 ($02) でなければ、名前の表示の
                ; 直後に Bad Data In File (58)。LOADM で読むのは SAVEM で作った
                ; 機械語ファイルに限る (参考書籍)。
                LDA     cmt_hdr_buf+8
                CMPA    #$02
                LBNE    bdf_err                 ; モータ停止 + Bad Data In File
                ; ブロックバッファを空に初期化 (論理バイトリーダ用)
                CLR     mlm_brem
                ; セグメント列の解析開始
ilm_seg_lp:
                BSR     mlm_get_req             ; セグメント種別バイト
                CMPA    #$FF
                BEQ     ilm_term                ; $FF → 終端 (実行アドレス続く)
                TSTA
                BNE     ilm_seg_lp              ; $00 以外 → 同期外れ, 読み飛ばす
                ; 機械語テープレコードの構造 ($00 マーカ後):
                ;   [データ長 (2バイト, BE)] [ロード先アドレス (2バイト, BE)] [データ...]
                ; (参考書籍記載の機械語ファイル形式)
                ; 長さ (BE) を先に読む
                BSR     mlm_get_req
                STA     mlm_seg_len             ; lenHi
                BSR     mlm_get_req
                STA     mlm_seg_len+1           ; lenLo
                ; ロード先アドレス (BE)
                BSR     mlm_get_req
                STA     cmt_load_ptr            ; addrHi
                BSR     mlm_get_req
                STA     cmt_load_ptr+1          ; addrLo
                ; ",オフセット" 指定がある場合、参考書籍の LOADM 第2引数
                ; 仕様に従い、指定値を各セグメントの埋め込みアドレスに「加算」する
                ; (= 相対オフセット)。指定値そのものがオフセットであり、先頭
                ; セグメントアドレスとの差分ではない。
                ; 例: 埋め込み $5000 / 指定 &H100 → $5100 へ展開、実行アドレスも +$100。
                LDA     ilm_have_addr
                BEQ     ilm_seg_apply
                CMPA    #1
                BNE     ilm_seg_apply           ; 既に確定 (=2) ならスキップ
                LDD     mlm_user_addr           ; 指定値 = オフセット
                STD     mlm_offset              ; = 相対オフセット (そのまま)
                LDA     #2
                STA     ilm_have_addr           ; オフセット確定
ilm_seg_apply:
                ; 格納先 = segAddr + offset
                LDD     cmt_load_ptr
                ADDD    mlm_offset
                STD     cmt_load_ptr
                ; 先頭セグメントの最終ロードアドレスを記録 (実行アドレスワークへ複写用)。
                ; まだ未設定 ($FFFF) のときだけ保存し、以降のセグメントでは更新しない。
                LDX     mlm_first_addr
                CMPX    #$FFFF
                BNE     ilm_seg_byte
                STD     mlm_first_addr
                ; len バイトを論理バイトリーダから読んで格納
ilm_seg_byte:
                LDD     mlm_seg_len
                BEQ     ilm_seg_lp              ; len=0 → 次セグメントへ
                JSR     mlm_get_one
                BCS     ilm_done
                LDU     cmt_load_ptr
                STA     ,U+             ; 現在のメモリ構成のまま格納する
                STU     cmt_load_ptr
                LDD     mlm_seg_len
                SUBD    #1
                STD     mlm_seg_len
                BRA     ilm_seg_byte

; mlm_get_req — 論理バイトを 1 つ読む。ブロックが尽きていれば呼び出し元へは
;   戻らず ilm_done へ抜ける。読取りの各所の共通の入口。
mlm_get_req:
                JSR     mlm_get_one
                BCC     mgr_ok
                LEAS    2,S             ; 呼び出し元の戻りアドレスを捨てる
                BRA     ilm_done
ilmh_ioerr:
                ; LOADM のヘッダのチェックサムの誤り。偽 sync 弾きを解いてから、
                ; LOAD? と同じ出口 (モータ OFF + Device I/O Error (53)) へ。
                CLR     cmt_strict_hdr
                JMP     isk_ioerr
mgr_ok:
                RTS

ilm_term:
                ; 終端レコード ($FF マーカ) もセグメントと同じ並び:
                ;   [$FF] [長さ (2バイト, 常に 0)] [実行開始アドレス (2バイト, BE)]
                ; まず長さフィールド (2バイト) を読み飛ばし、続く実行アドレスを取得する。
                BSR     mlm_get_req
                BSR     mlm_get_req
                ; 実行開始アドレス (BE) を取得
                BSR     mlm_get_req
                STA     mlm_exec
                BSR     mlm_get_req
                STA     mlm_exec+1
                ; オフセット適用
                LDD     mlm_exec
                ADDD    mlm_offset
                STD     mlm_exec
ilm_done:
                ; 装置別の後始末: ディスク経路 (mlm_dev bit7=1) でだけ FDC モータを
                ; 止める。テープ経路 (mlm_dev=$00) では FDC レジスタに触れない。
                LDA     mlm_dev
                BPL     ilmd_notdisk
                CLR     FDC_DRV                 ; FDC モータ OFF (ディスク経路のみ)
ilmd_notdisk:
                CLR     cmt_strict_hdr          ; 偽 sync 弾きを解除 (LOADM の全終端で)
                ; LOADM 完了時にカセットモータを OFF にする (参考書籍)。
                ;   機械語を 1 バイトもロードできなかった場合もここで止める。その後の扱いは ild_no_ml。
                JSR     cmt_motor_off           ; モータ OFF (LOADM の完了)
                ; 直近の LOADM の入口アドレスを EXEC アドレス (EXEC_ADDR $0217) に
                ; 残す。開始アドレスを省いた EXEC はこの値から実行する (参考書籍)。入口はファイル
                ; の実行開始アドレス、それが無ければ先頭のロードアドレス。
                ; 1 バイトもロードしていない ($FFFF) ときは書かない。
                LDD     mlm_first_addr
                CMPD    #$FFFF
                BEQ     ild_no_ml               ; ML 未ロード → ild_no_ml で始末する
                LDD     mlm_exec
                BNE     ilm_set_exec
                LDD     mlm_first_addr
ilm_set_exec:
                STD     EXEC_ADDR
                PULS    X                       ; ディスパッチャ復帰用 X
                ; ,R 指定なら実行アドレスへ JSR (モータは OFF 済み)
                LDA     mlm_run
                BEQ     ilm_ret                 ; ,R 無し → BASIC へ復帰
                PSHS    X
                ; 入口は EXEC アドレス ($0217) と同じ決め方にする。ファイルの実行
                ; 開始アドレス、それが無い ($0000) ときは先頭のロード先アドレス。
                ; 直前の STD EXEC_ADDR がその値を置いてあるので読み直す。
                ; 開始アドレスを省いた EXEC はこの入口から実行するので、,R が実行する
                ; 入口と EXEC の入口は同一でなければならない。
                LDX     EXEC_ADDR
                ; 入口アドレスへそのまま JSR する。メモリ構成 (ROM / RAM モード) は
                ; 切り換えない (EXEC と同じ扱い)。
                ; 利用者機械語へ制御を渡す前に周期タイマ割込を止め、
                ; 復帰したら再開する (EXEC の経路と同じ)。復帰しない委譲では止まった
                ; ままとなり、機械語側の割込運用と干渉しない。
                JSR     time_disarm
                JSR     ,X                      ; ML エントリへ
                JSR     time_rearm
                PULS    X
ilm_ret:
                ; モータは ilm_done 冒頭で OFF 済み。
                ANDCC   #$FE
                RTS
ild_no_ml:
                ; 機械語を 1 バイトもロードできなかった。CMT モータは ilm_done 冒頭で
                ; 既に OFF (ディスク経路の FDC モータも同じ)。
                PULS    X                       ; ディスパッチャ復帰用 X
                ; ",,R" は「ロードした機械語の入口から実行を始める」指定なので、
                ; 入口が決まらないここでは分岐しない。LOADM で読むのは SAVEM で
                ; 作った機械語ファイルに限る (参考書籍) ので、Bad Data In File (58) で
                ; 中止する。",,R" の指定が無いときは BASIC へ復帰する。
                LDA     mlm_run
                LBNE    bdf_err
                ANDCC   #$FE
                RTS

;------------------------------------------------------------------------------
; mlm_get_byte: ML データブロック列から論理 1 バイトを取り出す
;   ブロックバッファ (MLBUF = テープの入出力バッファ) が
;   空なら次のデータブロック (種別 01) を読み込む。
;   type=$02/$FF/その他 (=データ終端) に達したらキャリーセットで返す。
;   データブロックのチェックサムの誤りは Device I/O Error (53) で中止する (戻らない)。
;   出力: A = バイト, キャリー=0 成功 / キャリー=1 終端
;   破壊: A,B,X,U (cmt_read_block 経由)。mlm_seg_len 等は保存。
;------------------------------------------------------------------------------
mlm_get_byte:
                LDA     mlm_brem
                BNE     mgb_serve               ; バッファに残りあり
                ; 次のデータブロックを読む
mgb_fill:
                LDU     #MLBUF
                JSR     cmt_read_block
                LBCS    isk_ioerr               ; チェックサムの誤り → モータ OFF +
                                                ;   Device I/O Error (53)
                LDA     cmt_blk_type
                CMPA    #1
                BNE     mgb_eof                 ; type≠1 → データ終端
                LDA     cmt_blk_len
                BEQ     mgb_fill                ; 長さ0 → 次ブロック
                STA     mlm_brem
                LDD     #MLBUF
                STD     mlm_bptr
mgb_serve:
                LDU     mlm_bptr
                LDA     ,U+
                STU     mlm_bptr
                DEC     mlm_brem
                ANDCC   #$FE                    ; キャリークリア = 成功
                RTS
mgb_eof:
                ORCC    #$01                    ; キャリーセット = 終端
                RTS


; ramstub_ifc ($01B5-) は mlm_install_ramstub がその場で組み立てる。未設定の EXEC
;   アドレス / USR アドレスの飛び先で、未設定のときは Illegal Function Call に
;   なる (参考書籍)。



; firq_read — PORT_FIRQ を読む窓口 (テープの計測 cmt_measure_high だけは直接読む)。
;   同レジスタは ATTENTION (bit0) と中断キー (bit1) を 1 つのフラグに載せ、一度読むと落ちる。
;   ATTENTION は控え (key_attn)、押下は BREAK フラグ (BRK_FLAG) へ積む。
;   出力: A = $FD04 の値を反転したもの (1 = 発生)。破壊: A, CC (B/X/Y/U は保つ)。
firq_read:
                LDA     >PORT_FIRQ      ; $FD04 (bit0 = ATTENTION, bit1 = 中断キー)
                COMA                    ; 「0=発生」を「1=発生」へ
                BITA    #$02
                BEQ     fqr_nobrk       ; 中断キーは押されていない
                STA     >BRK_FLAG       ; 押下を控えへ (bit1 が立つので必ず非 0)
fqr_nobrk:
                BITA    #$01
                BEQ     fqr_rts         ; ATTENTION は載っていない
                INC     >key_attn       ; 受領を控えへ (0 以外 = 未消費の発生あり)
fqr_rts:
                RTS

; firq_isr — FIRQ の受け口。割込フックスロットの FIRQ 欄 ($01E0) から JMP で入る。
;   FIRQ の要因は ATTENTION (サブシステムからの通知) と中断キーの 2 つで、
;   どちらも $FD04 を読むまで要求線が下りない (参考書籍)。ここでは要因を控えへ
;   積むだけで、
;   分岐は文の切れ目 (key_attn_poll / brk_gate) が行う。
;   FIRQ は CC と PC しか積まない (E=0) ので、使うレジスタは自分で積む。
firq_isr:
                PSHS    A
                BSR     firq_read       ; $FD04 を読んで要因を控えへ (要求線が下りる)
                PULS    A
                RTI

;------------------------------------------------------------------------------
; brk_gate / brk_stop — 中断キーの検査と中断停止
;   中断キーは PORT_FIRQ の bit1 (0=押下)。
;   brk_gate は実行ループの文の切れ目 (iep_stmt_gate) から呼ばれる (直接モードの
;   行も実行ループが実行する)。検査を通れば本来の一時文字列の開放へ末尾呼びする。
;   押下時は brk_stop へ落ちる。停止位置は CURIP (これから実行する文の先頭) で、
;   CONT はその文からやり直す。カセットのモータは中断とともに止める。
;   表示と再開位置の記録は STOP と同じ経路 (impl_stop) が受け持つ。
;------------------------------------------------------------------------------
brk_gate:
                BSR     firq_read       ; $FD04 を読んで押下を BRK_FLAG へ
                TST     >BRK_FLAG
                BEQ     bg_pass         ; 非押下 → 本来の処理へ
brk_stop:
                CLR     >BRK_FLAG       ; 押下を受け付けた
                CLRA
                TFR     A,DP            ; 直接ページを既定 (0) へ戻す
                                        ;   (テープ計測の途中から来る経路があるため)
                JSR     cmt_motor_off   ; カセットのモータ停止
                LDX     CURIP           ; 停止位置 = これから実行する文の先頭
                JMP     impl_stop       ; "Break In <行>" + 再開位置の記録
bg_pass:
                JMP     str_tmp_clear   ; 文の切れ目の一時文字列開放 (末尾呼び)


;==============================================================================
; clear_body — CLEAR [文字列領域サイズ][,最上位アドレス] の本体
;   (impl_clear から JMP)
;
;   CLEAR 文は全変数を消去し、文字列領域の大きさと、
;   BASIC が使う最上位アドレスを指定する。プログラム本体 (TXTTAB..VARTAB) は残る。
;
;   要求された大きさを確保できるかを、変数を消す前に検査する。
;     ・第1引数 n は「整数を要求する文脈」の規約 (最近接丸め、整数の範囲外は
;       Overflow)。負値は Illegal Function Call。
;     ・第2引数 m は「アドレスを要求する文脈」の規約 (ゼロ方向切り捨て、
;       -65536..65535 の外は Illegal Function Call)。16 ビットで見て $8000 以上
;       (物理 RAM 上端 STACK_TOP=$7FFF 超) は Out Of Memory。
;     ・確保可否: 新しい文字列領域の下限 (新 MEMTOP − n) の下へ、いま使っている
;       システムスタック (枠列を含む) をそのまま移したときの S − STK_RESERVE が
;       VARTAB 以上であること。足りなければ Out Of Memory。
;       VARTAB はプログラム末尾 = 変数領域の先頭で、CLEAR は変数を消すので
;       ここが「下限が侵してはならない境界」になる。
;     誤りにするときは何も書き換えない (変数・配列・MEMTOP・文字列領域は
;       そのまま)。検査を全て通してから破壊を始める順序にしてある。
;   枠 (CPU スタック): 0,S = 文字列領域の大きさ / 2,S = 新しい MEMTOP
;==============================================================================
impl_clear:
clear_body:
                LDD     MEMTOP
                PSHS    D               ; 2,S:3,S = 新しい MEMTOP (暫定 = 現状維持)
                SUBD    STRBOT
                PSHS    D               ; 0,S:1,S = 現在の文字列領域の大きさ
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     ic_init         ; 引数なし → 文字領域と上限アドレスを初期設定
                CMPA    #':'
                BEQ     ic_init
                CMPA    #','            ; 第1引数省略で ",m" だけの形
                BEQ     ic_arg2
                ; 第1引数 (文字列領域の大きさ)
                JSR     eval_to_int     ; 整数の規約 (範囲外は Overflow)
                TSTA
                BMI     ic_err5         ; 負の大きさ → Illegal Function Call
                STD     ,S              ; 大きさを差し替え
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                CMPA    #','
                BNE     ic_fin
ic_arg2:
                JSR     skip1_sp_x              ; 空白読み飛ばし (1 進めてから)
                LDA     ,X
                BEQ     ic_fin          ; ",m" 省略 → 何もしない
                CMPA    #':'
                BEQ     ic_fin
                ; 第2引数 (最上位使用アドレス m)
                ;   拡大も許可する (上限は物理 RAM 上端)。
                JSR     eval_to_addr    ; アドレスの規約 (-65536..65535、外は誤り 5)
                CMPA    #$80            ; A>=$80 ⇔ 16 ビットで見て $8000 以上
                BHS     ic_err7         ; 物理 RAM 上端超 → Out Of Memory
                SUBD    #1
                STD     2,S             ; 新しい BASIC 使用上限 −1 (= m−1。
                                        ;   参考書籍)
                BRA     ic_fin
ic_init:
                ; 引数を省いた CLEAR は文字領域と BASIC の使用する上限アドレスを
                ;   初期設定する (参考書籍)。初期値は
                ;   起動時と同じ 上限 −1 = STACK_TOP−1、文字領域 = 300 byte。
                ;   確保可否の検査と実際の張り直しは以降の共通路が行う。
                LDD     #STACK_TOP-1
                STD     2,S
                LDD     #STRSPC_DEF
                STD     ,S
ic_fin:
                ; --- 確保できるか: 移設後のシステム S − STK_RESERVE >= VARTAB か ---
                LDD     2,S
                SUBD    ,S              ; 新しい文字列領域の下限
                BLO     ic_err7         ; 桁借り = 下限が 0 を割る
                PSHS    D               ; 0,S = 新しい下限 (一時)
                TFR     S,D             ; 現在の S (枠列と呼出フレームを含む使用中の範囲)
                ADDD    ,S              ; + 新しい下限 (和は $10000 を越えない)
                SUBD    STRBOT          ; − 旧下限 = 移設後の S
                BLO     ic_err7         ; 使用中の範囲が新しい下限より大きい
                SUBD    #STK_RESERVE
                BLO     ic_err7
                CMPD    VARTAB
                BLO     ic_err7         ; 変数領域の先頭を侵す → Out Of Memory
                LEAS    2,S             ; 一時を捨てる
                ; --- ここから確定。破壊を始める ---
                LDD     2,S
                STD     MEMTOP          ; 新しい BASIC 使用上限
                ; 変数/配列領域を開放 (ARYTAB=STREND=VARTAB = 変数なし)
                JSR     vars_clear_all
                PULS    D               ; D = 文字列領域の大きさ
                LEAS    2,S             ; 新 MEMTOP の枠は用済み
                ; clear_relocate_stack は作業で X を破壊する。X は呼び出し元
                ; (impl_exec_prog / cmd_direct) が続行位置として使う BASIC
                ; 命令ポインタなので、退避してから移設し復元する。退避値は移設
                ; 対象スタック領域に乗るため、移設後の新スタックから正しく取り出せる。
                PSHS    X               ; CURIP (命令ポインタ) を退避
                JSR     ic_setbot       ; 下限確定 + 必要なら移設 + 領域初期化
                PULS    X               ; 移設後スタックから CURIP を復元
                ; X (実行中の文の位置) は実行用行バッファ (固定) の中なので補正不要。
                ; CLEAR の後始末 (捕捉先の解除 + 再開位置の破棄) は
                ; ic_finish が受け持つ。
;------------------------------------------------------------------------------
; ic_finish — CLEAR の後始末 (clear_body から末尾呼び)。
;   ・誤りの捕捉先を解除する (参考書籍)。
;   ・CONT の再開位置も破棄する。
;------------------------------------------------------------------------------
ic_finish:
                CLR     ONERROR_EN
                JMP     cont_clear
ic_err5:
                JMP     errc_5          ; 積んだ枠の始末は実行ループの巻戻しに委ねる
ic_err7:
                JMP     err_rg          ; Out Of Memory (同上)




