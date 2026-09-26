; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; CMT 書き込み (SAVE) 低レベルルーティン
;
; FM-7 CMT 書き込みハードウェア:
;   $FD00 write bit1 = モーターON, bit0 = テープ書込データ
;   ビット 0 → 2400Hz (短い半周期×2)
;   ビット 1 → 1200Hz (長い半周期×2)
;   フレーム: スタートビット(0) + 8データビット(LSB先) + ストップビット(1)×2
;
; レベルを保持している間の経過サイクルがそのままパルス幅になる。
; 各半周期は「bit0 を立てて/倒して ディレイ」で生成する。
;------------------------------------------------------------------------------
; cmt_out_level: 書込データビット (B の bit0) と モーターON を $FD00 へ出力
;   入力: B = 0 (LOW) または 1 (HIGH) / 壊すレジスタ: A
;------------------------------------------------------------------------------
cmt_out_level:
                TFR     B,A
                ANDA    #$01            ; bit0 = 書込データ
                ORA     #$02            ; bit1 = モーターON 維持
                STA     PORT_CMT_CTRL   ; $FD00
                RTS

;------------------------------------------------------------------------------
; cmt_write_bit: 1 ビットを FSK パルスとして出力する
;   入力: A の bit0 = 出力ビット (0=2400Hz, 1=1200Hz) / 壊す: A,B,X
;------------------------------------------------------------------------------
cmt_write_bit:
                ; 半周期幅を X に確定してから HIGH/LOW を共通経路で出す
                ; (尾部併合。パルス幅は FSK 判別余裕 408cy/756cy に収める)
                LDX     #$0022          ; bit=0: 短半周期 ≈ 408cy
                ANDA    #$01            ; (Z 判定は LDX の後で行う)
                BEQ     cwb_go
                LDX     #$003F          ; bit=1: 長半周期 ≈ 756cy
cwb_go:
                ; --- クロック周波数の切替への追従 (参考書籍) ---
                ;   動作クロックが 1.2 MHz ($FD00 bit0 = 0。参考書籍) のときは同じ計数でも
                ;   実時間が 1.628 倍に伸びるので、幅を 5/8 倍 (= 1/2 + 1/8) に縮めて
                ;   同じパルス幅にする。判定の 10cy は LOW 半周期の計数を 1 減らして相殺。
                LDB     PORT_CMT_CTRL   ; $FD00 (読出し) bit0 = クロック
                LSRB
                BCS     cwb_2m          ; 2 MHz → そのまま
                TFR     X,D             ; (X < 256 なので A = 0)
                LSRB
                PSHS    B               ; X / 2
                LSRB
                LSRB                    ; X / 8
                ADDB    ,S+
                SUBB    #2              ; 切替の判定と計算のぶん (約 30cy) を差し引く
                TFR     D,X
cwb_2m:
                LDB     #1              ; HIGH 半周期
                PSHS    X               ; (cmt_delay_x が X を壊すため幅を退避)
                BSR     cmt_out_level
                JSR     cmt_delay_x
                CLRB              ; LOW 半周期
                BSR     cmt_out_level
                PULS    X
                LEAX    -2,X            ; 併合で増えた経路 12cy と切替の判定 10cy を計数 2 回で相殺
                JMP     cmt_delay_x     ;   (LOW 半周期の計測幅を規定値に保つ)

;------------------------------------------------------------------------------
; cmt_write_byte: 1 バイトをスタート/データ×8(LSB先)/ストップ×2 で出力
;   入力: A = 出力バイト / 壊す: A,B,X,Y
;------------------------------------------------------------------------------
cmt_write_byte:
                PSHS    A
                CLRA                    ; スタートビット = 0
                BSR     cmt_write_bit
                LDY     #8              ; データビット 8 個 (LSB 先)
                LDB     ,S
cwby_lp:
                TFR     B,A
                ANDA    #$01
                PSHS    B
                BSR     cmt_write_bit
                PULS    B
                LSRB
                LEAY    -1,Y
                BNE     cwby_lp
                LDA     #1              ; ストップビット = 1 ×2
                BSR     cmt_write_bit
                LDA     #1
                BSR     cmt_write_bit
                PULS    A,PC

;------------------------------------------------------------------------------
; cmt_write_block: 1 ブロックをテープへ書き込む
;   入力: cmt_blk_type, cmt_blk_len, sv_ptr=データ先頭 / 壊す: A,B,X,Y,U
;   形式: リーダー $FF×N + 同期 $01 $3C + type + len + data + checksum
;------------------------------------------------------------------------------
cmt_write_block:
                ; 書込パルス幅はサイクル計数ディレイで生成するため、タイマ割込の
                ; ジッタが乗らないようブロック書込中は周期タイマを止める。
                JSR     time_pause
                ; ギャップ $FF は、モータを回し始めた直後のヘッダブロックだけ
                ; 255 バイト、続くデータ／エンドブロックは 10 バイト
                ; (参考書籍)。
                LDB     #10
                TST     cmt_blk_type    ; $00 = ヘッダ (モータ始動の直後)
                BNE     cwbk_gap
                LDB     #$FF            ; 255 バイト
cwbk_gap:
                JSR     cmt_gap_out
                LDA     #$01            ; 識別子 $01 $3C
                BSR     cmt_write_byte
                LDA     #$3C
                BSR     cmt_write_byte
                LDA     cmt_blk_type    ; type
                BSR     cmt_write_byte
                LDA     cmt_blk_len     ; len
                BSR     cmt_write_byte
                LDA     cmt_blk_type    ; checksum 初期値 = type + len
                ADDA    cmt_blk_len
                STA     sv_chk
                LDA     cmt_blk_len
                STA     sv_len
                BEQ     cwbk_csum       ; 長さ0 → データなし
                LDX     sv_ptr
cwbk_data:
                LDA     ,X+
                TFR     A,B             ; チェックサム加算は B 側で行い A を温存
                ADDB    sv_chk
                STB     sv_chk
                PSHS    X               ; X (データポインタ) を保護
                BSR     cmt_write_byte  ; cmt_write_byte は X を壊す
                PULS    X
                DEC     sv_len
                BNE     cwbk_data
                STX     sv_ptr          ; 次ブロック用に進める
cwbk_csum:
                LDA     sv_chk          ; チェックサムバイト
                BSR     cmt_write_byte
                LDB     #4              ; エンドギャップ $FF ×4 (参考書籍)
                JSR     cmt_gap_out
                JMP     time_resume     ; タイマ再開して復帰 (末尾 RTS 共用)

;------------------------------------------------------------------------------
; sv_wr_blk: cmt_hdr_buf を先頭とするブロックを書き出す共通部 (共通化)
;   入力: A = len, B = type
;------------------------------------------------------------------------------
sv_wr_blk:
                STB     cmt_blk_type
                STA     cmt_blk_len
                LDD     #cmt_hdr_buf
                STD     sv_ptr
                BRA     cmt_write_block ; (末尾呼び)

;------------------------------------------------------------------------------
; sv_name_cp: cmt_filename (8B, $00 終端以降はゼロ埋め) を cmt_hdr_buf へ複写
;   出力: U = cmt_hdr_buf+8 / 壊す: A,B,X
;------------------------------------------------------------------------------
sv_name_cp:
                LDX     #cmt_filename
                LDU     #cmt_hdr_buf
                LDB     #8
svn_lp:
                LDA     ,X              ; 終端 $00 以降はゼロ埋め (残骸混入防止)
                BEQ     svn_z
                LEAX    1,X
svn_z:
                STA     ,U+
                DECB
                BNE     svn_lp
                RTS

;==============================================================================
; sv_buf_put: out_char 経由でバイト A を IIL_BUF のブロックバッファへ追記し、
;   128B 貯まるごとに type=1 データブロックとして書き出す (ストリーム化)。
;   ASCII SAVE のテキストは「連続バイト列を最大 255B のブロックに詰める」形式
;   (参考書籍のテープファイル形式) であり、1 行 = 1 ブロックである必要はない。
;   1 行分のテキストを IIL_BUF (128B) へ丸ごと組み立てると、長い行でバッファを
;   超えて常駐ワークを踏み、255B 超の行はブロック長 (1B) にも収まらないので、
;   バイト単位で流して行長に依存しないようにする (読込側 stream_lines は
;   複数行/ブロック跨ぎを扱える)。
;   壊すレジスタ: なし (A,B は退避、X,Y,U は out_char が保護して呼ぶ)
;==============================================================================
sv_buf_put:
                PSHS    A,B
                LDX     sv_ptr
                STA     ,X+
                STX     sv_ptr
                CMPX    #IIL_BUF+128
                BLO     sbp_ret                 ; まだ満杯でない
                LDB     #128
                BSR     sv_asc_wr               ; 満杯 → 128B ブロックを書き出す
sbp_ret:
                PULS    A,B,PC

; sv_asc_wr: IIL_BUF 先頭から B バイトを type=1 ブロックで書き、sv_ptr を
;   IIL_BUF 先頭へ戻す。壊す: A,B,X (呼出元で退避)
sv_asc_wr:
                BSR     sv_rst                  ; sv_ptr = IIL_BUF (データ先頭)
                BSR     sv_wr_data              ; type=1, len=B で書出し
                ; fallthrough: 書出しで進んだ sv_ptr を先頭へ戻して復帰
; sv_rst: sv_ptr を IIL_BUF 先頭へ戻す
sv_rst:
                LDX     #IIL_BUF
                STX     sv_ptr
                RTS

; sv_wr_data: sv_ptr のデータを type=1, len=B で書き出す共通部 (共通化)
;   入力: B = len (1..255), sv_ptr = データ先頭 (書出しで len 分進む)
sv_wr_data:
                STB     cmt_blk_len
                LDA     #1
                STA     cmt_blk_type
                JMP     cmt_write_block         ; (末尾呼び)

;==============================================================================
; SAVE "ファイル名" [,A]
;   保存は参考書籍のヘッダブロック + データブロック +
;   エンドブロックの形で行う。ヘッダのファイル形式は $00 (BASIC ソース)、属性は
;   $FF (アスキー) で、本文は行の綴りをそのまま並べ終端記号 $1A で閉じる。
;   ,A (アスキー形式の指定) は「指定しても同じ」として受理する。
;   ,B は本 ROM が採らない指定なので Syntax Error とする。
;   ,P (プロテクト保存) は prot_flag を立て、属性 $FE で書き出す。
;   装置がディスク (dl_device bit7=1) のときはディスク SAVE (dsv_body) へ分岐。
;   入力: X = SAVE トークンの次位置
;   X はディスパッチャ復帰用に引数を読み飛ばした位置を指して返す。
;==============================================================================
impl_save:
                ; オーディオカセット系コマンドは演奏を中断する (参考書籍)
                JSR     play_stop
                ; SAVEM 判定: SAVE トークン直後の 'M' = 機械語セーブ
                LDA     ,X
                CMPA    #'M'
                LBEQ   impl_savem
                JSR     impl_parse_fname        ; cmt_filename 設定, X 前進
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     isv_mode_done
                LEAX    1,X
                LDA     ,X
                BEQ     isv_mode_done           ; 裸の ',' で行末 → 消費しない
                CMPA    #':'
                BEQ     isv_mode_done           ; 文区切りも消費しない
                CMPA    #'B'
                LBEQ    errc_2                  ; SAVE ,B は Syntax Error
                CMPA    #'P'                    ; ,P = 保護して保存 (参考書籍 SAVE)
                BNE     isv_opt_done
                LDA     #1
                STA     prot_flag               ; 以後この本文は保護される
isv_opt_done:
                LEAX    1,X                     ; オプション文字を消費 (,A/,P 等)
isv_mode_done:
                PSHS    X                       ; ディスパッチャ復帰用 X
                ; 装置分岐: ディスク指定 (bit7=1) はディスク SAVE へ。
                ; 復帰用 X は積んだまま渡す (dsv_body 側の末尾が PULS X する)。
                LDA     dl_device
                LBMI    dsv_body
                ; モーター ON
                JSR     cmt_motor_on    ; モータ ON (控えも更新)
                ; --- ヘッダブロック (種別 00) ---
                ; ヘッダのデータ = ファイル名 8B ($00 埋め) / +8 ファイル形式 $00
                ; (BASIC ソース) / +9 属性 $FF (アスキー)。参考書籍
                ; (ヘッダブロックにはファイル名・ファイル形式・属性が記録される)
                ; と、参考書籍のファイルタイプ (0 = BASIC ソース) と
                ; アスキーフラグ ($FF = アスキー) の値による。
                BSR     sv_name_cp              ; 名前 8B → cmt_hdr_buf (U=+8)
                CLRA                            ; +8 ファイル形式 $00 (BASIC ソース)
                LDB     #$FF                    ; +9 属性 $FF (アスキー)
                SUBB    prot_flag               ;   保護保存は $FE
                STD     ,U++
                ; ヘッダブロック書き込み (種別 00, 長さ 10)
                LDA     #10
                CLRB
                JSR     sv_wr_blk               ; (距離増で BSR 圏外)
                ; --- 本体ブロック群 (ASCII 形式: 全行 + 終端 $1A) ---
                ; テキストを連続バイト列として sv_buf_put へ流し、128B ごとの
                ; データブロック (種別 01) に詰めて書き出す (行とブロックは独立)。
                ; 本文は原文のまま (LIST と同一の出力)。
                LDD     #sv_buf_put
                STD     out_vec
                BSR     sv_rst                  ; sv_ptr = IIL_BUF (ブロックバッファ書込先)
                BSR     sv_emit_lines           ; 全行 (行番号 + 本文原文 + CR)
                LDA     #$1A                    ; 終端記号
                JSR     out_char
                ; 端数 (未満杯) ブロックの書き出し
                LDD     sv_ptr
                SUBD    #IIL_BUF
                BEQ     isv_finish              ; 端数なし
                BSR     sv_asc_wr               ; B = 端数長 (1..127)
