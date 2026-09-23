; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 行変換器 impl_tokenize_xu
;   impl_tokenize_xu : X = 読み元 (格納本文の ASCII), U = 書き先 (実行用行バッファ)
;   本文は ASCII で格納するので、実行時に格納本文を実行用行バッファへ変換する。
;   読み元は決して書き換えない (原文保持の要件)。
;==============================================================================
impl_tokenize_xu:
                ; --- 小文字の畳み込みは「読み取る場所ごと」に行う ---
                ; 予約語表も名前の綴りも大文字で持つので、入力の小文字は大文字へ
                ; 畳む必要がある。ただし行全体を先に畳むと、注記 (REM / ') の
                ; 本文と DATA の値まで畳んでしまい、打ち込んだとおりの文字が
                ; 保てない (参考書籍、動作観察)。
                ; よって畳むのは次の 2 か所だけとし、生で写す区間 (tdd_lp /
                ; tkp_raw) と引用符の中は触らない。
                ;   ・予約語照合で 1 文字ずつ突き合わせるとき (tmk_inner)
                ;   ・名前 (変数名・配列名) の綴りを写すとき (tok_idlp)
                ; 数値定数の指数記号を小文字で書いた綴り (1e5) は、定数の読取が
                ; 記号の手前で終わり、続く "e5" が名前の綴りとして畳まれるので、
                ; 変換結果の並びは大文字で書いた場合と同じになる。
                ; 畳み込みは全てレジスタ上で行い、読み元へは書き戻さない
                ; (読み元が格納本文そのものであり、原文を保つため)。
                PSHS    X               ; 入力先頭を保存
tok_lp:
                LDA     ,X
                BEQ     tok_done
                ; 文字列リテラル " はそのまま
                CMPA    #'"'
                BNE     tok_nostr
                STA     ,U+
                LEAX    1,X
tok_strcp:
                LDA     ,X+
                STA     ,U+
                BEQ     tok_done
                CMPA    #'"'
                BNE     tok_strcp
                BRA     tok_lp
tok_nostr:
                ; REM の後はそのまま
                ; スペースはスキップ (簡易)
                CMPA    #' '
                BNE     tok_nospace
                LEAX    1,X
                BRA     tok_lp
tok_nospace:
                ; 数値定数はそのまま写す。ただし写す前に表現範囲へ収まるかを
                ; 確かめる (参考書籍の数値定数。範囲外の定数を含む行は
                ; 入力した時点で Overflow となり記憶されない)。検査と転写の本体は
                ; tok_numlit (別プール)。
                CMPA    #'.'
                BEQ     tok_numstart    ; 小数点始まりの定数 (.5E37 など)
                CMPA    #'0'
                BLO     tok_nonum
                CMPA    #'9'
                BHI     tok_nonum
tok_numstart:
                JSR     tok_numlit
                BRA     tok_lp
tok_nonum:
                ; --- 予約語照合は「長い綴りが勝つ」順で行う ---
                ; 命令語表 (kw_tbl) を先に、関数語表 (kw_fn_tbl) を後に走査する。
                ; 両表の綴りを総当りで検めると、
                ;   ・命令語の綴りが関数語の綴りの真の先頭部分になっている組は無い
                ;   ・逆に関数語 INT は命令語 INTERVAL の真の先頭部分である
                ; の 2 点が成り立つ。ある入力に両表が当たるとき、当たった 2 語は
                ; どちらも同じ入力の先頭部分なので一方が他方の先頭部分になり、
                ; 上の性質からそれは必ず「命令語の側が長い」になる。よって命令語表を
                ; 先に見ること自体が最長一致であり、走査本体を増やさずに済む。
                ; (関数語を先に見ると INTERVAL が INT + ERVAL へ割れる。表へ語を
                ;  足すときはこの 2 性質を必ず保つこと。)
                BSR     tok_match_kw
                BNE     tok_kwmiss
                JMP     tok_kw_post
tok_kwmiss:
                ; 命令語で当たらなかった → 関数予約語 (PEEK/LEN/ASC/LEFT$ 等)。
                ; 成功時は $FF + トークン を出力済み。
                BSR     tok_match_fn
                BEQ     tok_lp
tok_nokw:
                ; 二次予約語 (CHAIN/ERASE/LLIST/LPRINT/SOUND/PLAY → $EE-$F3)
                JSR     tok_match_kw2
                BEQ     tok_lp
                ; 二次関数予約語 (LPOS → $FF $B4)
                JSR     tok_match_fn2
                BEQ     tok_lp
                ; そのまま転写。名前の先頭の小文字はここで大文字へ畳んで写す
                ; (読み元 = 格納原文を書き換えないので、転写の瞬間に畳む。
                ;  名前の続きは tok_idlp が畳む)。
                LDA     ,X+
                CMPA    #'z'
                BHI     tnk_put
                CMPA    #'a'
                BLO     tnk_put
                SUBA    #$20
tnk_put:
                STA     ,U+
                ; --- 名前 (変数名・配列名) の途中は予約語照合を行わない ---
                ; 予約語のどれにも当たらなかった英字は名前の先頭である。名前は
                ; 英字で始まり英数字が続く綴りなので、続く英数字はここで一括して
                ; 写し、その間は予約語照合に掛けない。これにより名前の途中に
                ; 予約語の綴りが現れても名前が分解されない (SCORES / ABCDEF 等)。
                ; 名前が終わる文字 (空白・型宣言子・演算子・区切り等) は写さずに
                ; 主ループへ戻すので、そこからは通常どおり予約語照合が働く。
                CMPA    #'A'            ; 英字以外 (記号など) は名前ではない
                BLO     tok_lp
                CMPA    #'Z'
                BHI     tok_lp
tok_idlp:
                LDA     ,X              ; 名前の続きか (英数字なら続き)
                CMPA    #'0'
                BLO     tok_lp
                CMPA    #'9'
                BLS     tok_idput
                CMPA    #'A'
                BLO     tok_lp
                CMPA    #'Z'
                BLS     tok_idput
                CMPA    #'a'            ; 小文字も名前の続き (大文字へ畳んで写す)
                BLO     tok_lp
                CMPA    #'z'
                BHI     tok_lp
                SUBA    #$20
tok_idput:
                STA     ,U+
                LEAX    1,X
                BRA     tok_idlp
tok_done:
                CLR     ,U
                PULS    X,PC


; tok_match_kw / tok_match_fn: 予約語マッチ (完全実装)
; 入力: X = 入力バッファ先頭, U = 出力バッファ先頭
; 出力: 成功 → Z=1 (BNE が分岐しない)
;         X が入力の次位置に前進, U にトークンを書込んで前進
;       失敗 → Z=0 (BNE が分岐)
;         X/U 変化なし
;
; 命令語表 (kw_tbl) と関数語表 (kw_fn_tbl) は綴りの持ち方も走査手順も同一で、
; 違うのは「どの表を見るか」と「関数語は $FF 前置を伴うか」だけである。よって
; 入口だけを 2 つ持ち、走査本体 (tmk_outer 以降) は共有する。前置の要否は
; 一致した語が関数語表の側にあるかどうかで判別できるので、別途の指標は持たない。
;
; 使用レジスタ:
;   X = 予約語表 走査ポインタ
;   Y = 入力ポインタ (元の X 値, 各語の先頭でリセット)
;   B = 現在のトークン値 ($80 から)
;   U = 出力ポインタ (成功時のみ書き込み)
; スタック (走査本体の入口): S+0 = 表終端, S+2 = 入力先頭, S+4 = 呼出元 Y
;
tok_match_kw:
                ; 入力: X = 入力先頭, U = 出力先
                ; 出力: Z=1 成功(X=入力の語末次, U+1) / Z=0 失敗(X=入力先頭に戻す)
                PSHS    Y,X             ; outer: Y と入力先頭 X を保存 (Y先, X後 = S+0=X_hi, S+1=X_lo, S+2=Y_hi, S+3=Y_lo)
                TFR     X,Y             ; Y = 入力先頭
                LDX     #kw_tbl_end     ; 表終端
                PSHS    X
                ; 走査開始位置は先頭字索引 kw_idx で群へ絞る (照合コストの主対策)。
                ; 本体は空きプール (tmk_idx)。当区間に余地が無いため。
                LDX     #kw_idx
                JMP     tmk_idx

; tok_match_fn: 関数予約語マッチ (kw_fn_tbl)。走査本体は tok_match_kw と共有し、
;   マッチ時に $FF + トークン値 を出力する (式評価系の $FF 関数前置に対応)。
;   出力: 成功 → Z=1, X=語末次, U に $FF tok を書込/前進。
;         失敗 → Z=0, X/U 不変。
; tok_match_fn2 (二次関数予約語) は当区間に余白が無いため別プール
;   (7t_float.inc の空きプールの手前) に置いた。走査本体 tmk_outer は共用する。
tok_match_fn:
                PSHS    Y,X
                TFR     X,Y
                LDX     #kw_fn_tbl_end  ; 表終端
                PSHS    X
                LDX     #fn_idx         ; 関数語表も先頭字索引で群へ絞る
                JMP     tmk_idx
tmk_outer_first:
                ; 走査の入口 (kw2/fn2 用): 入力 1 文字目の大文字畳みを 1 回だけ
                ; 行い tok_fold1 へ置いてから走査本体へ。kw/fn は tmk_idx が
                ; 索引選択のついでに設定するので tmk_outer へ直接入る。
                LDA     ,Y              ; 入力の 1 文字目
                JSR     fold_a          ; 小文字→大文字 (レジスタ上のみ)
                STA     tok_fold1
tmk_outer:
                CMPX    ,S              ; テーブル終端チェック
                BHS     tmk_fail        ; 終端 → 失敗
                ; --- 語頭の早期判定 (全表共通の高速化) ---
                ; 予約語表は生成器の独自並び規則により語頭バイト昇順で並ぶ
                ; (機械保証)。入力の 1 文字目 (tok_fold1 = 大文字畳み済) と
                ; 語頭を先に比べ、
                ;   一致    → 語の照合 (tmk_inner) へ
                ;   語頭>入力 → 以降の語頭はさらに大きく一致し得ない → 即失敗
                ;   語頭<入力 → この語を読み飛ばして次語へ
                ; 語の途中の不一致 (tmk_mismatch) は従来どおり次語へ進む
                ; (同一語頭の群の中は長い綴りが先の規則で並ぶため)。
                LDA     ,X              ; 表の語頭 (1 文字語は bit7 込み)
                ANDA    #$7F
                CMPA    tok_fold1       ; 語頭 − 入力
                BEQ     tmk_enter
                BHI     tmk_fail        ; 語頭 > 入力 → 失敗確定 (語頭昇順)
                BRA     tmk_skip_rest   ; 語頭 < 入力 → 語頭から読み飛ばし
tmk_enter:
                ; 入力先頭を保存 (各語でリセット用)
                PSHS    Y               ; inner: 入力先頭を保存
tmk_inner:
                LDA     ,Y+             ; 入力の1文字 (レジスタ上でだけ畳む。
                CMPA    #'a'            ;  読み元 = 格納原文へは書き戻さない)
                BLO     tmk_i2          ; 大文字・数字・記号 → そのまま突合
                CMPA    #'z'
                BHI     tmk_ichk        ; '{'-'~' と bit7 付きは別途判定
                SUBA    #$20            ; 小文字→大文字 (表は大文字で持つ)
tmk_i2:
                EORA    ,X+             ; 表の 1 文字と突合: 一致なら $00 (語中)
                BEQ     tmk_inner       ;   か $80 (bit7 = 語末) だけが残る
                CMPA    #$80
                BEQ     tmk_matched     ; 語末で一致 → マッチ成功!
                BRA     tmk_mismatch
tmk_ichk:
                TSTA                    ; bit7 付き入力 (図形文字等) は常に不一致
                BPL     tmk_i2          ;   (表は 7 ビット綴り。EOR 突合の例外)
                LEAX    1,X             ; 不一致の作法 (X = 不一致文字の次) に合わせる
tmk_mismatch:
                PULS    Y               ; inner PSHS Y を復元 (入力先頭)
                LDA     -1,X            ; 不一致文字 (X は前進済みなので -1)
                BMI     tmk_skip_done   ; 語末なら X は既に次語先頭
tmk_skip_rest:
                LDA     ,X+
                BPL     tmk_skip_rest   ; bit7=0 の間スキップ
tmk_skip_done:
                INCB                    ; 次のトークン値
                BRA     tmk_outer
tmk_matched:
                ; Y = 入力の語末次位置, B = トークン値, X = 表の語末次位置
                ; スタック: [inner_Y 2B, 表終端 2B, 入力先頭 2B, 呼出元 Y 2B]
                LEAS    2,S             ; inner PSHS Y を捨てる
                CMPX    #kw_fn_tbl      ; 一致した語は関数語表 (二次関数語表を含む) の側か
                BLS     tmk_nopfx       ; 命令語表なら前置なし
                CMPB    #TOK_2ND_FIRST  ; 二次命令語 ($EE-$F3) も前置なし
                BHS     tmk_nopfx       ;   (二次命令語表も本走査を共用するため)
                LDA     #TOK_FUNC_PFX   ; $FF 前置 (関数語)
                STA     ,U+
tmk_nopfx:
                TFR     Y,X             ; X = 入力の語末次位置
                STB     ,U+             ; トークンを出力バッファに書き込む
                LEAS    4,S             ; 表終端と入力先頭を捨てる (不要)
                PULS    Y               ; outer PSHS Y を復元
                ORCC    #$04            ; Z=1 (成功)
                RTS
tmk_fail:
                LEAS    2,S             ; 表終端を捨てる
                PULS    X,Y             ; outer PSHS Y,X を復元 (X = 入力先頭に戻す)
                ANDCC   #$FB            ; Z=0 (失敗)
                RTS

;==============================================================================
; 行挿入 impl_insert_line
;   本体は 7t_startup.inc の空きプールに置く。
;   置換・削除・途中挿入を扱うため当区間の空きに収まらない。
;   参照はラベル経由のみで位置依存なし。
;==============================================================================


                ; 一致した語ごとの後処理 (DATA / REM / ' の後を生で写す、GO に
                ; TO / SUB が続くかを確かめる) は tok_kw_post が行う。当区間には
                ; その分の余地が無いため本体はプールへ置く。
                ; 一致後の後処理は別プール (当区間に余地が無い) (ここへ展開)
;------------------------------------------------------------------------------
; tok_kw_post — 命令語が 1 語当たった直後の後処理
;
;   当たった語で続きの扱いが分かれる。
;     GO        TO / SUB が続くときだけ受理する。
;     DATA      文の区切り ':' まで予約語化せず生で写す。
;     REM / '   行末まで生で写す。
;   生で写すのは、注記やデータ項目の中の綴りが予約語として拾われないように
;   するため (参考書籍)。いずれでもなければ主ループへ戻る。
;   REM と ' は表の並びで隣接しない (値の隣接に依存しない) ので、素直に
;   2 回の等値比較で見る。
;
;   入力: X = 語の直後の入力位置, U = 出力位置 (直前に当該トークンを書いてある)
;------------------------------------------------------------------------------
tok_kw_post:
                LDA     -1,U            ; 直前に書いたトークン
                CMPA    #TOK_GO         ; GO
                LBEQ    tok_go_chk      ; 続きの語を確かめる (別プール)
                CMPA    #TOK_DATA       ; DATA
                LBEQ    tdd_lp          ; ':' まで生で写す (別プール)
                CMPA    #TOK_REM        ; REM
                LBEQ    tkp_raw         ; 行末まで生で写す (別プール)
                CMPA    #TOK_APOS       ; ' (注記の短縮形。REM と同じ扱い)
                LBEQ    tkp_raw
                JMP     tok_lp

;==============================================================================
; expand_abbrev — 打ち込んだ 1 行のキーワードの省略形を完全形へ直す
;
;   キーワードのうち 16 語は
;   綴りの頭に '.' を付けた省略形で打ち込むことができ、プログラムリストは
;   完全形で出力される。本 ROM は打ち込んだ原文の ASCII をそのまま記憶する
;   ので、記憶する前 (行の挿入の手前) と直接モードの実行に入る前に、この
;   場所で完全形へ直しておけば LIST は完全形を出す。直接モードの行も同じ
;   経路を通るので、プログラム行と直接モードの両方を賄う。
;
;   引用符で囲んだ中、注記 (REM / ') の後、DATA の並びは直さない。これらは
;   打ち込んだとおりの文字が残る場所であり、省略形は語としてだけ扱う
;   からである (区切り方は行変換器 tok_kw_post と同じ)。
;
;   完全形は必ず省略形より長いので行は伸びる。1 行の上限 (255 字) に収まら
;   なくなるときはその語を直さずに残す (打ち込んだ綴りのまま扱われる)。
;
;   入力: なし (INPBUF を直に直す)  破壊: A,B,X,Y,U
;==============================================================================
expand_abbrev:
                LDY     #INPBUF
exa_lp:
                LDA     ,Y
                BEQ     exa_ret                 ; 行末
                LEAY    1,Y
                CMPA    #'"'
                BEQ     exa_str
                CMPA    #$27                    ; ' (注記) → 行末まで原文のまま
                BEQ     exa_ret
                JSR     fold_a                  ; 小文字で打っても同じ語とみなす
                CMPA    #'A'
                BLO     exa_lp
                CMPA    #'Z'
                BHI     exa_lp
                LEAY    -1,Y                    ; 語の先頭へ戻す
                BSR     exa_word
                BRA     exa_lp
exa_str:                                        ; 引用符の中は原文のまま
                LDA     ,Y
                BEQ     exa_ret
                LEAY    1,Y
                CMPA    #'"'
                BNE     exa_str
                BRA     exa_lp
exa_ret:
                RTS

;------------------------------------------------------------------------------
; exa_word — Y が指す語を 1 つ見る。
;   英字の並びを数え、
;     ・注記 / DATA の語なら、その後ろを打ち込んだとおりに読み飛ばす
;     ・直後が '.' で省略形の表に載っていれば完全形へ置き換える
;   いずれでもなければ語の次まで Y を進めるだけ。
;   出力: Y = 次に見る位置
;------------------------------------------------------------------------------
exa_word:
                TFR     Y,U                     ; U = 語の先頭
                CLRB                            ; B = 語の字数
ewd_len:
                LDA     ,Y
                JSR     fold_a
                CMPA    #'A'
                BLO     ewd_end
                CMPA    #'Z'
                BHI     ewd_end
                LEAY    1,Y
                INCB
                BRA     ewd_len
ewd_end:
                LDX     #exa_raw_tbl
                BSR     exa_raw_find            ; 注記 / DATA の語か
                BNE     ewd_abbr
                LDB     ,X                      ; 区切りの字 (0 = 行末まで)
                BEQ     ewd_toeol
ewd_skip:                                       ; 区切りの字まで原文のまま
                LDA     ,Y
                BEQ     ewd_ret
                LEAY    1,Y
                CMPA    #'"'
                BEQ     ewd_sstr
                PSHS    B
                CMPA    ,S+
                BNE     ewd_skip
                RTS                             ; 区切りの次から通常の走査へ
ewd_sstr:                                       ; DATA の並びの中の引用符
                LDA     ,Y
                BEQ     ewd_ret
                LEAY    1,Y
                CMPA    #'"'
                BNE     ewd_sstr
                BRA     ewd_skip
ewd_toeol:
                LDA     ,Y
                BEQ     ewd_ret
                LEAY    1,Y
                BRA     ewd_toeol
ewd_ret:
                RTS
ewd_abbr:
                LDA     ,Y                      ; 語の直後の字
                CMPA    #'.'
                BNE     ewd_ret                 ; 省略形ではない
                LDX     #kw_abbr_tbl
                BSR     exa_abbr_find
                BNE     ewd_ret                 ; 表に無い綴り
                LEAY    1,Y                     ; '.' も置き換える範囲に入れる
                BRA     exa_subst               ; [U, Y) を完全形へ (末尾呼び)

;------------------------------------------------------------------------------
; exa_cmpw — 語 (U から A 字) と X の綴りを突き合わせる。小文字は大文字へ畳む。
;   出力: Z=1 一致 / Z=0 不一致。いずれも X は綴りの次まで進む。U と B は保つ。
;------------------------------------------------------------------------------
exa_cmpw:
                PSHS    U,B
                CLRB                            ; B = 0 … まだ食い違いなし
ecw_lp:
                TSTA
                BEQ     ecw_end
                DECA
                PSHS    A
                LDA     ,U+
                JSR     fold_a
                CMPA    ,X+
                BEQ     ecw_next
                LDB     #1                      ; 食い違い (綴りは最後まで進める)
ecw_next:
                PULS    A
                BRA     ecw_lp
ecw_end:
                TSTB
                PULS    U,B,PC                  ; PULS は CC を変えないので Z は残る

;------------------------------------------------------------------------------
; exa_raw_find — 語 (U から B 字) を exa_raw_tbl から探す。
;   出力: Z=1 一致 (X = 区切りの字の欄) / Z=0 不一致
;------------------------------------------------------------------------------
exa_raw_find:
erf_lp:
                LDA     ,X+                     ; 字数 (0 = 表の終り)
                BEQ     erf_miss
                PSHS    B
                CMPA    ,S+                     ; 語の字数と同じか
                BEQ     erf_cmp
                LEAX    A,X                     ; 綴りを飛ばす
                LEAX    1,X                     ; 区切りの字を飛ばす
                BRA     erf_lp
erf_cmp:
                BSR     exa_cmpw
                BEQ     erf_ret                 ; Z=1, X = 区切りの字の欄
                LEAX    1,X
                BRA     erf_lp
erf_miss:
                ANDCC   #$FB                    ; Z=0
erf_ret:
                RTS

;------------------------------------------------------------------------------
; exa_abbr_find — 語 (U から B 字) を kw_abbr_tbl から探す。
;   出力: Z=1 一致 (X = 完全形の字数の欄) / Z=0 不一致
;------------------------------------------------------------------------------
exa_abbr_find:
eaf_lp:
                LDA     ,X+                     ; 省略形の字数 (0 = 表の終り)
                BEQ     eaf_miss
                PSHS    B
                CMPA    ,S+                     ; 語の字数と同じか
                BEQ     eaf_cmp
                LEAX    A,X                     ; 省略形の綴りを飛ばす
eaf_skipfull:
                LDA     ,X+                     ; 完全形の字数
                LEAX    A,X
                BRA     eaf_lp
eaf_cmp:
                BSR     exa_cmpw
                BEQ     eaf_ret                 ; Z=1, X = 完全形の字数の欄
                BRA     eaf_skipfull
eaf_miss:
                ANDCC   #$FB                    ; Z=0
eaf_ret:
                RTS

;------------------------------------------------------------------------------
; exa_subst — 行の [U, Y) を X の完全形の綴りへ置き換える。
;   X: [字数][綴り]。完全形は必ず元の綴り (省略形 + '.') より長いので、行末の
;   0 まで含めて後ろ側を差の分だけ下げ、空いた場所へ完全形を写す。下げるのは
;   末尾から前へ 1 度に行う。行が 255 字に収まらないときは何もしない。
;   出力: Y = 置き換えた綴りの次
;------------------------------------------------------------------------------
exa_subst:
                LDB     ,X+                     ; B = 完全形の字数
                PSHS    U,Y,X,B                 ; S+0=字数 S+1,2=綴り S+3,4=末尾 S+5,6=先頭
                TFR     Y,D
                SUBD    5,S                     ; B = 元の綴りの字数
                NEGB
                ADDB    ,S                      ; B = 伸びる字数
                BEQ     exs_copy
                TFR     Y,X                     ; 行末の 0 を探す
exs_end:
                LDA     ,X+
                BNE     exs_end
                LEAX    -1,X                    ; X = 行末の 0
                CLRA                            ; D = 伸びる字数
                LEAU    D,X                     ; U = 写し先の末尾
                CMPU    #INPBUF+255             ; 1 行 255 字に収まるか
                BHI     exs_nofit
exs_mv:                                         ; 後ろから前へ写して隙間を空ける
                LDA     ,X
                STA     ,U
                LEAU    -1,U
                LEAX    -1,X
                CMPX    3,S                     ; 元の綴りの末尾まで写したか
                BHS     exs_mv
exs_copy:
                PULS    B,X                     ; B = 字数, X = 完全形の綴り
                LEAS    2,S                     ; 末尾は用済み
                PULS    Y                       ; Y = 置き換える先頭
                PSHS    B                       ; 字数 (呼出元へ返す Y の算出に使う)
                DECB                            ; 後ろの字から前へ写す (B = 添字)
exs_cp:
                LDA     B,X
                STA     B,Y
                DECB
                BPL     exs_cp
                LDB     ,S+
                LEAY    B,Y                     ; Y = 置き換えた綴りの次
                RTS
exs_nofit:                                      ; 入らない → その語は直さない
                PULS    B,X,Y,U,PC

;------------------------------------------------------------------------------
; exa_raw_tbl — 後ろを打ち込んだとおりに残す語 (行変換器 tok_kw_post と同じ)
;   [字数][綴り][区切りの字]。区切りの字 0 = 行末まで。表の終りは字数 0。
;------------------------------------------------------------------------------
exa_raw_tbl:
                FCB     3,'R','E','M',0         ; REM … 行末まで
                FCB     4,'D','A','T','A',':'   ; DATA … 文の切れ目まで
                FCB     0

;------------------------------------------------------------------------------
; kw_abbr_tbl — キーワードの省略形 (16 語)
;   [省略形の字数][省略形の綴り][完全形の字数][完全形の綴り]。表の終りは字数 0。
;   省略形は綴りの後ろに '.' を伴って打ち込む。
;------------------------------------------------------------------------------
kw_abbr_tbl:
                FCB     1,'C',4,'C','O','N','T'
                FCB     2,'L','O',4,'L','O','A','D'
                FCB     2,'S','A',4,'S','A','V','E'
                FCB     2,'S','K',5,'S','K','I','P','F'
                FCB     2,'M','E',5,'M','E','R','G','E'
                FCB     2,'G','O',4,'G','O','T','O'
                FCB     3,'G','O','S',5,'G','O','S','U','B'
                FCB     3,'R','E','T',6,'R','E','T','U','R','N'
                FCB     4,'R','N','D','M',9,'R','A','N','D','O','M','I','Z','E'
                FCB     3,'L','O','C',6,'L','O','C','A','T','E'
                FCB     3,'C','O','L',5,'C','O','L','O','R'
                FCB     1,'M',5,'M','O','T','O','R'
                FCB     1,'R',3,'R','U','N'
                FCB     1,'L',4,'L','I','S','T'
                FCB     4,'C','O','N','S',7,'C','O','N','S','O','L','E'
                FCB     1,'W',5,'W','I','D','T','H'
                FCB     0