isv_finish:
                ; --- エンドブロック (種別 $FF, 長さ 0。参考書籍) ---
                CLRA                            ; len=0 (sv_ptr は不参照)
                LDB     #$FF
                JSR     sv_wr_blk
                ; モーター OFF
                JSR     cmt_motor_off
                PULS    X,PC ; ディスパッチャ復帰用 X

;------------------------------------------------------------------------------
; sv_emit_lines — 在メモリ全行を「行番号 10 進 + 本文原文 + CR」で out_char へ
;   流す (ASCII 形式の本文の共通部。テープ/ディスクの両 SAVE から使う。LIST と同じ
;   出力 = 往復で本文バイト一致)。破壊: A,B,X,Y
;   (行レコード先頭は Y に保つ。print_uint16 / list_body_emit / out_char は
;    いずれも Y を保存する)
;------------------------------------------------------------------------------
sv_emit_lines:
                LDX     TXTTAB
sel_lp:
                LDD     ,X                      ; next_ptr
                BEQ     sel_done                ; [00 00] → 完了
                LDD     2,X                     ; 行番号
                CMPD    unlist_ln               ; UNLIST で指定した非表示行番号
                BHS     sel_done                ; その値以上の行はアスキー形式で
                                                ;   保存できない (参考書籍 UNLIST)
                TFR     X,Y                     ; Y = 行レコード先頭
                JSR     print_uint16    ; X/Y は呼出先が保持する
                ; 本文 (原文。行番号直後の空白も原文が保持しているので足さない)
                JSR     list_body_emit
                LDA     #$0D                    ; CR 終端
                JSR     out_char
                LDX     ,Y                      ; next_ptr → 次行
                BRA     sel_lp
sel_done:
                RTS

;------------------------------------------------------------------------------
; impl_savem: SAVEM "ファイル名",開始アドレス,終了アドレス,実行アドレス
;   開始..終了 アドレスの機械語イメージをテープへ書き出す。参考書籍の機械語ファイル
;   形式 (LOADM が解釈するもの) に完全準拠して出力するため、LOADM で読み戻せる。
;   実行アドレスは必須引数 (参考書籍の SAVEM は 3 引数)。省略すると Syntax Error。
;   入力: X = SAVE トークン直後の 'M' を指す
;   テープ上の並び:
;     ヘッダブロック (type=0, 11B): name[8] file-type(=$02) loadHi loadLo
;     セグメントヘッダ (type=1, 5B): $00 lenHi lenLo addrHi addrLo   (len,addr は BE)
;     データブロック群 (type=1, <=255B/個): 開始..終了 の生バイト列
;     終端レコード (type=1, 5B): $FF 00 00 execHi execLo             (exec は BE)
;     EOF ブロック (type=2, 0B)
;   LOADM は type=1 ブロックのペイロードを論理連結し、$00/$FF マーカで
;   セグメント/終端を判別する。本体を素の type=1 生バイトのみで出力すると
;   ($00/$FF 枠を欠き) LOADM の読取と非整合になり、読み戻せない。
;   X はディスパッチャ復帰用に引数末尾へ進めて返す。
;------------------------------------------------------------------------------
impl_savem:
                LEAX    1,X                     ; 'M' を読み飛ばす
                JSR     impl_parse_fname        ; cmt_filename, X 前進
                BSR     ism_arg                 ; ',' 開始アドレス
                STD     cmt_load_ptr            ; 開始アドレス = 現在ポインタ
                BSR     ism_arg                 ; ',' 終了アドレス
                STD     svm_end                 ; 終了アドレス
                BSR     ism_arg                 ; ',' 実行アドレス (必須。省略 → Syntax
                                                ;   Error。参考書籍では 3 引数)
                STD     svm_exec                ; 実行開始アドレスを保持 (終端へ書く)
                PSHS    X                       ; ディスパッチャ復帰用 X
                ; モーター ON
                JSR     cmt_motor_on    ; モータ ON (控えも更新)
                ; --- ヘッダブロック (type 0, ML) ---
                JSR     sv_name_cp              ; 名前 8B ($00 終端以降ゼロ埋め, U=+8)
                LDA     #$02                    ; file-type = ML
                STA     ,U+
                LDD     cmt_load_ptr            ; 埋め込みロードアドレス
                STA     ,U+
                STB     ,U+
                ; ヘッダブロック書き込み (type=0, len=11)
                LDA     #11
                CLRB
                JSR     sv_wr_blk
                ; --- セグメントヘッダレコード: $00 lenHi lenLo addrHi addrLo ---
                ; 全長 = svm_end - 開始 + 1 (開始はまだ cmt_load_ptr にある)。
                ; cmt_hdr_buf をスクラッチに流用してレコードを組み立てる。
                LDU     #cmt_hdr_buf
                CLR     ,U+                     ; $00 セグメントマーカ
                LDD     svm_end
                SUBD    cmt_load_ptr
                ADDD    #1                      ; 包含長 (16bit)
                STA     ,U+                     ; lenHi
                STB     ,U+                     ; lenLo
                LDD     cmt_load_ptr            ; ロード先アドレス
                STA     ,U+                     ; addrHi
                STB     ,U+                     ; addrLo
                LDD     #$0501          ; A = 長さ 5, B = 種別 1
                JSR     sv_wr_blk               ; type=1, len=5
                ; --- 本体: 開始..終了 を 255B 単位の type=1 ブロックで出力 ---
ism_body_lp:
                ; 残り = svm_end - cmt_load_ptr + 1
                LDD     svm_end
                SUBD    cmt_load_ptr
                BMI     ism_body_done           ; ポインタが終了を越えた → 完了
                ADDD    #1                      ; 包含長
                TSTA                    ; 符号なし D <= 255 は A = 0 と等価
                BEQ     ism_len_ok
                LDD     #255
ism_len_ok:
                LDU     cmt_load_ptr
                STU     sv_ptr                  ; データ先頭
                JSR     sv_wr_data              ; type=1, len=B (D<=255 なので下位 B)。
                                                ;   sv_ptr は len 分進む (共通化)
                LDD     sv_ptr
                STD     cmt_load_ptr            ; 次ブロック先頭へ
                BRA     ism_body_lp
ism_body_done:
                JMP     savem_finish
ism_argerr:
                JMP     errc_2

; ',' + 数値式を D (16bit 整数) として読む (SAVEM の 3 引数を共通化)
ism_arg:
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     ism_argerr
                JMP     eval_to_addr_x1
                                                ;   定数は符号なしなので &HE000 も収まる

;------------------------------------------------------------------------------
; MERGE "ファイル名"
;   テープ上のアスキー形式のプログラムを読み、現在メモリ上の
;   プログラムへ行単位で併合する (同一行番号は置換、新規行は昇順挿入)。
;   受理は アスキー形式 (ヘッダの形式・属性 $00 $FF) のみ。それ以外は
;   Bad Data In File (参考書籍の 58) で中止する。
;   本文取込は LOAD と同じ行取込 (stream_lines) を共用し、1 行がブロック境界を
;   跨いでも/1 ブロックに複数行が入っても正しく取り込む。
;   入力: X = MERGE トークン直後 (ファイル名)
;------------------------------------------------------------------------------
impl_merge:
                JSR     play_stop               ; カセット系は演奏中断 (参考書籍)
                JSR     impl_parse_fname        ; cmt_filename, X 前進
                PSHS    X                       ; ディスパッチャ復帰用 X
                ; モーター ON
                JSR     cmt_motor_on    ; モータ ON (控えも更新)
                ; ヘッダブロック待ち (ファイル名照合)。EOT で確実に脱出する
                ; (BCS で無条件に再試行すると、テープ終端の無音固着で無限
                ;  ループになる。cmt_read_block は失敗の種別を B で返し、
                ;  B≠0 は EOT、B=0 は CRC エラー=再試行)。
img_header:
                LDU     #cmt_hdr_buf
                JSR     cmt_read_block
                BCC     imh_have
                TSTB                            ; EOT (B≠0) か CRC エラー (B=0) か
                BNE     img_done                ; EOT → モータ OFF で復帰
                BRA     img_header              ; CRC エラー → 再試行
imh_have:
                LDA     cmt_blk_type
                BNE     img_header              ; type≠0 → ヘッダでない
                JSR     cmt_name_chk            ; 名前照合 (指定なしは無条件一致)
                BCS     img_header              ; 不一致 → 別ヘッダを探す
                ; ヘッダの形式・属性 (cmt_hdr_buf+8/+9) を検証。
                ;   $00 $FF = ASCII 形式 → 併合へ
                ;   それ以外 → MERGE 不可 (Bad Data In File)
                LDD     cmt_hdr_buf+8
                CMPD    #$00FF
                BNE     img_bad_mode
                ; 本体を LOAD と同じ行取込 (stream_lines) で既存プログラムへ併合する。
                ; stream_lines は NEW を行わないため既存行は保持され、同一行番号は
                ; impl_insert_line が置換する。
                JSR     il_stream_prep          ; 緩衝初期化 + 供給 = テープ (LOAD と共用)
                JSR     stream_lines
img_done:
                JSR     cmt_motor_off           ; モーター OFF
                PULS    X,PC
img_bad_mode:
                ; アスキー形式以外は MERGE 不可。積んだ X の始末は誤り処理の
                ; スタック巻戻しに委ねる (bdf_err がモータ停止 + 58 で中止)。
                JMP     bdf_err

;==============================================================================
; out_char: 1 文字を out_vec が指す出力ルーティンへ送る (X,Y,U を必ず保護)
;   入力: A = 文字
;   list_body_emit / print_uint16 はこの間接出口を経由するため、
;   出力先 (impl_putchar=コンソール / cmt_write_byte=テープ) を out_vec で
;   切り替えられる。出力先が X/Y/U を壊しても呼び出し側は影響を受けない。
;==============================================================================
out_char:
                PSHS    X,Y,U
                JSR     [out_vec]
                PULS    X,Y,U,PC

;==============================================================================
; print_uint16: D を符号なし10進で out_char へ出力する
;   入力: D = 0..65535
;   壊すレジスタ: A,B,U と変換用の RAM 緩衝 str_conv_buf (X,Y は退避して保護)
;   方式: uint_digits で桁を str_conv_buf へ逆順に書き、先頭から out_char へ送る
;==============================================================================
print_uint16:
                PSHS    X,Y             ; 呼び出し側のポインタを保護
                JSR     uint_digits     ; 桁の分解は綴りを作る側と共用 (X = 先頭桁)
pu16_out:
                CMPX    #str_conv_buf+8
                BHS     pu16_done
                LDA     ,X+
                BSR     out_char        ; (out_char は X,Y,U を保護する)
                BRA     pu16_out
pu16_done:
                PULS    X,Y,PC

;==============================================================================
; 行変換の中核ヘルパ
;   格納本文は ASCII 原文 ([次行ptr 2B][行番号 2B][本文 原文][00])。実行時に
;   到達した 1 行だけを実行用行バッファ (EXECBUF, 256B) へ変換して
;   実行する。変換は決定的なので、同じ行を再変換すればバッファ内ポインタ
;   (CURIP・枠の IP・CONTPTR・DATPTR 等) は同値で有効になる。
;   バッファの内容は格納領域にもファイルにも決して書き戻さない。
;==============================================================================
;------------------------------------------------------------------------------
; exec_load_rec — 行レコード X の本文を実行用行バッファへ変換する (未変換時のみ)。
;   直接モードの行 (システムスタック上の擬似レコード) も同じ形なので区別しない。
;   X/Y/U は保存。破壊: D。
;   変換中の数値定数の範囲検査が Overflow を送出し得る (検査は実行時 = 行変換時)。
;------------------------------------------------------------------------------
exec_load_rec:
                CMPX    EXECREC
                BEQ     elr_ret         ; 既に載っている
                PSHS    U,Y,X
                ; 変換中の数値定数の範囲検査 (tok_numlit → pn_decimal) は
                ; FAC1/FACTYPE を破壊する。変換は式評価の途中 (FN 呼出の行復元・
                ; READ の DATA 行復元) にも走るため、評価中の値を退避する。
                LDU     #FAC1
elr_sv:
                LDA     ,U+
                PSHS    A
                CMPU    #FAC1+8
                BNE     elr_sv
                LDA     FACTYPE
                PSHS    A
                BSR     clr_execrec
                LEAX    4,X             ; X = 本文 (原文) 先頭 (X は退避ループで不変)
                LDU     #EXECBUF
                JSR     impl_tokenize_xu
                PULS    A
                STA     FACTYPE
                LDU     #FAC1+8
elr_rs:
                PULS    A
                STA     ,-U
                CMPU    #FAC1
                BNE     elr_rs
                PULS    X
                STX     EXECREC
                PULS    Y,U
elr_ret:
                RTS
;------------------------------------------------------------------------------
; exec_conv_line — 行レコード X へ実行を移すための変換 (行到達・行ジャンプ用)。
;   CURLINE_PTR/CURLIN を更新し、X = 変換済み本文の先頭 (作業列) を返す。
;   Y/U は保存。破壊: D。
;------------------------------------------------------------------------------
exec_conv_line:
                STX     CURLINE_PTR
                LDD     2,X
                STD     CURLIN
                BSR     exec_load_rec
                LDX     #EXECBUF
                RTS
;------------------------------------------------------------------------------
; exec_ensure — CURLINE_PTR の行が作業列に載っていることを保証する。
;   RETURN / NEXT / WEND / RESUME / CONT / READ の再開点から呼ぶ。
;   X/Y/U は保存。破壊: D。
;------------------------------------------------------------------------------
subcmd_xy1:                             ; 共通の並びを 1 本に括った
                LDD     tmpX16
                STD     SUBCMD_P3
                LDD     tmpY16
                STD     SUBCMD_P5
                RTS
clr_execrec:                            ; 共通の並びを 1 本に括った
                CLR     EXECREC
                CLR     EXECREC+1
                RTS
exec_ensure_curlin:                     ; CURLIN を更新してから枠を確かめる
                STD     CURLIN
exec_ensure:
                PSHS    X
                LDX     CURLINE_PTR
                BSR     exec_load_rec
                PULS    X,PC
;------------------------------------------------------------------------------
; list_body_emit — 行レコード X の本文 (原文) を out_char へそのまま出力する。
;   LIST と ASCII 形式 SAVE が共用する。原文には行番号直後の空白も含まれる
;   ため、呼出側は行番号の後に区切りの空白を足さない (入力の完全再現)。
;   破壊: A, X。
;------------------------------------------------------------------------------
list_body_emit:
                LEAX    4,X
lbe_lp:
                LDA     ,X+
                BEQ     lbe_ret
                JSR     out_char
                BRA     lbe_lp
lbe_ret:
                RTS
; (crs_fix_bufptrs は容量の都合で 7t_deffn.inc 側のプールに置く)

;==============================================================================
; LIST [開始行番号]
;   入力: X = LIST トークンの次位置 (省略時は $00 か ':' か直後トークン)
;   全行 (または開始行以降) を「行番号 SPACE 原文 CRLF」で出力する。
;   X は引数を読み飛ばした位置を指して返す。
;==============================================================================
impl_list:
                JSR     chk_prot        ; 保護中は Protected Program (参考書籍)
                JSR     out_vec_screen  ; 出力先を画面へ戻す (共通化)
                ; 行番号範囲を解析 (il_start_ln / il_end_ln を設定, X 前進)。
                ; 参考書籍の LIST 構文 (LIST / n / n- / n-m / -m) を解析する本体は
                ; ZMB $8F39 プールが吸収する il_parse_range に置く (当区間の
                ; ZMB $E787 予算が逼迫のため handler 側は JSR 1 個に留める)。
                JSR     il_parse_range
                ; 一覧の前に改行を 1 回。行頭にいるときはこれが空行 1 行として
                ; 残り、行の途中にいるときは単にその行を閉じる。該当行が無くても
                ; 出す (一覧の表示体裁)。
                JSR     disk_crlf
                ; 一覧を出し終えたら呼出元へ戻らず直接モードへ抜ける
                ; (詳細は il2_done)。よってディスパッチャ復帰用の X 退避は不要。
                LDX     TXTTAB
il2_lp:
                ; --- BREAK キー検査 (LIST 出力中断) ---
                ;   $FD04 (PORT_FIRQ) bit1 は BREAK キー状態 (active-low: 0=押下,
                ;   1=非押下)。FM-7 キーボードのハードウェア仕様に
                ;   基づき毎行 1 回ポーリングし、押下なら出力を打ち切って直接モード
                ;   へ戻す。読取は A のみ使用し X (リスト走査ポインタ) は保全する。
                LDA     PORT_FIRQ       ; $FD04 読み (bit1 = BREAK キー状態)
                BITA    #$02
                BEQ     il2_done        ; bit1=0 (BREAK 押下) → 出力中断
                LDD     ,X              ; next_ptr
                BEQ     il2_done        ; [00 00] → 終端
                LDD     2,X             ; この行の行番号
                CMPD    il_start_ln
                BLO     il2_next        ; 開始行より小さい → 表示しない
                CMPD    il_end_ln       ; 終端行番号 (既定 $FFFF=無制限)
                BHI     il2_done        ; 終端超過 → 以降は昇順ゆえ打ち切り
                CMPD    unlist_ln       ; UNLIST で指定した非表示行番号
                BHS     il2_done        ; その値以上の行は表示しない (参考書籍
                                        ;   UNLIST。以降は昇順ゆえ打ち切り)
                JSR     print_uint16    ; 行番号を10進出力
                ; 本文は原文をそのまま出す (入力の完全再現)。原文は行番号直後の
                ; 空白も保持しているので、ここで区切りの空白は足さない。
                PSHS    X
                BSR     list_body_emit
                PULS    X
                JSR     disk_crlf       ; 行末の CR+LF (共通ルーチンへ集約)
il2_next:
                LDD     ,X              ; next_ptr → 次行へ
                TFR     D,X
                BRA     il2_lp

il2_done:
                ; --- LIST は実行を終了して直接モードへ戻る ---
                ; 一覧を出し終える (または BREAK で打ち切る) と、プログラム中で
                ; あれば次の行以降を実行せず、直接入力であれば同じ行の ':' 以降も
                ; 実行せずに "Ready" へ戻る。LIST はプログラムの一覧をとる編集用の
                ; コマンドであり、実行の続きを持たないためである (参考書籍)。
                ; CONT の再開位置 (CONTPTR) には触れない。STOP で記録
                ; した再開位置は LIST を跨いでも生き、LIST 自身は再開位置を作らない。
                ; 復帰しないので呼出フレームが残るが、合流先の warm_start が
                ; システム S をコマンド待ちの基準へ戻す。
                JMP     warm_start


; DATE$ の日の範囲検査に使う月ごとの上限。値はその月で受け付けない最小の日
; (= 日数 + 1) で、date_set_str は CMPB / BHS の 1 回で撥ねられる。2 月は
; 平年の 29 を置き、閏年だけ呼出側が 30 に差し替える。
dss_mlimit:
                FCB     32,29,32,31,32,31,32,32,31,32,31,32

; parse_prog_linenum — 打ち込まれたプログラム行の行番号を読む (impl_insert_line)。
;   行番号は 0-63999 (参考書籍)。上限を超える綴りは行として受け付けず
;   Syntax Error とする。桁積みは 16 ビットを超えると上限 $FFFF に張り付くので、
;   66000 のように 16 ビットで巻き付く綴りも取りこぼさない。
;------------------------------------------------------------------------------
parse_prog_linenum:
                JSR     pn2_uint        ; D = 行番号
                CMPD    #64000
                LBHS    errc_2          ; 上限超過 → Syntax Error
                RTS

;------------------------------------------------------------------------------
; 画面機能設定 / カーソル表示 / カーソル消去 (参考書籍)
;
;   $DAEF cursor_off   — カーソル消去 ($05A8 の bit0 を落として送る)。破壊: CC, A, B
;   $DAF6 cursor_on    — カーソル表示 ($05A8 の値を送る)。破壊: CC, A, B
;   $DAF9 scr_func_set — B = CNSCTL の制御フラグ (bit0 = カーソル表示) を送る。
;         破壊: CC, A
;
;   3 つは 1 本のルーチンの 3 つの入口で、実体は CNSCTL の発行 1 つである。
;   $DAEF より前の ZMB は空き領域を表す。3 入口の間の ZMB は 0 バイトで、
;   各入口までの実処理の大きさを組立て時に検査する。
;   KEY LIST の一覧出力 (kl_*) も同じ入口を使う。コンソールは「制御コードを
;   動作として実行するか、字形として表示するか」の表示状態を持ち、CNSCTL の
;   bit1 で切り替わる。一覧はこれを字形側へ倒してから割当て文字列を流すので、
;   割当てに含まれる復帰などが 1 桁ぶんの絵として画面に出る。出し終えたら
;   動作側へ戻す。発行する値は定数ではなく控え (disp_state) から派生させる。
;   定数を置くと、利用者が LOCATE の第 3 引数でカーソルを出していた場合に
;   一覧を通しただけでカーソル表示のビットが落ちてしまう。
;------------------------------------------------------------------------------
;==============================================================================
; impl_fn_screen — SCREEN(水平位置, 垂直位置 [,セレクトスイッチ])
;   SCREEN 関数は指定したキャラクタ座標の
;   キャラクタコード (スイッチ省略 / 0) か属性 (スイッチ 1) を返す。属性は
;   8 で割った余りがパレットコード、商が 0 でノーマル・1 でリバース表示。
;   指定した座標にキャラクタの表示が無いときは、いずれの場合も 0 を返す。
;   読み取りは Get Character Block 2 ($08。参考書籍)
;   に 1x1 の矩形を与えて行う。応答は $D383 = 計数、$D384 = 属性、
;   $D385 = キャラクタコードである。
;   座標はどちらも 0..255 に収める (範囲外は Illegal Function Call)。
;   画面の桁数・行数を越える座標はサブシステムがコンソール座標値の誤りを
;   返し、共通のエラー写像が Illegal Function Call にする。
;   引数の解析は impl_fn_point と同じ規約で、閉じ括弧の次を fn_xsave へ置く。
;   セレクトスイッチの控えは $D387 (SUBCMD_P5) に置く。$08 はこの欄を入力にも
;   応答にも使わないので、発行を跨いでそのまま残る。
;   (置き場所は容量の都合。参照はラベル経由のみで位置依存はない)
;==============================================================================
impl_fn_screen:
                JSR     impl_expect_tok
                FCB     '('
                JSR     eval_to_int_255         ; B = 水平位置 (0..255)
                PSHS    B
                JSR     impl_expect_tok
                FCB     ','
                JSR     eval_to_int_255         ; B = 垂直位置 (0..255)
                PSHS    B
                CLRB                            ; スイッチ省略 = 0
                JSR     chk_comma_x
                BNE     ifs_close
                LEAX    1,X                     ; ',' を消費
                JSR     eval_to_int_1           ; スイッチは 0 か 1
ifs_close:
                JSR     impl_expect_tok
                FCB     ')'
                STX     fn_xsave                ; 引数直後の本文ポインタ
                STB     SUBCMD_P5               ; スイッチの控え ($D387)
                ; 座標の格納は引数を全て評価し終えてから行う。座標の式の中で
                ; 別の関数がサブシステムへコマンドを出すとステージングが
                ; 上書きされるため、先に積んでおくことはできない。
                PULS    A,B                     ; A = 垂直位置 / B = 水平位置
                EXG     A,B
                STD     SUBCMD_P1               ; X1 ($D383), Y1 ($D384)
                STD     SUBCMD_P3               ; X2 ($D385), Y2 ($D386)
                LDA     #$08                    ; Get Character Block 2
                JSR     sub_send_cmd_a
                LDB     SUBCMD_P3               ; $D385 = キャラクタコード
                BEQ     ifs_value               ; 表示が無い → 0
                TST     SUBCMD_P5
                BEQ     ifs_value               ; スイッチ 0 → キャラクタコード
                LDB     SUBCMD_P2               ; $D384 = 属性
                ANDB    #$0F                    ; パレットコード + 8 × リバース
ifs_value:
                JMP     int_to_fac_b    ; A=0 を畳んだ入口

;==============================================================================
; OPEN / CLOSE (キーボード・画面の 2 デバイスのみ)
;   OPEN・CLOSE の仕様 (参考書籍):
;     OPEN "モード",[#]ファイル番号,"デバイス記述子"
;     CLOSE [[#]ファイル番号[,[#]ファイル番号]…]
;   本 ROM が今受け持つのは "KYBD:" と "SCRN:" だけで、どちらも入出力の緩衝を
;   持たないデバイスである。開いたファイル番号は論理機番/物理機番対応テーブル
;   LOGPHY に記録する (1 件 1 バイト。bit4-0 = 物理機番、bit5 = 入力、
;   bit6 = 出力、0 = 未使用)。ファイル番号 n は LOGPHY+n を使う。
;   モード文字列は文字列式として受理するが、キーボードと画面はモードで振舞いが
;   変わらないので、入力・出力の両ビットを立てて記録する。
;   誤りは参考書籍のコードに従う:
;     50 Bad File Number    … ファイル番号が 1〜16 の外
;     52 File Already Open  … 開いているファイル番号をもう一度開いた
;     60 Device Unavailable … 本 ROM が受け持たないデバイス記述子
;   CLOSE は開いていないファイル番号を与えられても誤りにしない。
;   (置き場所は容量の都合。参照はラベル経由のみで位置依存はない)
;==============================================================================
impl_open:
                JSR     eval_x_str      ; "モード" (文字列でなければ Type Mismatch)
                JSR     impl_expect_tok
                FCB     ','
                BSR     fio_num         ; U = LOGPHY+ファイル番号
                PSHS    U               ; 記述子の評価で U が壊れるので退避
                JSR     impl_expect_tok
                FCB     ','
                JSR     eval_x_str      ; "デバイス記述子"
                BSR     fio_dev         ; B = LOGPHY への記録値 (受け持たない綴りは 60)
                PULS    U
                TST     ,U
                BNE     fio_e52         ; 既に開いている → File Already Open
                STB     ,U
                RTS

; CLOSE — 番号を並べた分だけ閉じる。番号を省略したら全て閉じる。
impl_close:
                JSR     skip_sp_tst     ; 空白飛ばし + A の 0 判定 (共通化)
                BEQ     icl_all         ; 行末 → 全て閉じる
                CMPA    #':'
                BNE     icl_lp          ; 文の区切り → 全て閉じる
icl_all:
                JMP     file_close_all
icl_lp:
                BSR     fio_num         ; U = LOGPHY+ファイル番号
                CLR     ,U              ; 開いていなくても誤りにしない
                JSR     chk_comma_x     ; 本文の現在位置が ',' か (共通化)
                BNE     icl_ret
                LEAX    1,X             ; ',' を消費して次の番号へ
                BRA     icl_lp
icl_ret:
                RTS

; fio_num — [#]ファイル番号 を読み、U = LOGPHY+番号 を返す。範囲外は 50。
;   入出力: X = 本文ポインタ (評価後の位置へ前進)。破壊: A,B,U
fio_num:
                JSR     skip_sp_x
                CMPA    #'#'            ; '#' は付けても付けなくてもよい
                BNE     fnm_nohash
                LEAX    1,X
fnm_nohash:
                JSR     eval_to_int_round
                TSTA                    ; 上位が残る値 (負・256 以上) は範囲外
                BNE     fio_e50
                DECB                    ; 1〜16 → 0〜15
                CMPB    #15
                BHI     fio_e50
                LDU     #LOGPHY+1
                LEAU    B,U
                RTS
fio_e50:
                LDB     #50             ; Bad File Number
                BRA     fio_errb

; fio_dev — 評価済みの文字列 (STRPTR/STRLEN) をデバイス記述子として分類する。
;   出力: B = LOGPHY への記録値 = 物理機番 (KYBD: = 0 / SCRN: = 1) に入力・
;         出力の両ビット ($60) を重ねた値。
;   受け持たない綴り (カセット・プリンタ・回線・バブル・ディスク) は 60。
;   見分けは長さ 5 と先頭 2 文字で行う。本 ROM が受け持たないデバイス記述子は
;   長さか先頭 2 文字のどちらかで必ず外れるので、この 2 つで足りる。
fio_dev:
                LDA     STRLEN
                CMPA    #5              ; "KYBD:" / "SCRN:" は 5 文字
                BNE     fio_e60
                LDU     STRPTR
                LDD     ,U              ; 先頭 2 文字で 2 つを見分ける
                CMPD    #$4B59          ; "KY"
                BEQ     fdv_kybd
                CMPD    #$5343          ; "SC"
                BNE     fio_e60
                LDB     #$61            ; SCRN: = 物理機番 1 + 入出力
                RTS
fdv_kybd:
                LDB     #$60            ; KYBD: = 物理機番 0 + 入出力
                RTS

                IFNDEF  STAGE_ALT_RAM
                ZMB     $DAEF-*
                ENDC
cursor_off:
                LDB     disp_state      ; $05A8 コンソールコントロール
                ANDB    #$FE            ; カーソル表示のビットを 0 にする
                BRA     scr_func_set
                IFNDEF  STAGE_ALT_RAM
                ZMB     $DAF6-*
                ENDC
cursor_on:
                LDB     disp_state
                IFNDEF  STAGE_ALT_RAM
                ZMB     $DAF9-*
                ENDC
scr_func_set:
                LDA     #$0C            ; CNSCTL (コンソール制御)
                STD     SUBCMD_CMD      ; cmd=$0C, パラメタ=B
                JMP     impl_sub_send_cmd       ; 末尾呼出
                ; 表示状態の初期化 + A=1 (区画の都合でプール) (ここへ展開)

;------------------------------------------------------------------------------
; fio_e52 / fio_e60 — OPEN / CLOSE の誤りの出口
;------------------------------------------------------------------------------
fio_e52:
                LDB     #52             ; File Already Open
                BRA     fio_errb
fio_e60:
                LDB     #60             ; Device Unavailable
fio_errb:
                JMP     impl_error_handler

; cmt_gap_out — ギャップ ($FF) を B バイト書き出す (区画の都合でこちらのプールへ
;   配置。位置依存は無い)。先導のギャップと末尾のエンドギャップが共用する。
;   カウンタは sv_len を借用する (どちらの呼出しもデータ段と排他)。壊す: A,B,X,Y
cmt_gap_out:
                STB     sv_len
cgo_lp:
                LDA     #$FF
                JSR     cmt_write_byte
                DEC     sv_len
                BNE     cgo_lp
                RTS

; kl_init2 — 一覧の開始時: 表示状態を「カーソルを伏せた控え」にして、
;   最初のキー番号 A=1 を返す。カーソルを伏せるのは、本実装のコンソールが
;   文字を書いた位置にカーソルの下線を残してしまい、出したまま一覧を流すと
;   一覧の文字が汚れるためである。伏せるのは一覧の間だけで、kl_fin が控えを
;   丸ごと書き戻すので、利用者が指定したカーソルの状態は一覧の後も残る。
kl_init2:
                BSR     cursor_off      ; カーソルだけ伏せる (他のビットは控えのまま)
                LDA     #1              ; 最初のキー番号
                RTS

; kl_hide — 割当て文字列を流す間だけ、さらに bit1 も落とす。
kl_hide:
                LDB     disp_state
                ANDB    #$FC            ; 動作実行 + カーソルを伏せる
                BRA     scr_func_set    ; 末尾呼出
                ; KEY LIST の第 3 断片 (別区画) (ここへ展開)
; kl_show — $2B の応答 (0 終端の割当て文字列) を出力し、bit1 を戻す。
;   カーソルは一覧が終わるまで伏せたままにする。
kl_show:
                LDX     #SUBCMD_P3
                JSR     impl_print_str
                BRA     cursor_off      ; 動作実行だけ戻す (末尾呼出)

; kl_fin — 一覧の後始末: 1 行送り、表示状態を控えへ丸ごと戻す。
;   ここでカーソル表示のビットが利用者の指定どおりに復活する。
kl_fin:
                JSR     disk_crlf
                BRA     cursor_on       ; 末尾呼出

; RANDOMIZE
impl_randomize:
                LDA     ,X
                BEQ     iran_no
                CMPA    #':'
                BEQ     iran_no
                JSR     eval_to_int    ; 式評価→整数化 (共通化)
                JMP     pool_iran_st    ; 格納実体は 7t_deffn.inc の空き区画
iran_no:
                ; 式の省略時は種の値 (-32768〜32767) を問い合わせて入力を待つ。
                JMP     pool_iran_ask

; BEEP は本体 impl_beep_snd を分岐表から直接指す (中継 JMP は置かない)。

; LINE
; 構文: LINE [@] [(x1,y1)]-(x2,y2), {機能|文字列} [,パレットコード [,{B|BF}]]
; subsys $15 LINE パラメータ:
;   $D383 = CL (カラーコード)
;   $D384 = F  (ファンクションコード)
;   $D385-$D386 = X1 (16bit)
;   $D387-$D388 = Y1 (16bit)
;   $D389-$D38A = X2 (16bit)
;   $D38B-$D38C = Y2 (16bit)
;   $D38D = B  (0=直線, 1=矩形枠, 2=矩形塗りつぶし)
impl_line:
                ; 既定は subsys $15 (グラフィック直線)。文字ライン形式を見つけた
                ; ときだけ $20 (文字ライン) へ差し替える。
                LDA     #$15
                STA     SUBCMD_CMD
                ; '@' フラグ確認 (グラフィックカーソル継続)
                ; 本文は命令の直後の空白をそのまま持つので、'@'/'(' の判定の前に
                ;   空白を読み飛ばす (読み飛ばさないと座標指定が gcur 省略の
                ;   経路へ入る)。
                JSR     skip_sp_x
                ; LINE INPUT 文: 命令トークン LINE の後に関数トークン
                ;   ($FF xx) が続く形。本体は空きプールへ。
                CMPA    #TOK_FUNC_PFX
                LBEQ    pool_line_input
                CMPA    #'@'
                BNE     iln_no_at
                LEAX    1,X
iln_no_at:
                ; (x1,y1) 省略チェック
                JSR     skip_sp_lparen
                BNE     iln_use_gcur
                ; (x1,y1) 指定あり
                JSR     impl_parse_xy_cmd   ; tmpX16, tmpY16 に (x1,y1)
                BRA     iln_got_p1
iln_use_gcur:
                ; 省略時は gcur_x/y を使う
                LDD     gcur_x
                STD     tmpX16
                LDD     gcur_y
                STD     tmpY16
iln_got_p1:
                ; '-' を読み飛ばす (ASCII '-' $2D / トークン化 '-' $DA の両方を許容)
                ; プログラムを LOAD すると '-' は $DA へトークン化されるため、
                ; $2D のみ判定すると LINE(x1,y1)-(x2,y2) の第2座標が解析されず
                ; 文が途中で打ち切られ Syntax Error になる。
                JSR     skip_sp_take
                CMPA    #'-'
                BEQ     iln_p2
                CMPA    #TOK_MINUS
                BNE     iln_err
iln_p2:
                ; (x2,y2) を解析
                JSR     impl_parse_xy_cmd2  ; tmpX16_2, tmpY16_2 に (x2,y2)
iln_err:
                ; ',' 機能/パレット
                ; (既定値の設定は分岐先の iln_no_func が行うため、ここには置かない)
                JSR     skip_comma
                BCC     iln_no_func
                ; 機能コード or 文字列 (形式1はスタブ)
                JSR     skip_sp_x           ; ',' と機能/文字列の間の空白を許容
                CMPA    #'"'
                BEQ     iln_str             ; 文字ライン形式
                JSR     impl_parse_func     ; B = ファンクションコード
                BRA     iln_col
iln_str:
                ; 文字ライン形式 LINE (x1,y1)-(x2,y2),"文字"[,色][,B|BF]。
                ; 引く文字を機能コードの位置に載せ、コマンドを $20 へ差し替える。
                ; 座標・色・B/BF の並びは $15 と同一なので以降の経路を共用する。
                LEAX    1,X                 ; 開き引用符を消費
                LDB     ,X                  ; B = 引く文字
                LDA     #$20
                STA     SUBCMD_CMD
iln_str_lp:
                LDA     ,X+
                CMPA    #'"'
                BNE     iln_str_lp
iln_col:
                PSHS    B                   ; 機能/文字を退避 (式評価が B を壊すため)
                ; パレットコード (省略時 = 現在の描画色)。省略判定と評価は
                ; opt_int_a に共通化。既定値 A を温存し空白も読み飛ばす。
                LDA     cur_fg_color
                BSR     opt_int_a
                PULS    B                   ; 機能/文字を復帰
                ; A=カラー, B=機能または文字 の状態
                BRA     iln_check_b
iln_no_func:
                LDA     cur_fg_color
                CLRB                        ; = FUNC_PSET (0)
iln_check_b:
                ; A=カラー, B=機能 の状態で B/BF チェック
                PSHS    A,B                 ; カラー・機能保存
                ; B/BF チェック
                JSR     skip_comma
                BCC     iln_no_bflag
                JSR     skip_sp_x           ; ',' と B/BF の間の空白を許容
                CMPA    #'B'
                BNE     iln_no_bflag
                LEAX    1,X
                LDB     ,X
                LDA     #LINE_BOX
                CMPB    #'F'
                BNE     iln_bflag_done      ; 'B' のみ = 矩形枠
                LEAX    1,X
                INCA                        ; 'BF' = 矩形塗潰し (BOX+1)
                BRA     iln_bflag_done
iln_no_bflag:
                CLRA                        ; = LINE_LINE (0)
iln_bflag_done:
                ; A = B/BF フラグ
iln_send:
                STA     SUBCMD_P11          ; $D38D = B フラグ
                ; コマンドコードは入口で確定済 ($15 直線 / $20 文字ライン)
                PULS    A,B                 ; A=カラー, B=機能または文字
                STD     SUBCMD_P1           ; CL と F
                JSR     subcmd_xy1
                LDD     tmpX16_2
                STD     SUBCMD_P7           ; $D389-$D38A = X2
                STD     gcur_x              ; gcur 更新 (D を温存して併記)
                LDD     tmpY16_2
                STD     SUBCMD_P9           ; $D38B-$D38C = Y2
                STD     gcur_y
                JMP     impl_sub_send_cmd

; 省略可能な数値引数の共通解析 (共有化)
;   入力: A = 省略時の既定値。X の位置 (空白は読み飛ばす) に ',' が続けば
;   式を評価して A = 値 (下位バイト)。無ければ A を保持して返す。
opt_int_a:
                PSHS    A
                JSR     skip_sp_x           ; ',' 前の空白を許容 (A を壊すため退避)
                PULS    A
                LDB     ,X
                CMPB    #','
                BNE     oia_rts
                LEAX    1,X
                ; 空の位置引数 ",," は既定値のまま桁だけ送る。ここを見ないと
                ; 空引数を式として評価して値 0 を得てしまい、
                ; CIRCLE ...,r,,縦横比,... のパレットコードが 0 (背景色) に
                ; 化けて円弧が消える。
                LDB     ,X
                CMPB    #','
                BEQ     oia_rts
                ; 本ヘルパの呼び手は LINE / CIRCLE / SYMBOL のパレットコードと
                ;   SYMBOL の角度コード。いずれも最近接丸め (動作観察)。
                ; 値域はパレットコードが 0..15。角度コードはさらに狭い 0..3 で、
                ;   呼び手側の opt_int_a3 が絞る。範囲外は Illegal Function Call。
                JSR     eval_to_int_15      ; 丸め + 値域検査 (0..15)
                TFR     B,A
oia_rts:
                RTS

; CIRCLE
; 構文: CIRCLE (x,y), 半径 [,パレットコード [,縦横比 [,開始位置 [,終了位置]]]]
;   (引数順は色→縦横比→開始位置→終了位置。
;    開始/終了位置は円周に対する割合 0〜1 で、64 分割の頂点番号へ最近接丸め
;    される (0 = X 軸正方向、0.25 = 画面の真下、下回りに増加)。頂点番号
;    ks/ke は ke<=ks で一周またぎとし、頂点 ks..ke を結ぶ ke-ks 本の線分を
;    描く (終端頂点まで引く。省略時は ks=ke=0 → 全周 64 本で多角形が閉じる)。
;    丸め結果が 0..255 の外 (負含む) は Illegal Function Call、
;    ±32768 以上は Overflow。値は 64 頂点リングへ &63 で折返す。
;    縦横比は既定 0.4495 (参考書籍)。空引数 ",," は既定扱い。値は縦大きさへの
;    乗算で、負値は 16bit 符号なしの大きさとして扱われ、クランプで加算方向
;    は下限/上限へ張り付く (負半径と同じ規則))
; 実装方式:
;   - 頂点 k: X = clamp(CX ± trunc(R*|cos(2*pi*k/64)|))、
;     Y = clamp(CY ± trunc(trunc(R*|sin(2*pi*k/64)|) * 縦横比))
;     (trunc はゼロ方向切り捨て。縦横比はクランプ有無に関わらず常に適用)
;   - 縦横比は 1/256 単位へ切り捨て量子化してから乗算する (動作観察)
;   - clamp は 0..639 / 0..199。加算あふれ/減算借り (負半径・負縦横比で発生)
;     は上限/下限へ張り付く (符号なしクランプ)
;   - 隣接頂点を subsys $15 (直線) で結ぶ (全周は閉じず、最終参照点も
;     動かさない)。円弧は開始頂点から線分本数ぶんだけ結ぶ
;   - 大きさは第 1 象限 17 点のみ実計算し、残り象限は対称で折り返す
; 作業域:
;   - 大きさ表 CIR_MAG/CIR_RMAG (68B) はディスク ASCII 読取のセクタ緩衝
;     ($0640-$073F) の後半 $0690-$06D3 に置く。同居相手は本系統だけが触る
;     緩衝に限る (サブ CPU の挙動を前提にしない)。時間排他の根拠:
;     セクタ緩衝を使うのはディスク ASCII 読取だけで、その間 BASIC の文は 1 つも
;     実行されない (読み取ったセクタを本文へ詰め直すだけで実行ループへ戻らない)。
;     逆に大きさ表が生きるのは CIRCLE 文 1 回の内側だけである。空白文字列の
;     出力緩衝 ($0640-$068F, 80B) ともアドレスが重ならない
;   - 縦横比 cir_asp と塗りの退避先 cfl_dst は専有域 $0753-$075A に置く。
;   - 小物ワークは ZP 安全域の式評価用一時 (arr_mul_a-) を流用する。
;     描画エンジンは式評価を呼ばないため排他。ただし引数解析の途中から生きる
;     値 (半径・開始頂点) は、後続引数の式評価が同じ ZP 枠 (STRPTR / 配列添字の
;     乗算ワーク) を書き潰すため、全引数の評価が終わるまでスタックで運んでから
;     格納する
CIR_MAG         EQU     str_work_buf+$50           ; 大きさ表 17 ワード (2 バイト) ($0690-$06B1)
                                        ;    CIR_MAG+34、cir_magst が 34,X で参照)
                IFDEF   STAGE_ALT_RAM
CIR_K           EQU     STAGE_EXPR+0
                ELSE
CIR_K           EQU     $0002           ; 現頂点番号 (1B, arr_mul_a と排他流用)
                ENDC
                IFDEF   STAGE_ALT_RAM
CIR_SGN         EQU     STAGE_EXPR+1
                ELSE
CIR_SGN         EQU     $0003           ; 符号フラグ (1B, 非零=負方向)
                ENDC
                                        ;   (LDD が B を潰すため CC/レジスタでは
                                        ;    運べない。cir_pm の直前に必ず設定)
                IFDEF   STAGE_ALT_RAM
CIR_J           EQU     STAGE_EXPR+3
                ELSE
CIR_J           EQU     $0005           ; 前計算の添字 (1B)
                ENDC
                IFDEF   STAGE_ALT_RAM
CIR_MYR         EQU     STAGE_EXPR+6
                ELSE
CIR_MYR         EQU     $0008           ; 縦大きさ 縦横比適用済 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
CIR_R           EQU     STAGE_EXPR+8
                ELSE
CIR_R           EQU     $000A           ; 半径 (2B)
                ENDC
                IFDEF   STAGE_ALT_RAM
CIR_CNT         EQU     STAGE_EXPR+10
                ELSE
CIR_CNT         EQU     $000C           ; 残線分本数 (1B, $000A-$000D は GET/PUT
                ENDC
                                        ;   作業と排他共用の未割当域)
                IFDEF   STAGE_ALT_RAM
cir_asp         EQU     STAGE_BUFFER+122
                ELSE
cir_asp         EQU     $05B7           ; 縦横比 (6B 展開形, $0753-$0758, 専有域)。
                ENDC
                                        ;   格納は全引数の式評価が終わってから行う。
                                        ;   解析中は CPU スタックの枠で運ぶ (解析の
                                        ;   途中でステージング内のアドレスへ置くと、
                                        ;   開始/終了位置の式がサブ CPU との往復を含むとき
                                        ;   128B 読み戻しに晒されるため)
; --- 塗りつぶし指定 (第 7 引数 F) の作業域 ---
;   走行は cir_draw の内側でのみ生きる。ZP 安全域のうち CIRCLE が
;   使っていない枠を借りる (GET/PUT・配列名・乗算スクラッチは引数解析が
;   終わった時点で死んでおり、描画エンジンは式評価を呼ばないため排他)。
                IFDEF   STAGE_ALT_RAM
cfl_dst         EQU     STAGE_BUFFER+53
                ELSE
cfl_dst         EQU     $0572           ; 現頂点の退避先 (2B, $0759-$075A, 専有域)。
                ENDC
                                        ;   上位バイト 0 = 塗り (CFL_AX)、
                                        ;   非 0 = 輪郭 (SUBCMD_P3) の判別も兼ねる
; CFL_* (塗りつぶし作業域) の EQU は 7t_defs.inc へ置く
;   (この取り込みより前の 7t_deffn.inc から参照するため。
;    先に定義しておかないと直接ページ指定にならず 1 バイト太る)

impl_circle:
                JSR     impl_parse_xy_cmd   ; 中心 (x,y) → tmpX16, tmpY16
                JSR     skip_comma
                BCC     oia_rts
                JSR     eval_to_addr   ; 半径はアドレスと同じ規約 (-65536..65535)
                PSHS    A,B                 ; 半径は全引数の式評価が終わるまで
                                            ;   スタックで運ぶ。CIR_R ($000A) は後続
                                            ;   引数の式評価が書く文字列レジスタ
                                            ;   STRPTR ($0009-$000A) と重なるため、
                                            ;   評価の完了前に格納してはならない
                ; パレットコード (省略時 = 現在の描画色)
                LDA     cur_fg_color
                BSR     opt_int_a
                STA     SUBCMD_P1           ; CL
                ; --- 縦横比 (省略・空 = 既定 0.4495) ---
                JSR     skip_comma
                BCC     ica_adfl
                JSR     skip_sp_comma
                BEQ     ica_adfl            ; 空 ",," → 既定 (',' は消費済)
                CMPA    #':'
                BEQ     ica_adfl
                TSTA
                BEQ     ica_adfl            ; 行末 → 既定
                JSR     impl_eval_expr
                JSR     circle_unit_ensure  ; 0〜1 を検査して単精度へ揃える
                PSHS    X
                BRA     ica_ast
ica_adfl:
                PSHS    X
                LDX     #c_04495
                JSR     fx_ld1              ; FAC1 = 0.4495 の量子化値
ica_ast:
                ; 縦横比は 1/256 単位へ切り捨て量子化する (×256 の整数化=ゼロ方向、
                ; 256 で戻す)。既定値も同じ経路を通る (量子化済み定数ゆえ不変)。
                LDA     FAC1
                BEQ     ica_aq              ; 0 は量子化不要
                ADDA    #8                  ; ×256 (指数 +8)
                STA     FAC1
                ; 検査済みの比率は 0〜1。×256 を切り捨て、同じ量子化を保つ。
                JSR     impl_fac_to_addr    ; D = trunc(縦横比×256) / 範囲外は誤り 5
                JSR     impl_int_to_fac
                JSR     impl_int_to_float
                LDA     FAC1
                BEQ     ica_aq
                SUBA    #8                  ; ÷256 (指数 -8)
                STA     FAC1
ica_aq:
                LEAS    -6,S                ; 縦横比も CPU スタックの枠で運ぶ。
                TFR     S,X                 ;   本文ポインタの控えは枠のぶん 6 バイト
                JSR     fx_st1              ;   奥へ動くので 6,S から取り直す
                LDX     6,S
                ; --- 開始位置 → 開始頂点 CIR_K / 終了位置 → 線分本数 ---
                JSR     cir_angle
                PSHS    B                   ; ks もスタックで運ぶ。CIR_K ($0002) は
                                            ;   終了位置の式が配列添字を含むときに
                                            ;   乗算ワーク (arr_mul_a) で潰される
                JSR     cir_angle           ; B = ke
                SUBB    ,S
                BGT     ica_kd
                ADDB    #64                 ; ke<=ks は一周またぎ (動作観察)
ica_kd:
                STB     CIR_CNT             ; 線分本数 = ke-ks (1..64。終端頂点
                                            ;   まで引く。全周 64 本で閉じる)
                ; --- ここで全引数の式評価が完了。運んできた値を作業域へ移す ---
                PULS    A
                STA     CIR_K               ; ks = 描画開始頂点
                LDU     #cir_asp            ; 縦横比を枠から作業域へ (以後 cir_premag
                LDB     #6                  ;   まで式評価は 1 度も走らない)
ica_acp:
                LDA     ,S+
                STA     ,U+
                DECB
                BNE     ica_acp
                LEAS    2,S                 ; 本文ポインタの控えは用済み。最新の位置
                                            ;   (開始/終了位置を読み終えた位置) は X に
                                            ;   載っており、控えを載せ直すと後続の
                                            ;   塗り指定 ",F" を読み落とす
                PULS    A,B
                STD     CIR_R               ; 半径 (符号なしクランプは cir_clamp)
                JSR     cir_fill_arg        ; 塗りつぶし指定 (F/N) を解析し型を決める
                ; --- 第 8 引数 = 機能 (PSET/PRESET/AND/OR/XOR。省略時 PSET) ---
                ; 機能は PSET / PRESET / AND / OR / XOR で、省略時は PSET。F (塗り)
                ; を指定したときも、機能は線を引くときに効く (参考書籍)。
                ; 円弧・扇形は cir_draw が頂点ごとに $15 直線を発行するので、
                ; 機能コードを SUBCMD_P2 へ載せればそのまま合成に効く。
                CLRB                        ; 省略時 = FUNC_PSET (0)
                JSR     skip_comma
                BCC     ica_fn
                JSR     skip_sp_x           ; ',' と機能語の間の空白を許容
                JSR     impl_parse_func     ; B = 機能コード
ica_fn:
                STB     SUBCMD_P2           ; F = 機能
                PSHS    X                   ; 本文ポインタを保全
                CLR     SUBCMD_P11          ; 形状 = 直線
                BSR     cir_premag
                BSR     cir_draw
                PULS    X
                ; 綴りの読み残しがあると、その綴りが次の文として実行されてしまう
                ; (引数の無い文として走り、Missing Operand で止まる)。
                JMP     impl_stub           ; ':'/$00 まで X を進めて復帰

; (開始/終了位置引数を頂点番号へ変換する cir_angle は、容量の都合で
;  7t_startup.inc の空きプールに置く。参照は impl_circle からの JSR のみで
;  位置依存はない)

; 第 1 象限の大きさ表を前計算する。
;   CIR_MAG[j]  = trunc(R * sin(j*2*pi/64))  (j=0..15。[16] = R そのもの)
;   CIR_RMAG[j] = trunc(CIR_MAG[j] * 縦横比)
; sin は fx_sin_core (単精度)。R の単精度値は fsc_u に保持する
; (fx_sin_core は fsc_x/fsc_r/fsc_s/fsc_flag のみ使用し fsc_u は保存される)
cir_premag:
                LDD     CIR_R
                BSR     cir_d2f
                JSR     fx_st1_fsc_u
                CLR     CIR_J               ; j=0 から (sin(0)=0 で表零詰め兼用)
cpg_lp:
                ; 頂点角は j*2*pi/64 (j=0..15) の固定 16 値のみなので、
                ; sin コア (fx_sin_core) の出力ビット列を ROM 定数表
                ; c_sjtab9/c_sjtab7 に持ち、乗算入力を表から読む。
                ; fmul_core へ渡る FAC1 のビット列は sin コアの出力と同じなので
                ; CIR_MAG/CIR_RMAG (= 画素) は計算した場合と変わらない
                LDA     CIR_J
                LDX     #c_sjtab9           ; j=0..8
                CMPA    #9
                BLO     cpg_tb
                SUBA    #9
                LDX     #c_sjtab7           ; j=9..15 (容量均衡のため分置)
cpg_tb:
                LDB     #6
                MUL                         ; B = 表内オフセット (6B/項, 最大 48)
                ABX
                JSR     fx_ld1              ; FAC1 = S_j (ガード 0・FACTYPE=1)
                LDX     #fsc_u
                JSR     fx_ld2mul       ; FAC1 = R * sin
                JSR     float_to_int_trunc  ; D = trunc (ゼロ方向)
                BSR     cir_magst
                INC     CIR_J
                LDA     CIR_J
                CMPA    #16
                BNE     cpg_lp
                LDD     CIR_R               ; j=16 は cos 側の端点 = R そのもの
                ; (そのまま cir_magst へ落下し、格納して RTS)

; D = 大きさ → CIR_MAG[CIR_J]、縦横比適用値 → CIR_RMAG[CIR_J]
cir_magst:
                TFR     D,U                 ; 索引計算の LDB/ASLB は D の下位
                LDB     CIR_J               ;   (=B) を潰すため、値は U に退避する
                ASLB
                LDX     #CIR_MAG
                ABX
                STU     ,X
                PSHS    X
                TFR     U,D
                BSR     cir_d2f
                LDX     #cir_asp
                JSR     fx_ld2mul       ; FAC1 = 大きさ * 縦横比 (既定 0.4495)
                JSR     float_to_int_trunc
                TFR     D,U
                PULS    X
                STU     34,X                ; CIR_RMAG = CIR_MAG+34
                RTS

; D (符号付き 16bit) を単精度 FAC1 へ
cir_d2f:
                JSR     impl_int_to_fac
                JMP     impl_int_to_float

; CIR_CNT (1..64) 本の線分を頂点 CIR_K から $15 で送る。現頂点は SUBCMD_P7/P9
; (終点) に生成し、送信後に SUBCMD_P3/P5 (始点) へ移す。終端頂点 ke まで引く
; (全周は 64 本で閉じる。動作観察)。
cir_draw:
                BSR     cir_vertex          ; 開始頂点 CIR_K → P7/P9
cdr_lp:
                LDX     cfl_dst             ; 現頂点の退避先 (輪郭=始点欄 / 塗り=走査元)
                LDD     SUBCMD_P7
                STD     ,X
                LDD     SUBCMD_P9
                STD     2,X
                INC     CIR_K               ; (最大 126。cir_vertex は bit0-5 のみ
                                            ;  参照するので mod 64 でリングを回る)
                BSR     cir_vertex
                LDA     #$15
                STA     SUBCMD_CMD
                TST     cfl_dst             ; 上位バイト 0 = 塗り指定あり
                BNE     cdr_ns
                JSR     cir_fill_edge       ; 辺の途中画素を先に送る (復帰時 P7/P9 = 終端頂点)
cdr_ns:
                DEC     CIR_CNT
                BEQ     cdr_last
                JSR     impl_sub_send_draw  ; 中間線分: 軽量送出
                BRA     cdr_lp
cdr_last:
                JMP     impl_sub_send_cmd   ; 最終線分のみ完全同期 = 後続コマンド
                                            ;   との直列化を保証 (末尾呼び)

; CIR_K の頂点座標を計算し SUBCMD_P7 (X) / SUBCMD_P9 (Y) へ格納する。
; 象限対称: idx = k&15 (第 2・4 象限は 16-idx)。
;   横大きさ = CIR_MAG[16-idx] (= trunc(R*|cos|))、
;   縦大きさ = CIR_RMAG[idx] (縦横比適用済。縦横比はクランプ有無に
;   関わらず常に適用し、スケール後の値を符号なしクランプする。動作観察)
;   横符号: ((k+16) & 32) 非零で負 / 縦符号: (k & 32) 非零で負
;   (k は bit0-5 のみ参照 = mod 64。円弧の一周またぎで 64 以上になり得る)
cir_vertex:
                LDB     CIR_K
                TFR     B,A
                ANDA    #$0F
                BITB    #$10
                BEQ     cvx_i
                NEGA
                ADDA    #16                 ; 奇数象限は添字を反転
cvx_i:
                ASLA                        ; A = 2*idx (0..32)
                LDX     #CIR_MAG+34         ; = CIR_RMAG
                LDU     A,X
                STU     CIR_MYR             ; 縦大きさ (縦横比適用済)
                NEGA                        ; A = -2*idx (符号付きオフセット)
                LEAX    -2,X                ; = CIR_MAG+32
                LDU     A,X                 ; U = 横大きさ = CIR_MAG[16-idx]
                ; X 座標 = CX ± 横大きさ → 0..639 へクランプ
                ADDB    #16
                ANDB    #$20                ; 非零 = 負方向
                STB     CIR_SGN
                LDD     tmpX16
                BSR     cir_pm
                LDX     #639
                BSR     cir_clamp
                STD     SUBCMD_P7
                ; Y 座標 = CY ± 縦大きさ → 0..199 へクランプ
                LDU     CIR_MYR
                BSR     cir_yc
                STD     SUBCMD_P9
                RTS

; U = 縦大きさ → D = clamp(CY ± U, 0..199)
cir_yc:
                LDB     CIR_K
                ANDB    #$20
                STB     CIR_SGN
                LDD     tmpY16
                BSR     cir_pm
                LDX     #199
                BRA     cir_clamp           ; (末尾呼び)

; D = D ± U (CIR_SGN 非零なら減算)
cir_pm:
                PSHS    U
                TST     CIR_SGN
                BNE     cpm_n
                ADDD    ,S++
                RTS
cpm_n:
                SUBD    ,S++
                RTS

; D を 0..X へクランプする。
; 入場時の C フラグ = 直前の cir_pm の ADDD あふれ / SUBD 借り。
; 大きさが 16bit 符号なしの大きな値 (負半径・負縦横比の類) のとき、加算方向は
; 上限へ・減算方向は下限へ張り付く。これを C 判定で行う。
; (BSR/LDX は C を変えないので cir_pm から C はそのまま届く)
cir_clamp:
                BCS     ccl_wrap
                PSHS    X
                CMPD    ,S++
                BLS     ccl_ok
ccl_max:
                TFR     X,D
                RTS
ccl_wrap:
                TST     CIR_SGN
                BEQ     ccl_max             ; 加算方向 → 上限
                LDD     #0                  ; 減算方向 → 下限
ccl_ok:
                RTS

; 定数 (単精度パック形: 指数, 仮数 4B, 符号)
c_04495:        FCB     $7E,$E6,$00,$00,$00,$00 ; 縦横比の既定値 0.4495 (参考書籍) を
                                                ; 1/256 単位に切り捨てた値

; PAINT: 本体は容量の都合で 7t_deffn.inc 末尾の空きプールに置く
;   (cmd_jt からの FDB 参照のみで内部参照は無い)

; CONNECT
; 構文: CONNECT (x1,y1)-(x2,y2)[-(x3,y3)...] [,パレットコード [,機能]]
; subsys $16 CHAIN パラメータ:
;   $D383 = CL
;   $D384 = F
;   $D385 = N (座標点数 2-30)
;   $D386-$D387 = X1, $D388-$D389 = Y1, $D38A-$D38B = X2...
;   X は本文ポインタ専用に保ち、格納先は毎回 B 索引で求める (JSR を跨いで
;   保持するレジスタを作らない)。多点 (-(x3,y3)...) を受け付ける。
impl_connect:
                CLR     SUBCMD_P3           ; N = 0
icon_pt:
                JSR     impl_parse_xy_cmd   ; (x,y) → tmpX16/tmpY16 (X は本文を前進)
                LDB     SUBCMD_P3
                CMPB    #30
                BLO     icon_st
                LDB     #29                 ; 31 点目以降は最終スロットへ上書き
                STB     SUBCMD_P3           ;   (共有 RAM の容量 = 最大 30 点。
                                            ;    後の INC で N=30 に戻る)
icon_st:
                ASLB
                ASLB                        ; B = 格納オフセット (4B/点, 最大 116)
                LDU     #SUBCMD_P4
                LEAU    B,U
                LDD     tmpX16
                STD     ,U++
                LDD     tmpY16
                STD     ,U
                INC     SUBCMD_P3
                ; '-' 継続判定 (ASCII $2D / トークン化 $DA の両方を許容。LINE と同じ)
                JSR     skip_sp_x
                CMPA    #'-'
                BEQ     icon_nx
                CMPA    #TOK_MINUS
                BNE     icon_opt
icon_nx:
                LEAX    1,X
                BRA     icon_pt
icon_opt:
                ; [,パレットコード [,機能]] (既定: 現在の描画色 / PSET)。
                ; CONNECT は最終参照点を動かさないので、gcur_x/gcur_y は
                ; 更新しない。
                LDA     cur_fg_color
                JSR     pool_gr_opts        ; A = CL, B = F
                STD     SUBCMD_P1           ; CL と F
                LDA     #$16
                JMP     sub_send_cmd_a          ; A = コマンド番号のまま発行

; SYMBOL
; 構文: SYMBOL (x,y), 文字列, 横倍率, 縦倍率 [,パレットコード [,角度コード [,機能]]]
; subsys $19 SYMBOL パラメータ:
;   $D383 = CL
;   $D384 = F
;   $D385 = A (角度)
;   $D386 = WM (横倍率)
;   $D387 = HM (縦倍率)
;   $D388-$D389 = IX (X, 16bit)
;   $D38A-$D38B = IY (Y, 16bit)
;   $D38C = N (文字数)
;   $D38D以降 = 文字列
impl_symbol:
                ; (x,y) を解析
                JSR     impl_parse_xy_cmd   ; tmpX16, tmpY16
                JSR     skip_sp_take
                CMPA    #','
                BNE    isym_done
                ; 文字列式を評価して sym_buf へ複写
                ; (リテラルに加え文字列変数・連結式 "..."+CHR$() 等に対応)
isym_str:                                   ; GCURSOR の文字列描画の形から入る入口
                                            ;   ((x,y) は解析済、X = ',' の直後)
                JSR     sym_eval_str        ; C=0:B=文字数/sym_buf, C=1:非文字列
                BCS    isym_done           ; 非文字列は描画せず安全終了
                PSHS    B                   ; 文字数保存
                ; 横倍率 (最近接丸め。動作観察)
                JSR     eval_to_int_255_ac
                PSHS    B                   ; WM
                ; 縦倍率
                JSR     eval_to_int_255_ac
                PSHS    B                   ; HM
                ; パレットコード (省略可)。既定は現在の描画色。
                ;   (省略判定と評価は opt_int_a に共通化。既定値 A を温存する)
                LDA     cur_fg_color
                JSR     opt_int_a
                PSHS    A                   ; CL
                ; 角度コード (省略可)。既定 0。
                CLRA
                ; 丸め + 値域検査 (0..3) (ここへ展開)
;------------------------------------------------------------------------------
; opt_int_a3 — 省略可能引数のうち値域が 0..3 のもの (SYMBOL の角度コード)。
;   共通解析 opt_int_a は色と同じ 0..15 の検査を通すので、その後で 3 まで
;   絞る。省略時の既定値 0 は当然この範囲に収まる。
;   (動作観察)
;------------------------------------------------------------------------------
opt_int_a3:
                JSR     opt_int_a
                CMPA    #3
                LBHI    errc_5
                PSHS    A                   ; 角度
                ; 機能 (省略可)。既定 PSET を B へ (省略時 B がそのまま機能になる)。
                CLRB                    ; FUNC_PSET = 0
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     isym_nofunc
                LEAX    1,X
                JSR     impl_parse_func
isym_nofunc:
                ; B = 機能
                ; スタック: B=機能, [角度], [CL], [HM], [WM], [文字数]
                ; subsys $19 コマンド設定 (B=機能を持ったまま設定し
                ;   退避/復帰を省く。隣接語 P4/P5 は PULS A,B + EXG で一括格納)
                LDA     #$19
                STA     SUBCMD_CMD
                STB     SUBCMD_P2           ; F=$D384 (機能)
                PULS    A                   ; 角度
                STA     SUBCMD_P3           ; A=$D385
                PULS    A                   ; CL (カラー)
                STA     SUBCMD_P1           ; CL=$D383
                PULS    A,B                 ; A=HM, B=WM
                EXG     A,B                 ; A=WM, B=HM
                STD     SUBCMD_P4           ; WM=$D386, HM=$D387
                LDD     tmpX16
                STD     SUBCMD_P6           ; IX=$D388-$D389
                LDD     tmpY16
                STD     SUBCMD_P8           ; IY=$D38A-$D38B
                PULS    B                   ; 文字数
                STB     SUBCMD_P10          ; N=$D38C
                ; 文字列を $D38D 以降にコピー: sym_buf[0..N-1] → SUBCMD_P11+
                ; X は本文ポインタ。コピーで X を作業に使うため退避し、コピー後に
                ; 必ず復元する (復元しないとディスパッチャが X=サブシステムアドレス
                ; から実行を続ける)。
                ; (直前の STB SUBCMD_P10 が立てた Z で判定する)
                BEQ     isym_done           ; 0文字ならスキップ
                PSHS    X                   ; 本文ポインタ退避
                LDU     #sym_buf
                LDX     #SUBCMD_P11         ; $D38D
isym_copy:
                LDA     ,U+
                STA     ,X+
                DECB
                BNE     isym_copy
                PULS    X                   ; 本文ポインタ復元
                JSR     impl_sub_send_cmd
isym_done:
                RTS

;------------------------------------------------------------------------------
; impl_gcursor — GCURSOR (x,y),(変数,変数)[,(変数,変数)]…[,パレットコード]
;
;   (x,y) = カーソルの初期座標、(変数,変数) = 読み取った座標の受け皿 (10 組まで)、
;   パレットコードの既定値 = 現在の描画色。
;
;   対話そのものはサブシステムのコマンド $1F (GRAPHIC CURSOR) が行う。BASIC 側は
;   組数と表示色と初期座標を組み立てて発行し、応答 (1 点 4 byte、X 座標・Y 座標の
;   順に $D383 から並ぶ) を変数へ代入する。
;   本文は 2 度走査する。$1F は組数を先に要求するのに対しパレットコードは並びの
;   末尾に来るため、1 度目で組数と表示色を確定し、発行後の 2 度目で同じ並びを
;   もう一度辿って代入する。
;------------------------------------------------------------------------------
impl_gcursor:
                JSR     impl_parse_xy_cmd   ; 初期座標 → tmpX16 / tmpY16
                PSHS    X                   ; 変数の並びの先頭 (2 度目の走査で戻す)
                CLR     SUBCMD_P2           ; 組数 N = 0
                LDA     cur_fg_color        ; 省略時のカーソル表示色
                STA     SUBCMD_P1
                ; (x,y) の次は座標の組の丸括弧でなければならない
                ;   (形式はこの 1 つだけ。参考書籍)。
                JSR     skip_comma
                BCC     gcu_syn             ; 引数が続かない → Syntax Error
                JSR     skip_sp_lparen
                BEQ     gcu_skip            ; 座標の組 → 以降は組を数える
                BRA     gcu_syn             ; それ以外 → Syntax Error
gcu_scan:
                JSR     skip_comma
                BCC     gcu_send
                JSR     skip_sp_lparen
                BNE     gcu_pal             ; '(' でない → パレットコード
gcu_skip:
                ; 1 度目は組を数えるだけ。組の中の変数が配列の要素なら添字の
                ;   丸括弧が入れ子になるので、深さを数えて対応する ')' で閉じる
                ;   (最初の ')' で切ると、組の途中を次の引数と読み違える)。
                CLRB                        ; 丸括弧の深さ
gcu_sk1:
                LDA     ,X+
                BEQ     gcu_syn             ; 閉じないまま行末 → Syntax Error
                CMPA    #'('
                BNE     gcu_sk2
                INCB
                BRA     gcu_sk1
gcu_sk2:
                CMPA    #')'
                BNE     gcu_sk1
                DECB
                BNE     gcu_sk1
                INC     SUBCMD_P2
                BRA     gcu_scan
gcu_pal:
                JSR     eval_to_int_15      ; パレットコード (丸め + 値域 0..15)
                STB     SUBCMD_P1
                BRA     gcu_scan
gcu_syn:
                JMP     errc_2              ; Syntax Error (控えは warm_start が捨てる)
gcu_send:
                LDA     SUBCMD_P2           ; (0 組は入口の振り分けで弾いてある)
                CMPA    #10
                BHI     gcu_syn             ; 10 組まで (参考書籍)
                JSR     subcmd_xy1
                LDA     #$1F                ; GRAPHIC CURSOR
                JSR     sub_send_cmd_a          ; A = コマンド番号のまま発行
                ; --- 2 度目の走査: 応答を変数へ代入する ---
                PULS    X                   ; 変数の並びの先頭へ戻る
                LDD     #SUBCMD_P1          ; 応答の先頭 (1 点 4 byte)
                STD     gcu_rp
gcu_asg:
                JSR     skip_comma
                BCC     gcu_ret
                JSR     skip_sp_lparen
                BNE     gcu_ret             ; パレットコードに達した = 並びの終り
                LEAX    1,X                 ; '(' を消費
                BSR     gcu_one             ; 変数 1 ← 読み取った X 座標
                JSR     skip_comma
                BCC     gcu_syn             ; 組の中の ',' は必須
                BSR     gcu_one             ; 変数 2 ← 読み取った Y 座標
                JSR     skip_sp_x           ; ')' の前の空白を許容
                LEAX    1,X                 ; ')' を消費
                BRA     gcu_asg
gcu_ret:
                JMP     impl_stub           ; 末尾のパレットコードを読み捨てて復帰

; gcu_one — 応答 1 ワード (2 バイト) (,Y++) を X の変数へ代入する。X は名前の直後へ進む。
gcu_one:
                JSR     impl_get_var_ref    ; U = 値スロット / VARTYP = 型
                LDY     gcu_rp              ; 応答の読出し位置
                LDD     ,Y++                ; 読み取った座標
                STY     gcu_rp
                JSR     impl_int_to_fac     ; (X/U を保存する 2 本)
                JSR     impl_int_to_float   ; 型を問わず同じ経路で入れる
                PSHS    X                   ; 代入が壊す分を保全
                JSR     impl_assign_var
                PULS    X,PC

                include "7t_using_parse2.inc"   ; PRINT USING 10進解析 指数/正規化 (プール)
;------------------------------------------------------------------------------
; cs_do — cont_save の後半。頭部 (cont_save) は別の空きプールにあり、
;   単一プールに収まらないので分割配置している。参照はラベル経由のみ。
;   入力: X = 再開位置 / CURLIN = 停止した行番号 (呼出側で $FFFF でないと確認済)
;   出力: D = 記録前の CURLIN (呼出側が表示に使う)。CURLIN は $FFFF へ落とす。
;------------------------------------------------------------------------------
cs_do:
                STX     CONTPTR         ; 再開位置 (停止した文の次の位置。作業列内)
                ; 再開位置を含む行レコード先頭は CONTREC が対で持つ。
                ; ic_go はこの行を作業列へ復元してから CONTPTR で再開する。
                LDX     CURLINE_PTR
                STX     CONTREC
                LDX     #$FFFF
                STX     CURLIN          ; 以後は直接モード (D は壊さない)
                RTS
; (括弧付き式の整数化は、値域を持つ入口 eval_p_to_int_255 / eval_p_to_addr が
;  行う。fn(expr) 系の引数評価は impl_eval_expr_p が '(' / ')' の消費と
;  fn_xsave の確定まで行う契約で、素の impl_eval_expr とは入れ替えられない。)
; ice_trim — 1 行入力の後始末 (impl_input_line から JMP)。Y = INPBUF 内の書込位置。
;   行末に並ぶ空白を捨てて null 終端し、改行 (CR+LF) を 1 回送って復帰する
;   (エコーの後のカーソルは入力の末尾に残るので改行は呼出側の責務)。
;   行頭・行中の空白はそのまま残す。空白しかない行は長さ 0 の行になる。
ice_trim:
                CMPY    #INPBUF                 ; 行頭まで戻ったら終わり (全部空白の行)
                BEQ     ice_term
                LDA     -1,Y
                CMPA    #' '
                BNE     ice_term                ; 末尾が空白でなくなったら終わり
                LEAY    -1,Y                    ; 末尾の空白を 1 つ捨てる
                BRA     ice_trim
ice_term:
                CLR     ,Y                      ; INPBUF を null 終端
                JMP     disk_crlf               ; CR+LF を送って復帰 (共通の改行出力)

; vars_clear_all — 変数領域を「本文末尾から空」に揃える共通ルーチン。
;   スカラ領域の起点 VARTAB (= 本文末尾) はそのままに、配列領域の起点 ARYTAB と
;   末尾 STREND を VARTAB へ揃える。これで全変数 (数値・文字列・配列) が消える。
;   CLEAR・行入力後・読込完了の 3 か所が呼ぶ。
;   破壊: D のみ。呼出元はいずれも D 非生存。
; (本体は 7t_strheap.inc の空きプールに置く。文字列領域の開放と地続きで
;  実行するため str_heap_reset の直前に置いてある)

;------------------------------------------------------------------------------
; dsv_put — ディスク SAVE の出力シンク (out_vec 経由)。A をセクタ緩衝
;   (DA_SECBUF) へ追記し、満杯 (256B) になったら 1 セクタ書き出す。
;   X,Y,U は out_char が保護する。A,B はここで保全する。
; dsv_flush — DA_SECBUF を現クラスタの次セクタへ書く。クラスタを使い切って
;   いたら新クラスタを割当てて割当鎖を伸ばす。破壊: A,B,X,Y
;------------------------------------------------------------------------------
dsv_put:
                PSHS    A,B
                LDX     da_bptr
                STA     ,X+
                STX     da_bptr
                CMPX    #DA_SECBUF_END
                BLO     dsvp_ret
                BSR     dsv_flush       ; 満杯 → 書出し (da_bptr は先頭へ戻る)
dsvp_ret:
                PULS    A,B,PC

dsv_flush:
                LDA     dl_nsec
                BNE     dfl_have        ; クラスタ内にまだセクタ残
                JSR     dsv_alloc       ; B = 新クラスタ (仮終端で占有済)
                PSHS    B
                LDX     #str_work_buf+5
                LDB     dl_cluster
                ABX
                PULS    B
                STB     ,X              ; FAT[旧] = 新 (鎖を伸ばす)
                STB     dl_cluster
                JSR     disk_clu_seek   ; シーク + dl_secidx / dl_nsec=8
dfl_have:
                LDY     #DA_SECBUF
                LDA     dl_secidx
                JSR     dsv_wsec        ; 書けなければ 53 で中止 (戻らない)
                INC     dl_secidx
                DEC     dl_nsec
                LDX     #DA_SECBUF
                STX     da_bptr         ; 緩衝を先頭へ戻す
                RTS

; >>FOLD dl_seek_cyl1
;------------------------------------------------------------------------------
; dl_seek_cyl1 — サイド 0・物理シリンダ 1 (ディレクトリ/割当表のトラック) へ
;   シークして BUSY 解除を待ち、起動 ROM の控え ($FFE1+ドライブ) を更新する。
;   ディレクトリ検索と割当表参照が共用する。SEEK 以降は disk_clu_seek の
;   dcs_seek (7t_diskboot.inc) と共用する (末尾は JMP)。破壊: A, B, X。
;------------------------------------------------------------------------------
dl_seek_cyl1:
                CLR     FDC_SIDE        ; サイド0
                LDB     #1              ; SEEK 物理シリンダ1
                JMP     dcs_seek
; <<FOLD dl_seek_cyl1

;------------------------------------------------------------------------------
; dl_dirsec — ディレクトリの通し番号 (4〜32) を面と物理セクタに分ける。
;   ディレクトリはトラック 1 のセクタ 4〜セクタ 32 に置かれ、1 セクタが 32 バイト
;   ずつの 8 ブロック = 合わせて 232 スロットになる。通し番号は面 0 のセクタ
;   1〜16 に続けて面 1 のセクタ 1〜16 を 17〜32 と数えたものである
;   (参考書籍)。
;   入力: A = 通し番号 (4〜32)。出力: A = 物理セクタ (1〜16)、FDC_SIDE = 面。
;   破壊: A, B, CC。
;------------------------------------------------------------------------------
dl_dirsec:
                CLRB                    ; 面 0
                CMPA    #16
                BLS     dds_st
                SUBA    #16             ; 17〜32 → 面 1 のセクタ 1〜16
                INCB
dds_st:
                STB     FDC_SIDE
                RTS


; DEF
; DEF USRn = アドレス  /  DEF FN (使用者定義関数) / その他の DEF は読み飛ばし
;   入力: X = DEF トークンの直後
;   DEF USRn の n は 0-9 (省略時 0)。アドレス値を usr_slot_tbl[n] に格納する。
impl_def:
                JSR     skip_sp_x
                CMPA    #TOK_USR        ; USR トークン
                BEQ     idef_usr
                CMPA    #TOK_FN         ; FN トークン → DEF FN (使用者定義関数)
                LBEQ    idef_fn
                ; その他の DEF: 行末/コロンまで読み飛ばして無視する
idef_skip:
                LDA     ,X
                BEQ     idef_done
                CMPA    #':'
                BEQ     idef_done
                LEAX    1,X
                BRA     idef_skip
idef_done:
                RTS
idef_usr:
                LEAX    1,X             ; USR トークンをスキップ
                BSR     read_usr_slot   ; B = スロット番号, X 前進
                ; スロットの格納先アドレス (usr_slot_tbl + 2*slot) を先に確定し
                ; for_var_ptr に保持する (アドレス評価で D を使うため)。
                ASLB                    ; B = 2*slot
                PSHS    X
                LDX     #usr_slot_tbl
                ABX                     ; X = &usr_slot_tbl[slot]
                STX     for_var_ptr
                PULS    X
                JSR     impl_expect_tok
                FCB     TOK_EQ          ; = トークン
                JSR     eval_to_addr   ; アドレスなので -65536..65535
                PSHS    X               ; 本文ポインタ退避
                LDX     for_var_ptr
                STD     ,X              ; スロットへ格納
                PULS    X,PC

;==============================================================================
; read_usr_slot: USRn の スロット番号 n を読む
;   入力: X = USR トークンの直後 (スロット番号の位置)
;   出力: B = スロット番号 (0-9, 省略時 0), X = スロット番号の直後
;   ASCII 数字 ('0'-'9') を受理する (省略時は 0)。A/CC 破壊。
;==============================================================================
read_usr_slot:
                CLRB                    ; 既定スロット 0 (短縮)
                LDA     ,X
                CMPA    #'0'
                BLO     rus_done
                CMPA    #'9'
                BHI     rus_done
                LDB     ,X
                SUBB    #'0'            ; ASCII 数字
                LEAX    1,X
rus_done:
                RTS

; ERROR
;   誤り番号は 1..255 (0 と 256 以上は Illegal Function Call。動作観察)。
impl_error:
                ; 整数化 + 値域 1..255 (ここへ展開)
eval_to_int_err:
                JSR     eval_to_int_round
                TSTA
                BNE     isnd_fc
                TSTB
                BEQ     isnd_fc
                TFR     B,B
                JMP     impl_error_handler

; RESUME [0 | NEXT | 行番号] — 本体は空き領域 (irsm_body)

; FN
impl_fn:
                LDB     #18
                JMP     impl_error_handler

; SOUND — PSG を直接コントロールする (参考書籍)
;   構文: SOUND レジスタ番号 (0-13), データ (0-255)
;   範囲逸脱は Illegal Function Call。区切りのカンマ欠落は Syntax Error。
;   二次コマンド規約: 本文ポインタは 2,S 経由で受け渡す (impl_chain と同じ)。
;   PLAY のバックグラウンド演奏中も実行できる (参考書籍。レジスタの
;   書替えで音が変わり得るのは正常動作)。
impl_sound:
                LDX     2,S             ; X = 本文ポインタ
                JSR     eval_to_int    ; 式評価→整数化 (共通化)
                CMPD    #13
                BHI     isnd_fc         ; レジスタ番号 0-13 (負値も unsigned で弾く) (短縮)
                PSHS    B               ; レジスタ番号を退避
                JSR     skip_comma
                BCC     isnd_syn
                JSR     eval_to_int    ; 式評価→整数化 (共通化)
                CMPD    #255
                BHI     isnd_fc1        ; データ 0-255
                PULS    A               ; A = レジスタ番号, B = データ
                JSR     impl_psg_write
                STX     2,S             ; 読了位置を実行ループへ返す
                RTS
isnd_syn:
                JMP     errc_2
isnd_fc1:
                LEAS    1,S
isnd_fc:
                JMP     errc_5

; PLAY 文の本体は 7t_play.inc (impl_play)。

; INTERVAL
;   本体は容量の都合で 7t_diskboot.inc のプールに置く。参照は cmd_jt の FDB
;   のみで位置依存はない。

; KEY
; 構文: KEY 番号,文字列式
;   番号も文字列も式でよい。文字列側は ikey_str (7t_keyarg.inc) が汎用の
;   文字列式評価へ通すので、`KEY 1,A$` / `KEY 1,"A"+CHR$(13)` / `KEY 1,MID$(...)`
;   がいずれも通る。文字列でない値 (数値式) は Type Mismatch。
; subsys $2A (Define String of PF)
; $D383 = キー番号 (1-10)
; $D384 = 文字数
; $D385以降 = 文字列
;
; 引数が上記の形でないとき (`KEY ON` / `KEY OFF`) は Syntax Error を返す。
;   `KEY LIST` は割当ての一覧表示 (impl_key_list) へ分岐し、表示を終えてから
;   C=1 で戻る。引数の前処理は ikey_parse_num (7t_keyarg.inc) へ委譲。
;   (1) 命令語の後の空白は読み飛ばす (飛ばさないと `KEY ON` の 'O' 判定が
;       常に外れる)、(2) カンマ無しの脱出路でも PSHS B を必ず降ろしてから
;       RTS する (降ろさないと復帰アドレスがキー番号 1 バイトぶんずれて制御を失う)。
;   文字数 0 (`KEY n,""`) は「そのキーの割当てを消す」操作である。サブ CPU 側は
;   コマンド $2A を長さ 0 で受けると定義枠 16 byte をすべて 0 にするので、
;   メイン CPU 側は複写だけを飛ばして発行そのものは必ず行う。発行ごと飛ばすと
;   直前の割当てが残り、利用者は一度与えた割当てを二度と消せない。
;   (複写を飛ばすのは、B=0 のまま複写ループへ入ると 256 回まわって
;    ステージング域を溢れさせるためである。)
impl_key:
                ; 番号を丸括弧で囲む形 `KEY(n) ON/OFF/STOP` は割込みの許可・禁止・
                ;   停止を指す別の文なので、割当ての解析へ入る前に引き取る
                ;   (参考書籍 KEY(n) ON/OFF/STOP)。
                JSR     skip_sp_lparen
                LBEQ    impl_key_int    ; 本体は別プール (7t_core.s 末尾)
                JSR     ikey_parse_num  ; B = キー番号, X = ',' の直後 (不正形は復帰しない)
                BCS     ikey_done       ; C=1 = KEY LIST (一覧は表示済み → そのまま戻る)
                PSHS    B
                ; 文字列式 (定数・変数・連結・関数呼出しのいずれも受ける)
                JSR     ikey_str            ; B=文字数(0..15), sym_buf に文字列
                ; subsys $2A
                LDA     #$2A
                STA     SUBCMD_CMD
                PULS    A               ; キー番号
                STD     SUBCMD_P1           ; CL と F
                ; 複写先の指定で X (本文ポインタ) を潰してしまうので退避する。
                ;   退避を欠くと復帰時の X がステージング域を指し、実行ループが
                ;   その中身を文として読み始める。
                PSHS    X
                TSTB
                BEQ     ikey_send       ; 文字数0 = 割当ての消去 → 複写だけを飛ばす
                LDU     #sym_buf
                LDX     #SUBCMD_P3
ikey_copy:
                LDA     ,U+
                STA     ,X+
                DECB
                BNE     ikey_copy
ikey_send:
                JSR     impl_sub_send_cmd
                PULS    X               ; 本文ポインタを復元
ikey_done:
                RTS

; CIRCLE 線分専用の軽量送出。先行コマンドの完了を確認してから先頭 16B
;   ($15 は SH_CMD+P1..P11 のみ使用) を複写して RUN する。線分毎の完了待ち・
;   128B 読戻し・ATN 復帰操作は行わない (円の最終線分が impl_sub_send_cmd の
;   完全同期を通るため、後続コマンドとの直列化と BUSY 解放は保証
;   される)。壊す: A,B,X,U (cir_draw はレジスタ非依存)
;
; 完了確認は $FD05 bit7 (BUSY) を読んで行う。BUSY は HALT なしで読めるため、
;   待っている間サブを一度も止めない。
;   BUSY=0 が先行コマンド完了を意味することはサブ側の手順から従う:
;   サブはコマンド受理時に BUSY をセットし、応答完了 (SH_CMD クリア) の後に
;   待機ループ先頭で BUSY をクリアする。よって BUSY=0 なら SH_CMD=0 でもある。
;   処理中の共有領域を上書きしない点も、待ち方も impl_sub_send_cmd の先頭
;   (issc_busy_wait) と同じ。
impl_sub_send_draw:
issd_poll:
                LDA     PORT_SUBCPU         ; $FD05 bit7 = BUSY (HALT 不要で読める)
                BMI     issd_poll           ; サブが処理中の間は待つ (サブは走行継続)
issd_go:
                JSR     ssc_halt            ; HALT 確立 (共有 RAM へ書くため)
                LDX     #SUBSTG_BASE
                LDU     #SUBCMD_BASE
                LDB     #8
                JSR     ssc_cp_lp           ; 16B 複写 (X→U, B=残り対数 #254)
                CLR     PORT_SUBCPU         ; RUN (サブにコマンドを処理させる)
                RTS

; CHAIN "ファイル名"
;   テープから BASIC プログラムを読み込み、先頭行から実行する
;   (CHAIN はプログラムの連鎖実行。本 ROM では RUN"file" と
;    同義に扱い、変数継承なしで新プログラムへ制御を移す)。
;
;   二次コマンド呼出規約: iep_2nd / cmd_direct_2nd はハンドラを `JSR ,X`
;   (X=ハンドラアドレス) で呼び、本文ポインタ (CHAIN トークン直後) は直前の
;   `PSHS X` により 2,S へ退避されている。ここから本文ポインタを取り出す。
;   CHAIN は呼出元へ復帰しない。新プログラムの起動 (impl_exec_prog) がシステム
;   スタックを上端へ戻すので、連鎖のたびにスタックが伸びることはない。
impl_chain:
                LDX     2,S             ; X = CHAIN 引数の本文ポインタ
                JSR     skip_spc        ; 語間の空白を除去
                LDA     ,X
                CMPA    #'"'            ; ファイル名 (文字列) 必須
                BNE     ich_ret         ; 文字列でなければ何もしない
                JSR     impl_load       ; BASIC プログラムをテープからロード
                JMP     impl_exec_prog  ; 新プログラムを先頭から実行 (スタックを上端へ戻す。戻らない)
ich_ret:
                RTS

; LLIST / LPRINT (未実装スタブ)
;   (ERASE は impl_erase として 7t_arrays.inc の配列サブシステム側に実装。
;    コード領域を圧迫しないよう 空き領域へ配置)
impl_llist:
impl_lprint:
                RTS
