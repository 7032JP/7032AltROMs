; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 単精度浮動小数演算 (参考書籍の実数演算仕様に準拠)
;
;   + - * / と比較は両オペランドを float 昇格し fadd_core / fmul_core /
;   fdiv_core / fcmp_core で処理する。結果 FACTYPE=1。
;   \ (整数除算) と MOD は整数のまま扱う。
;
;   FAC 展開形 (8B):
;     +0 指数 (bias128, 0=値ゼロ) / +1..+4 仮数 32bit (先頭1明示, bit31=$xx+1 の bit7)
;     +5 符号 (bit7) / +6..+7 ガード
;   スクラッチ $0043-$004F を使用 (FAC1/FAC2 と非重複)。X/Y/U は不変。
;==============================================================================

; impl_int_to_float: FAC1+2 の符号付き16bit整数 → FAC1 (float 展開形), FACTYPE=1
;   破壊: A,B,CC (D)。X/Y/U 保存。
impl_int_to_float:
                LDD     FAC1+2
                BEQ     fdc_z0
                BPL     iitf_pos
                ; 負: |value| を求め, 符号ビットを立てる
                JSR     negd
                PSHS    D
                LDA     #$80
                STA     FAC1+5
                PULS    D
                BRA     iitf_mag
iitf_pos:
                CLR     FAC1+5
iitf_mag:
                ; magnitude D を仮数上位16bit へ, 下位/ガード 0
                STD     FAC1+1          ; $51:$52
                CLR     FAC1+3
                CLR     FAC1+4
                CLR     FAC1+6
                CLR     FAC1+7
                LDA     #143            ; 指数の初期値 (magnitude を bit31..16 に置いた前提)
                STA     FAC1
iitf_norm:
                LDA     FAC1+1
                BMI     iitf_done       ; bit31 (=$51 bit7) セット → 正規化完了
                BNE     iitf_nbit       ; 上位バイトに 1 がある → ビット単位で詰める
                JSR     iitf_shl8       ; 上位 0 バイトは 1 バイト移動で 8 歩ぶん
                BCS     iitf_norm       ; C=1 = 1 バイトぶん進めた
iitf_nbit:
                ; 仮数が全ゼロなら指数を減らし続けて回り続けるので、指数 0 を
                ; ゼロ値の終端とする。整数経由 (impl_int_to_float) は入口で
                ; ゼロを弾くため通らないが、仮数を直接組み立てて合流する経路
                ; (ift_num の 24 bit 組立て) はここでゼロを処理する。
                LDA     FAC1
                BEQ     fdc_z0
                JSR     fac1_shl32
                DEC     FAC1
                BRA     iitf_norm
iitf_done:
                JMP     fac_type_sng    ; 型=単精度にして復帰 (共通化)

; FRE は全てラベル経由で呼ばれる。
impl_fn_fre:
                JSR     impl_eval_expr_p
                ; 引数が文字列なら「文字列領域の空き」を返す (参考書籍)。
                ;   詰め直しを起こしてから数える。数値引数は
                ;   「変数/配列に使える空き」= 文字列領域の下限 - 配列領域末尾。
                JSR     chk_type_str    ; FACTYPE を文字列型と比較 (共通化)
                BEQ     ifre_str
                LDD     STRBOT
                SUBD    STREND
                JMP     impl_int_to_fac
ifre_str:
                JSR     str_gc
                LDD     FRETOP
                SUBD    STRBOT
                JMP     impl_int_to_fac

; fdiv_core: FAC2(被除数) / FAC1(除数) → FAC1, FACTYPE=1。両者 float 展開形。
;   指数減算+bias, 32bit 復元法除算, 正規化不要(結果は常に先頭1)。X/Y/U 保存。
fdiv_core:
                LDA     FAC1            ; 除数 指数
                BNE     fdc_divok
                JMP     errc_11
fdc_divok:
                LDA     FAC2            ; 被除数 指数
                BNE     fdc_go
fdc_z0:
                JMP     flt_zero1       ; 被除数=0 → 結果 0
fdc_go:
                ; 符号 = 符号(FAC2) XOR 符号(FAC1)
                LDA     FAC2+5
                EORA    FAC1+5
                ANDA    #$80
                PSHS    A               ; 2,S(後) = 結果符号
                ; 指数 eres = e2 - e1 + 127 (int_q 分は後で加算)
                CLRA
                LDB     FAC2            ; D = e2
                SUBB    FAC1            ; B -= e1
                SBCA    #0              ; 符号拡張
                ADDD    #127
                PSHS    D               ; 0,S = eres(16bit), 2,S = 符号
                ; 仮数コピー: m1=除数, m2=被除数(=rem)
                LDD     FAC1+1
                STD     fdv_m1
                LDD     FAC1+3
                STD     fdv_m1+2
                LDD     FAC2+1
                STD     fdv_rem
                LDD     FAC2+3
                STD     fdv_rem+2
                CLR     fdv_q
                CLR     fdv_q+1
                CLR     fdv_q+2
                CLR     fdv_q+3
                ; 事前減算: rem >= m1 なら rem -= m1, int_q=1 (rem<2*m1 保証で1回)
                ; int_q は符号バイト(2,S)の bit0 に立てる
                LDD     fdv_rem
                CMPD    fdv_m1
                BHI     fdc_presub
                BCS     fdc_nopresub
                LDD     fdv_rem+2
                CMPD    fdv_m1+2
                BCS     fdc_nopresub
fdc_presub:
                BSR     fdc_rsub        ; 剰余 -= 除数仮数 (32bit)
                LDA     2,S             ; 符号バイト
                ORA     #1              ; int_q = 1 (bit0)
                STA     2,S
                LDD     ,S              ; eres += 1
                ADDD    #1
                STD     ,S
fdc_nopresub:
                ; --- 32bit 復元法除算: Q = floor(rem * 2^32 / m1), rem<m1 ---
                LDA     #32
                STA     fdv_cnt
fdc_divlp:
                ASL     fdv_q+3
                ROL     fdv_q+2
                ROL     fdv_q+1
                ROL     fdv_q
                ASL     fdv_rem+3
                ROL     fdv_rem+2
                ROL     fdv_rem+1
                ROL     fdv_rem         ; C = bit32 (桁上げ)
                BCS     fdc_sub
                LDD     fdv_rem
                CMPD    fdv_m1
                BHI     fdc_sub
                BCS     fdc_nosub
                LDD     fdv_rem+2
                CMPD    fdv_m1+2
                BCS     fdc_nosub
fdc_sub:
                BSR     fdc_rsub        ; 剰余 -= 除数仮数 (32bit)
                INC     fdv_q+3         ; 商 bit0 = 1
fdc_nosub:
                DEC     fdv_cnt
                BNE     fdc_divlp
                ; 仮数組み立て+最近接丸め (丸め追加。本体は空きプール)
                JMP     fdc_post

;------------------------------------------------------------------------------
; fdc_rsub — 32bit の剰余から除数仮数を引く (fdv_rem -= fdv_m1)。
;   復元法除算の前引きと各ビットの引きで同じ並びを使うので 1 本にまとめてある。
;   破壊: B,CC。
;------------------------------------------------------------------------------
fdc_rsub:
                LDD     fdv_rem+2       ; 下位 16 ビット
                SUBD    fdv_m1+2
                STD     fdv_rem+2
                LDD     fdv_rem         ; 上位 16 ビット (借りを SBCB → SBCA へ連鎖)
                SBCB    fdv_m1+1
                SBCA    fdv_m1
                STD     fdv_rem
                RTS

;==============================================================================
; print_float: FACTYPE=1 の FAC1 を有効6桁10進で出力 (impl_print_val から分岐)
;   整数値は小数点なし / 0.5→.5 (先頭ゼロ省略) / 末尾ゼロ抑制。
;   0.1≤|x|<1E6 は通常表記、それ以外は E 形式 (pfe_try) で出す。
;   表示桁の最下位で最近接丸めを行い、桁上げは整数部へ伝播させる。X 保存。
;==============================================================================
; div32by10: pf_ip(4B) /= 10, 余り(0-9) → A。X/Y/U 保存。
div32by10:
                CLRA              ; 余り
                LDB     #32
d32_lp:
                ASL     pf_ip+3
                ROL     pf_ip+2
                ROL     pf_ip+1
                ROL     pf_ip
                ROLA                    ; 余り = 余り*2 + 桁上げ
                CMPA    #10
                BLO     d32_skip
                SUBA    #10
                INC     pf_ip+3         ; 商 bit0 = 1
d32_skip:
                DECB
                BNE     d32_lp
                RTS

; fracx10: pf_frac(4B) *= 10, あふれ整数桁(0-9) → A。X/Y/U 保存。
fracx10:
                LDB     pf_frac+3
                LDA     #10
                MUL
                STB     pf_frac+3
                STA     pf_tmp
                LDB     pf_frac+2
                LDA     #10
                MUL
                ADDB    pf_tmp
                ADCA    #0
                STB     pf_frac+2
                STA     pf_tmp
                LDB     pf_frac+1
                LDA     #10
                MUL
                ADDB    pf_tmp
                ADCA    #0
                STB     pf_frac+1
                STA     pf_tmp
                LDB     pf_frac
                LDA     #10
                MUL
                ADDB    pf_tmp
                ADCA    #0
                STB     pf_frac
                RTS                     ; A = あふれ桁

; pf_add_round: 表示小数桁数 pf_nfrac に基づき pf_frac へ 0.5*10^-nfrac を加算し
;   最近接丸めを行う。呼出しは整数桁抽出の前。桁上げはそのまま pf_ip (未抽出の
;   整数部) へ +1 伝播させる (動作観察)。
;   X/Y/U 保存。
pf_add_round:
                LDD     pf_ip
                PSHS    D
                LDD     pf_ip+2
                PSHS    D                   ; 整数部退避 (0.5*10^-nfrac 生成に流用)
                LDD     #$8000              ; pf_ip = 2^31 (= 0.5 の固定小数)
                STD     pf_ip
                CLR     pf_ip+2
                CLR     pf_ip+3
                LDB     pf_nfrac
                BEQ     pfar_add
pfar_div:
                PSHS    B                   ; div32by10 は B を壊すため退避
                BSR     div32by10           ; pf_ip /= 10 (余りA は無視)
                PULS    B
                DECB
                BNE     pfar_div
pfar_add:
                LDD     pf_frac+2           ; pf_frac += pf_ip (4B。下位 16 ビット)
                ADDD    pf_ip+2
                STD     pf_frac+2
                LDD     pf_frac             ; 上位 16 ビット (桁上げを ADCB → ADCA へ連鎖)
                ADCB    pf_ip+1
                ADCA    pf_ip
                STD     pf_frac             ; C = 整数部への桁上げ
                PULS    D
                STD     pf_ip+2
                PULS    D
                STD     pf_ip               ; 整数部復元 (CC の C は PULS/STD で不変…
                                            ;  ではないため下で再判定しない: PULS は CC を
                                            ;  変えないが STD は N/Z のみ変更で C 保存)
                BCC     pfar_done           ; 桁上げ無し → OK
                INC     pf_ip+3             ; 桁上げ → 整数部 +1 (32bit 伝播)
                BNE     pfar_done
                INC     pf_ip+2
                BNE     pfar_done
                INC     pf_ip+1
                BNE     pfar_done
                INC     pf_ip
pfar_done:
                RTS

;==============================================================================
; ファイル入出力ルーチン群の置き場所については 7t_cmt_io.inc を参照。
;==============================================================================
                include "7t_using_num1.inc"     ; PRINT USING 数値整形 1/4 (プール)
; PRINT USING — 終端処理
;   書式 1 周で値を 1 つも消費できなければ Illegal Function Call (動作観察。
;   USING を式評価へ流すと未消費のまま同一項を無限に印字するため、ここで
;   打ち切る)。値が残っていれば書式再利用 (動作観察)。値切れでフィールドに達した
;   ときは末尾 ';' でも改行して打切る (動作観察)。
;   本片は空きプール。
;==============================================================================
ipu_fmt_end:
        LDA     UF_FLAGS,S
        BITA    #1              ; 値保留?
        BEQ     ipu_finish
        BITA    #2              ; 当周で消費した?
        BEQ     errc_5          ; 無消費 → Illegal Function Call
        JMP     ipu_pass        ; 書式再利用
ipu_starve:
        JSR     disk_crlf       ; 行送りは共通 (CR + LF)
        BRA     ipu_dealloc
ipu_finish:
        LDA     UF_FLAGS,S
        BITA    #4              ; 末尾 ';' ?
        BNE     ipu_dealloc
        JSR     disk_crlf       ; 行送りは共通 (CR + LF)
ipu_dealloc:
        LEAS    UF_SIZE,S       ; 作業枠を捨てる
        RTS

; flt_zero1: FAC1 を単精度の値ゼロ (指数 0・仮数 0・ガード 0) にして FACTYPE=1。
;   加算・乗算・除算の各ゼロ経路で共用する。破壊: A,CC。X/Y/U 温存。
flt_zero1:
                CLR     FAC1
                CLR     FAC1+1
                CLR     FAC1+2
                CLR     FAC1+3
                CLR     FAC1+4
                CLR     FAC1+5
; >>FOLD fac1_fin_sng
;------------------------------------------------------------------------------
; fac1_fin_sng — 演算結果の仕上げ。ガード桁を 0 にし、型を単精度にして復帰する。
;   直前の CLR FAC1+5 (flt_zero1) から落ち込む経路もある。
;   破壊: A。
;------------------------------------------------------------------------------
fac1_fin_sng:
                CLR     FAC1+6
                CLR     FAC1+7
                JMP     fac_type_sng    ; 型=単精度にして復帰 (共通化)

;------------------------------------------------------------------------------
; errc_5 / errc_8 / errc_2 — 誤り送出の共通スタブ (容量捻出)
;   各所へ `LDB #n : JMP impl_error_handler` (5 byte) を展開する代わりに、
;   出現頻度の高い 3 種だけをここへ 1 本ずつ置き、呼出側は `JMP errc_n`
;   (3 byte) に畳む。誤り番号は参考書籍の誤り一覧に従う:
;     5 = Illegal Function Call / 8 = Undefined Line Number / 2 = Syntax Error
;   (本片は空きプールに置く。位置依存はない)
;------------------------------------------------------------------------------
errc_5:
                LDB     #5
                BRA     errc_go
errc_8:
                LDB     #8
                BRA     errc_go
errc_6:
                LDB     #6
                BRA     errc_go
errc_11:
                LDB     #11
                BRA     errc_go
errc_2:
                LDB     #2
errc_go:
                JMP     impl_error_handler

                include "7t_strcpy.inc"         ; 実体の複写 (区画の都合でここへ)

;------------------------------------------------------------------------------
; dsv_alloc — FAT バッファ (str_work_buf) から最小番号の空き ($FF) クラスタを確保する
;   出力: B = クラスタ番号。確保した位置は仮終端 $C7 で占有済にする (次の割当で
;         再選択されないように。真の終端値は書き終わりに dsv_wfat が置き直す)。
;   空きが無ければ Disk Full で中止 (戻らない)。破壊: A,X
;------------------------------------------------------------------------------
dsv_alloc:
                LDX     #str_work_buf+5 ; 割当表エントリ先頭 (クラスタ 0)
                CLRB
dsa_lp:
                LDA     ,X
                CMPA    #$FF
                BEQ     dsa_ok
                LEAX    1,X
                INCB
                CMPB    #152            ; クラスタ 0-151 (参考書籍)
                BNE     dsa_lp
                LDB     #67             ; Disk Full (参考書籍)
                JMP     dsv_errb
dsa_ok:
                LDA     #$C7
                STA     ,X              ; 仮終端で占有
                RTS

                include "7t_using_round.inc"    ; PRINT USING 四捨五入 (プール)

                include "7t_strinit.inc"        ; 文字列領域の張り直し

                include "7t_double2.inc"        ; 倍精度展開/パック (プール)
                include "7t_using_fin.inc"      ; PRINT USING 終端処理 (プール)
;------------------------------------------------------------------------------
; brk_pre — Break 表示の前置き。プール配置 (呼出は JSR 拡張のみ)。
;   まず現在行を無条件に閉じ、続けてエラー表示と同じ前置き (err_pre) を通す。
;   結果として Break の前だけ空行が 2 行になる。
;------------------------------------------------------------------------------
brk_pre:
                JSR     disk_crlf
;------------------------------------------------------------------------------
; err_pre — エラー表示の前置き。プール配置 (呼出は JSR 拡張のみ)。
;   演奏を止め (参考書籍)、表示前の行送りを行う。呼出元
;   (impl_error_handler の属する区間) は 当区間の残余が 0 なので、
;   増分をここへ寄せている。
;------------------------------------------------------------------------------
err_pre:
                JSR     play_stop
                JMP     nl_blank
; >>FOLD dbl_shl_dec
;------------------------------------------------------------------------------
; dbl_shl_dec — 倍精度の正規化 1 歩。仮数を 1 桁左送りし、指数を 1 減らす。
;   加減算・展開・十進取込の 3 箇所で使う。RTS は CC を変えないので、呼出側は
;   STD dw1_exp の結果で分岐できる。
;   直前は JMP なので、この定義へ落ち込む経路は無い。破壊: A,B,X。
;------------------------------------------------------------------------------
dbl_shl_dec:
                JSR     mshl8_dw1
                LDD     dw1_exp
                SUBD    #1
                STD     dw1_exp
                RTS
; <<FOLD dbl_shl_dec


print_float:
                PSHS    X               ; 本文ポインタ退避 (putchar は X/B 保存)
                ; 符号
                LDA     FAC1+5
                ANDA    #$80
                BEQ     pf_pos
                LDA     #'-'
                BRA     pf_sgo
pf_pos:
                LDA     #' '
pf_sgo:
                JSR     impl_putchar
                CLR     FAC1+5          ; 符号は出力済 → 以降は magnitude で処理
                LDA     FAC1            ; 指数=0 → 値ゼロ
                BNE     pf_nz
                JSR     put_zero
                LBRA    pf_end
pf_nz:
                ; 表示形式の判定: |v|≥1E6、または |v|<0.1 で第 7 位が要るなら指数表記(E形式)へ。
                ;   閾値内 (0.1≤|v|<1E6、および 0.1 未満でも第 6 位までで表せる値)
                ;   は通常表記 (fixed) の経路をそのまま使う。
                JSR     pfe_try         ; 閾値外なら E 形式で出力し carry セット
                BCC     pf_fixed        ; carry clear → 通常表記
                LBRA    pf_end
; pf_dacc_load — FAC1 の仮数を表示用 64bit 作業域へ読み込み、2^(指数-127) 倍する。
;   高 4 バイト = 整数部、低 4 バイト = 小数部。通常表記と E 形式の共通の並び
;   (破壊: A,B)。
pf_dacc_load:
                CLR     pf_dacc
                CLR     pf_dacc+1
                CLR     pf_dacc+2
                CLR     pf_dacc+3
                LDD     FAC1+1
                STD     pf_dacc+4
                LDD     FAC1+3
                STD     pf_dacc+6
                LDA     FAC1
                SUBA    #127
                BEQ     pdl_done
                BMI     pdl_shr
pdl_shl:
                ASL     pf_dacc+7
                ROL     pf_dacc+6
                ROL     pf_dacc+5
                ROL     pf_dacc+4
                ROL     pf_dacc+3
                ROL     pf_dacc+2
                ROL     pf_dacc+1
                ROL     pf_dacc
                DECA
                BNE     pdl_shl
pdl_done:
                RTS
pdl_shr:
                NEGA
pdl_shr_lp:
                LSR     pf_dacc
                ROR     pf_dacc+1
                ROR     pf_dacc+2
                ROR     pf_dacc+3
                ROR     pf_dacc+4
                ROR     pf_dacc+5
                ROR     pf_dacc+6
                ROR     pf_dacc+7
                DECA
                BNE     pdl_shr_lp
                RTS
pf_fixed:
                ; dacc = M << (exp-127) : 高4=整数部, 低4=小数部
                BSR     pf_dacc_load    ; 読み込みと桁送りは E 形式と共通の 1 本
pf_shifted:
                ; ---- 事前丸め: 先に整数桁数だけを数え (pf_ip は退避/復元)、
                ;      0.5*10^-nfrac を加算してから桁抽出する。桁上げは整数部へ
                ;      伝播する ----
                LDD     pf_ip
                PSHS    D
                LDD     pf_ip+2
                PSHS    D                   ; pf_ip (4B) 退避
                CLR     pf_nip
                JSR     pf_ip_or32
                BEQ     pfsh_cnt0
pfsh_cnt:
                JSR     div32by10
                INC     pf_nip
                JSR     pf_ip_or32
                BNE     pfsh_cnt
pfsh_cnt0:
                PULS    D
                STD     pf_ip+2
                PULS    D
                STD     pf_ip               ; pf_ip 復元
                LDA     #6
                SUBA    pf_nip
                BPL     pfsh_nfok
                CLRA
pfsh_nfok:
                STA     pf_nfrac
                STA     pf_fcount
                JSR     pf_add_round        ; 最下位表示桁で最近接丸め (桁上げ伝播)
                ; 整数部桁をスタックへ (LSB 先), nip 再カウント (繰上げ追随)
                CLR     pf_nip
                JSR     pf_ip_or32
                BEQ     pf_ipdone
pf_ip_lp:
                JSR     div32by10
                ADDA    #'0'
                PSHS    A
                INC     pf_nip
                BSR     pf_ip_or32
                BNE     pf_ip_lp
pf_ipdone:
                ; 丸め繰上げで 7 桁 (= ちょうど 1000000) になったら E 形式へ昇格
                ; (動作観察)。7 桁は 999999+1 の繰上げでのみ起きる。
                LDA     pf_nip
                CMPA    #7
                BNE     pf_nf6
                LEAS    7,S                 ; 押した 7 桁を破棄
                JSR     print_inline
                FCC     "1E+06"         ; 丸め繰上げによる 1E+06 昇格
                FCB     0
                BRA    pf_end
pf_nf6:
                ; nfrac = 6 - nip (0..6, 繰上げで桁が増えた場合も追随)
                LDA     #6
                SUBA    pf_nip
                BPL     pf_nfok
                CLRA
pf_nfok:
                STA     pf_nfrac
                STA     pf_fcount
                ; 小数桁生成 → pf_fbuf
                LDX     #pf_fbuf
pf_fr_lp:
                LDA     pf_fcount
                BEQ     pf_fr_done
                JSR     fracx10
                ADDA    #'0'
                STA     ,X+
                DEC     pf_fcount
                BRA     pf_fr_lp
pf_fr_done:
                ; 末尾ゼロ抑制
pf_strip:
                LDA     pf_nfrac
                BEQ     pf_strip_done
                LDX     #pf_fbuf
                LDB     pf_nfrac
                ABX
                LDA     -1,X
                CMPA    #'0'
                BNE     pf_strip_done
                DEC     pf_nfrac
                BRA     pf_strip
pf_strip_done:
                ; 出力有無フラグ = nip | nfrac
                LDA     pf_nip
                ORA     pf_nfrac
                STA     pf_tmp
                ; 整数部出力 (スタックから MSB 先)
                LDA     pf_nip
                BEQ     pf_out_noip
pf_out_ip:
                PULS    A
                JSR     impl_putchar
                DEC     pf_nip
                BNE     pf_out_ip
pf_out_noip:
                ; 小数部出力
                LDA     pf_nfrac
                BEQ     pf_out_nofrac
                JSR     put_dot
                LDX     #pf_fbuf
                LDB     pf_nfrac
pf_out_fr:
                JSR     put_x_inc
                DECB
                BNE     pf_out_fr
pf_out_nofrac:
                LDA     pf_tmp
                BNE     pf_end
                JSR     put_zero
pf_end:
                JSR     put_space               ; 空白 1 個を出す
                PULS    X,PC

;==============================================================================
; 単精度浮動小数 四則 (加減乗比較) + 小数リテラル — プール本体
;
;   本来の区間の外 ($CE03 以降の空きプール) に本体を置き、算術セグメントの
;   トランポリンから JMP される。整数のみのオペランドは 16bit の経路を
;   そのまま通し結果ビットを不変に保つ (型二重パス)。片方でも単精度なら
;   float 昇格して float コアで演算する。
;
;   FAC 展開形 (8B): +0 指数(bias128,0=零) / +1..+4 仮数32bit(先頭1明示) /
;                    +5 符号(bit7) / +6..+7 ガード。FAC2=+0..+7。
;   結果型: 両オペランド整数なら整数(FACTYPE=0)、片方でも単精度なら単精度(1)。
;==============================================================================

; push_fac1: FAC1(8B) + FACTYPE を HW スタックへ積み、戻り後も残す。
;   ネスト式のため 16bit 退避に代えて FAC 全体+型を退避する。破壊: Y (直後に
;   JSR expr_* が続くため呼出元は Y を当てにしない)。X/U/D/CC は温存。
negd:                                   ; D = -D
                COMA
                COMB
                ADDD    #1
                RTS
pf_ip_or32:                             ; pf_ip 32bit が 0 か調べる
                LDA     pf_ip
                ORA     pf_ip+1
                ORA     pf_ip+2
                ORA     pf_ip+3
                RTS
push_fac1_x1:                           ;!NOTAIL 1 進めてから FAC1 を積む
                LEAX    1,X
push_fac1:                              ;!NOTAIL 自身の戻りアドレスを PULS する
                PULS    D               ; D = 戻りアドレス (A,B のみ破壊; Y は温存)
                STD     flt_ret
                LDD     FAC1
                PSHS    D
                LDD     FAC1+2
                PSHS    D
                LDD     FAC1+4
                PSHS    D
                LDD     FAC1+6
                PSHS    D
                LDA     FACTYPE
                PSHS    A
                JMP     [flt_ret]       ; 9B を残して復帰

; pop_to_fac2: push_fac1 が積んだ 9B を FAC2 + FAC2TYPE へ取り出す。
;   FAC1 (右オペランド, expr_* が残した) は保持。破壊: Y,A,D,CC。X/U 温存。
pop_to_fac2:                            ;!NOTAIL 自身の戻りアドレスを PULS する
                PULS    D               ; D = 戻りアドレス (Y 温存)
                STD     flt_ret
                PULS    A               ; 左オペランド型
                STA     FAC2TYPE
                PULS    D
                STD     FAC2+6
                PULS    D
                STD     FAC2+4
                PULS    D
                STD     FAC2+2
                PULS    D
                STD     FAC2
                JMP     [flt_ret]

; ensure_operands_float: FAC1/FAC2 のうち整数型のものを float 昇格する。
;   impl_int_to_float は FAC1 のみ, iitf2 は FAC2 のみ触るため相互干渉なし。
ensure_operands_float:
                LDA     FACTYPE
                BNE     eof_r
                JSR     impl_int_to_float   ; FAC1 int→float (FACTYPE=1)
eof_r:
                LDA     FAC2TYPE
                BNE     eof_done
                BSR     iitf2               ; FAC2 int→float
eof_done:
                RTS

; iitf2: FAC2+2 の符号付き16bit整数 → FAC2 (float 展開形)。impl_int_to_float の
;   FAC2 向け。破壊: A,B,CC。X/Y/U 温存。
iitf2:
                LDD     FAC2+2
                BEQ     iitf2_zero
                BPL     iitf2_pos
                BSR     negd
                PSHS    D
                LDA     #$80
                STA     FAC2+5
                PULS    D
                BRA     iitf2_mag
iitf2_pos:
                CLR     FAC2+5
iitf2_mag:
                STD     FAC2+1
                CLR     FAC2+3
                CLR     FAC2+4
                CLR     FAC2+6
                CLR     FAC2+7
                LDA     #143
                STA     FAC2
iitf2_norm:
                LDA     FAC2+1
                BMI     iitf2_done
                ASL     FAC2+4
                ROL     FAC2+3
                ROL     FAC2+2
                ROL     FAC2+1
                DEC     FAC2
                BRA     iitf2_norm
iitf2_done:
                RTS
iitf2_zero:
                CLR     FAC2
                CLR     FAC2+1
                CLR     FAC2+2
                CLR     FAC2+3
                CLR     FAC2+4
                CLR     FAC2+5
                RTS

; 定数 10.0 (float): 指数$83(=128+3), 仮数$A0000000(=1.25), 符号+ → 1.25*8=10
ften:
                FCB     $83,$A0,$00,$00,$00,$00

; --- 型二重パス トランポリン本体 (算術セグメントから JMP される) ---
; 整数のあふれ: 16 ビットに収まらない結果になったら単精度で計算し直す。
;   参考書籍のとおり、整数どうしの加減乗の結果が整数の範囲
;   (-32768..32767) を超えると値は実数 (単精度) になる。以下の各パスは「まず 16 ビットで試し、あふれたら
;   両オペランドを単精度へ昇格して同じ演算をやり直す」形にしてある
;   (オペランドは整数パスでは書き換えないので、そのまま昇格できる)。
fp_add_impl:
                LDA     FAC2TYPE
                ORA     FACTYPE
                BNE     fpa_float
                LDD     FAC2+2
                ADDD    FAC1+2
                BVS     fpa_flt2            ; あふれ → 単精度で計算し直す
                JMP     impl_int_to_fac     ; 整数パス
fpa_float:
                JSR     dchk                ; どちらかが倍精度 → 倍精度コア
                LBCS    fpa_dbl
fpa_flt2:
                BSR     ensure_operands_float
                BRA     fadd_core

fp_sub_impl:
                LDA     FAC2TYPE
                ORA     FACTYPE
                BNE     fps_float
                LDD     FAC2+2
                SUBD    FAC1+2
                BVS     fps_flt2            ; あふれ → 単精度で計算し直す
                JMP     impl_int_to_fac
fps_float:
                JSR     dchk                ; どちらかが倍精度 → 倍精度コア
                LBCS    fps_dbl
fps_flt2:
                JSR     ensure_operands_float
                JSR     fac1_flip_sign
                BRA     fadd_core

; 整数どうしの乗算。
;   両オペランドが 0..255 のときは 8x8 積が 32767 以下である限り 16 ビットの
;   積として確定する。その枠を外れる組合せ (どちらかが 256 以上/負、または
;   積が 32768 以上) は単精度で計算し直す。16 ビットに収まる積は単精度でも
;   誤差なく表せるので、単精度へ回した場合でも値は常に同じになる。
fp_mul_impl:
                LDA     FAC2TYPE
                ORA     FACTYPE
                BNE     fpm_float
                LDA     FAC2+2              ; 左辺の上位バイト
                ORA     FAC1+2              ; 右辺の上位バイト
                BNE     fpm_flt2            ; どちらかが 0..255 の外
                LDA     FAC2+3
                LDB     FAC1+3
                MUL                         ; D = 左辺 * 右辺 (0..65025)
                TSTA
                BMI     fpm_flt2            ; 32768 以上 → 16 ビットに収まらない
                JMP     impl_int_to_fac
fpm_float:
                JSR     dchk                ; どちらかが倍精度 → 倍精度コア
                LBCS    fpm_dbl
fpm_flt2:
                JSR     ensure_operands_float
                JMP     fmul_core

fp_cmp_impl:
                LDA     FAC2TYPE
                ORA     FACTYPE
                BNE     fpc_float
                LDD     FAC2+2
                CMPD    FAC1+2
                RTS
fpc_float:
                JSR     dchk                ; どちらかが倍精度 → 倍精度コア
                LBCS    fpc_dbl
                JSR     ensure_operands_float
                JMP     fcmp_core

fdiv_dispatch:
                ; 整数どうしも float へ昇格してから実数除算する (/ は常に実数
                ; 除算。参考書籍。ゼロ除算は fdiv_core の指数 0 検査が拾う)。
                JSR     dchk                ; どちらかが倍精度 → 倍精度コア
                LBCS    fpd_dbl
                JSR     ensure_operands_float
                JMP     fdiv_core           ; FAC2/FAC1 (両 float)

; --- 空きプール ---
;------------------------------------------------------------------------------
; err_tmm — Type Mismatch の送出口。
err_tmm:
                LDB     #13             ; Type Mismatch
                JMP     impl_error_handler

;------------------------------------------------------------------------------
; dsv_dirfull — ディスク SAVE: 書込先スロットが無い (Directory Full) の中止口
;   (本体は 7t_diskboot.inc の dsv_body。置き場所は容量の都合)
;------------------------------------------------------------------------------
dsv_dirfull:
                LDB     #65             ; Directory Full (参考書籍)
                LBRA    dsv_errb


;------------------------------------------------------------------------------
; fadd_core: FAC1 = FAC2 + FAC1 (両 float 展開形)。指数合わせ→仮数加減→正規化
;   →ガード丸め。結果 FACTYPE=1。X/Y/U 温存。
;------------------------------------------------------------------------------
fadd_core:
                LDA     FAC2
                BNE     fac_2nz
                JMP     fac_type_sng    ; 型=単精度にして復帰 (共通化)
fac_2nz:
                LDA     FAC1
                BNE     fac_both
                LDD     FAC2                ; FAC1=0 → 結果=FAC2
                STD     FAC1
                LDD     FAC2+2
                STD     FAC1+2
                LDD     FAC2+4
                STD     FAC1+4
                JMP     fac_type_sng    ; 型=単精度にして復帰 (共通化)
fac_both:
                CMPA    FAC2                ; (A はすぐ上で読んだ FAC1 の指数のまま)
                BHS     fac_1big
                ; FAC2 の指数が大: a=FAC2, b=FAC1
                LDD     FAC2+1
                STD     fa_a
                LDD     FAC2+3
                STD     fa_a+2
                LDD     FAC1+1
                STD     fa_b
                LDD     FAC1+3
                STD     fa_b+2
                LDA     FAC2
                STA     fa_exp
                SUBA    FAC1                ; diff = e2 - e1 (A は FAC2 の指数のまま)
                STA     fa_cnt
                LDA     FAC2+5
                STA     fa_sgn
                EORA    FAC1+5              ; (A は FAC2+5 のまま)
                BRA     fac_haveab
fac_1big:
                ; FAC1 の指数が大 (等指数も含む): a=FAC1, b=FAC2
                LDD     FAC1+1
                STD     fa_a
                LDD     FAC1+3
                STD     fa_a+2
                LDD     FAC2+1
                STD     fa_b
                LDD     FAC2+3
                STD     fa_b+2
                LDA     FAC1
                STA     fa_exp
                SUBA    FAC2                ; diff = e1 - e2 (A は FAC1 の指数のまま)
                STA     fa_cnt
                LDA     FAC1+5
                STA     fa_sgn
                EORA    FAC2+5              ; (A は FAC1+5 のまま)
fac_haveab:
                ANDA    #$80                ; bit7 = 符号が異なる
                PSHS    A
                LDA     fa_cnt
                CMPA    #33
                BLO     fac_shiftb
                LEAS    1,S                 ; b は無視できる → 結果=a (要 24bit 丸め)
                LBRA    fac_round
fac_shiftb:
                LDA     fa_cnt
                BEQ     fac_aligned
fac_shb_lp:
                LSR     fa_b
                ROR     fa_b+1
                ROR     fa_b+2
                ROR     fa_b+3
                DECA
                BNE     fac_shb_lp
fac_aligned:
                PULS    A                   ; 符号が異なるか (PULS は CC 不変ゆえ TSTA 要)
                TSTA
                BMI     fac_subtract
                ; --- 同符号: 加算 fa_a += fa_b (4B) ---
                LDD     fa_a+2              ; 下位 16 ビット
                ADDD    fa_b+2
                STD     fa_a+2
                LDD     fa_a                ; 上位 16 ビット (桁上げを ADCB → ADCA へ連鎖)
                ADCB    fa_b+1
                ADCA    fa_b
                STD     fa_a
                BCC     fac_round           ; あふれ無し → 先頭1 は保持済
                ROR     fa_a                ; あふれ: C(=1)を最上位へ入れ右1シフト
                ROR     fa_a+1
                ROR     fa_a+2
                ROR     fa_a+3
                INC     fa_exp
                BRA    fac_round
fac_subtract:
                ; --- 異符号: |a|-|b| か |b|-|a| ---
                ;   4B の大小比較。上位 16 ビット → 下位 16 ビットの順に
                ;   ワード (2 バイト) 単位で比べる (符号なし比較なので BHI / BLO)。
                LDD     fa_a
                CMPD    fa_b
                BHI     fac_a_ge
                BLO     fac_b_gt
                LDD     fa_a+2
                CMPD    fa_b+2
                BHI     fac_a_ge
                BLO     fac_b_gt
                BRA     fac_zero            ; |a|==|b| → 0
fac_b_gt:
                ; |b|>|a|: fa_a = fa_b - fa_a, 符号は b 側 (a の反対)
                LDA     fa_sgn
                EORA    #$80
                STA     fa_sgn
                LDD     fa_b+2              ; 下位 16 ビット
                SUBD    fa_a+2
                STD     fa_a+2
                LDD     fa_b                ; 上位 16 ビット (借りを SBCB → SBCA へ連鎖)
                SBCB    fa_a+1
                SBCA    fa_a
                STD     fa_a
                BRA     fac_normalize
fac_a_ge:
                LDD     fa_a+2              ; 下位 16 ビット
                SUBD    fa_b+2
                STD     fa_a+2
                LDD     fa_a                ; 上位 16 ビット (借りを SBCB → SBCA へ連鎖)
                SBCB    fa_b+1
                SBCA    fa_b
                STD     fa_a
fac_normalize:
                LDA     fa_a
                BMI     fac_round           ; bit31 立ち → 正規化完了
                ORA     fa_a+1              ; (A はすぐ上で読んだ fa_a のまま)
                ORA     fa_a+2
                ORA     fa_a+3
                BEQ     fac_zero
                ASL     fa_a+3
                ROL     fa_a+2
                ROL     fa_a+1
                ROL     fa_a
                DEC     fa_exp
                BEQ     fac_zero            ; 指数 0 → アンダーフロー
                BRA     fac_normalize

fac_round:
                ; 単精度の有効仮数は 24bit。その直下のバイト fa_a+3 をガードとし、
                ; 1/2ulp 以上なら絶対値を切り上げる (タイはゼロから遠い側)。
                LDA     fa_a+3
                CLR     fa_a+3              ; 24bit 未満の桁は残さない
                CMPA    #$80
                BLO     fac_store_a
                INC     fa_a+2              ; 仮数 3B へ +1 (桁上がりだけ上位へ伝播)
                BNE     fac_store_a
                INC     fa_a+1
                BNE     fac_store_a
                INC     fa_a
                BNE     fac_store_a
                ROR     fa_a                ; 丸めで桁上がり → 右1シフト+指数++
                ROR     fa_a+1
                ROR     fa_a+2
                INC     fa_exp
fac_store_a:
                LDA     fa_exp
                ; 加減算の結果指数が単精度の格納範囲を超えたら Overflow (6) で
                ; 止める。同符号加算の桁上がりや丸めの繰り上がりで内部展開形の
                ; 指数が上限 254 を超えると、検査なしでは範囲外の値が黙って
                ; 格納され (格納形への +1 で指数が 0 へ巻き付き、値ゼロに化ける)、
                ; 指数 254 超を Overflow にする乗除算と挙動が食い違う
                ; (参考書籍の誤り一覧 6 = Overflow)。
                LBEQ    errc_6          ; 指数が 2 回の繰り上がりで 0 へ巻き付いた
                CMPA    #$FF
                LBEQ    errc_6          ; 指数 255 = 内部展開形の上限 254 超
                STA     FAC1
                LDD     fa_a
                STD     FAC1+1
                LDD     fa_a+2
                STD     FAC1+3
                LDA     fa_sgn
                ANDA    #$80
                STA     FAC1+5
                JMP     fac1_fin_sng    ; ガード 0 + 型=単精度で復帰 (共通化)
fac_zero:
                JMP     flt_zero1       ; 値ゼロの単精度 (共通本体)

;------------------------------------------------------------------------------
; fmul_core: FAC1 = FAC2 * FAC1 (両 float)。指数加算-bias, 32x32→64bit 右シフト
;   積, 正規化, ガード丸め。結果 FACTYPE=1。X/Y/U 温存。
;------------------------------------------------------------------------------
fmul_core:
                LDA     FAC1
                LBEQ    fmul_zero
                LDA     FAC2
                LBEQ    fmul_zero
                LDA     FAC1+5              ; 符号 = s1 XOR s2
                EORA    FAC2+5
                ANDA    #$80
                PSHS    A
                CLRA                  ; exp16 = e1 + e2 - 127
                LDB     FAC1
                ADDB    FAC2
                ADCA    #0
                SUBD    #127
                PSHS    D                   ; [exp16 hi][exp16 lo][sign]
                LDD     FAC1+1
                STD     fm_plo
                LDD     FAC1+3
                STD     fm_plo+2
                LDD     FAC2+1
                STD     fm_m2
                LDD     FAC2+3
                STD     fm_m2+2
                JSR     fmul_bmul           ; MUL の部分積で 64bit 厳密積を作る
                                            ;   (本体は 7t_startup.inc 末尾のプール)
                ; P = fm_phi:fm_plo。bit63 が 0 なら左1シフト+指数--
                LDA     fm_phi
                BMI     fmul_normok
                ASL     fm_plo+3
                ROL     fm_plo+2
                ROL     fm_plo+1
                ROL     fm_plo
                ROL     fm_phi+3
                ROL     fm_phi+2
                ROL     fm_phi+1
                ROL     fm_phi
                LDD     ,S
                SUBD    #1
                STD     ,S
fmul_normok:
                ; 有効仮数 24bit の直下 fm_phi+3 をガードとして最近接丸め。
                ; ガード=1/2ulp ちょうどの場合も切り上げる (タイはゼロから遠い側)
                ; ため、それより下位 (fm_plo) は判定に影響しない。
                LDA     fm_phi+3
                CLR     fm_phi+3            ; 24bit 未満の桁は残さない
                CMPA    #$80
                BLO     fmul_noround
                INC     fm_phi+2            ; 積の上位 3B へ +1 (桁上がりだけ上位へ伝播)
                BNE     fmul_noround
                INC     fm_phi+1
                BNE     fmul_noround
                INC     fm_phi
                BNE     fmul_noround
                LDA     #$80                ; あふれ → $80000000, 指数++
                STA     fm_phi
                CLR     fm_phi+1
                CLR     fm_phi+2
                LDD     ,S
                ADDD    #1
                STD     ,S
fmul_noround:
                PULS    D                   ; exp16 (sign が 0,S に残る)
                CMPD    #1
                BLT     fmul_uf_pop         ; exp16 <= 0 → アンダーフロー
                CMPD    #254
                BLE     fmul_expok
                JMP     errc_6
fmul_expok:
                STB     FAC1
                LDD     fm_phi
                STD     FAC1+1
                LDD     fm_phi+2
                STD     FAC1+3
                PULS    A
                STA     FAC1+5
                JMP     fac1_fin_sng    ; ガード 0 + 型=単精度で復帰 (共通化)
fmul_uf_pop:
                LEAS    1,S                 ; sign を捨てる
fmul_zero:
                JMP     flt_zero1       ; 値ゼロの単精度 (共通本体)

;------------------------------------------------------------------------------
; fcmp_core: FAC2 と FAC1 を比較 (両 float)。A = +1(FAC2>FAC1)/-1(<)/0(=) を作り
;   TSTA で N/Z を確定 (erel の BLT/BGT/BEQ 契約: FAC2-FAC1 の符号)。X/Y/U 温存。
;------------------------------------------------------------------------------
fcmp_core:
                LDA     FAC2                ; 値ゼロの符号は正に正規化
                BNE     fcm_2nz
                CLR     FAC2+5
fcm_2nz:
                LDA     FAC1
                BNE     fcm_1nz
                CLR     FAC1+5
fcm_1nz:
                LDA     FAC2+5              ; 符号相違は XOR の bit7 で検出
                EORA    FAC1+5
                BMI     fcm_diff
                ; 同符号: 指数+仮数 5 バイトを上位から一括比較 (FAC1 側は FAC2 からの
                ;   相対で参照する)
                PSHS    X
                LDX     #FAC2
fcm_lp:
                LDB     ,X+
                CMPB    FAC1-FAC2-1,X       ; FAC2 側バイト vs FAC1 側バイト
                BNE     fcm_neq
                CMPX    #FAC2+5
                BNE     fcm_lp
                PULS    X                   ; (PULS は CC 不変)
                CLRA                        ; |FAC2|==|FAC1| → 等しい
                RTS
fcm_neq:
                PULS    X
                BLO     fcm_mag_lt          ; C: |FAC2| < |FAC1|
                ; |FAC2| > |FAC1|
                TST     FAC2+5              ; 共通符号 (負なら |大|=値小)
                BMI     fcm_lt
fcm_gt:
                LDA     #1                  ; LDA が N/Z/V を確定
                RTS
fcm_mag_lt:
                TST     FAC2+5
                BMI     fcm_gt
fcm_lt:
                LDA     #$FF
                RTS
fcm_diff:
                TST     FAC2+5              ; 符号相違: FAC2 が正なら GT
                BPL     fcm_gt
                BRA     fcm_lt

; MOD: FAC2 mod FAC1 → FAC1  (余りの符号は被除数 FAC2 に従う)
;   (置き場所は容量の都合。div16s は 7t_expr.inc)
impl_fp_mod:
                JSR     impl_fac_to_int_round ; 右辺(除数)を整数化
                STD     FAC1+2
                BNE     fmod_ok
                JMP     errc_11
fmod_ok:
                PSHS    X               ; 本文ポインタ退避
                LDX     FAC2+2          ; 被除数
                LDD     FAC1+2          ; 除数
                JSR     div16s          ; Y = 余り
                TFR     Y,D
                JSR     impl_int_to_fac
                PULS    X,PC

; (pn_decimal 本体は後方の空きプール (連携サービス入口の後) に置く。仮数を
;  16bit で累算すると 5 桁超の整数・多桁小数であふれるため、そこでは
;  より広い累算幅と、トークン化済み演算子を含む指数 E の符号を扱う)

;==============================================================================
; 単精度 内部形式 パック/アンパック + FIX + FOR(実数) 拡張
;   内部形式 (単精度 4B)
;   のパック形式:
;     [0] 指数 (bias128, 0=値ゼロ)
;     [1] 仮数 MSB (bit7=符号, bit6-0=仮数 b30..b24。明示先頭1は符号へ差替)
;     [2] 仮数 b23..b16
;     [3] 仮数 b15..b8
;   FAC 展開形 (+0指数/+1..+4仮数32bit(先頭1明示)/+5符号/+6..+7ガード) と相互変換。
;   整数値 (FACTYPE=0) は impl_int_to_float で float 化してから格納する
;   (既定型=単精度)。
;==============================================================================

; pack_sng: FAC1 (整数 or float) → U 指す 4B 内部形式。整数なら float 昇格。
;   破壊: A,B,CC。X/Y/U 温存 (U は入力=格納先を保持)。
pack_sng:
                JSR     dbl_demote      ; 倍精度は単精度へ落として格納
                LDA     FACTYPE
                BNE     pmb_isf
                JSR     impl_int_to_float
pmb_isf:
                LDA     FAC1            ; 指数 (内部展開形)
                BEQ     pmb_zero        ; 指数0 → 値ゼロ
                INCA                    ; 格納形の指数基準は内部展開形 +1
                STA     ,U
                LDA     FAC1+5          ; 符号 ($00/$80) → $7F/$FF
                ORA     #$7F
                ANDA    FAC1+1          ; 先頭1を落として符号ビットへ差し替え
                STA     1,U
                LDD     FAC1+2          ; 仮数 b23..b8
                STD     2,U
                RTS
pmb_zero:
                CLR     ,U
                CLR     1,U
                CLR     2,U
                CLR     3,U
                RTS

; unpack_sng: U 指す 4B 内部形式 → FAC1 展開形, FACTYPE=1。破壊: A,B,CC。X/Y/U 温存。
;   格納形の指数基準は内部展開形 +1 (仮数 [0.5,1) 規約) ゆえ 1 だけ下げて渡す。
unpack_sng:
                LDA     ,U              ; 指数
                BEQ     umb_zero
                DECA
                STA     FAC1
                LDA     1,U
                ANDA    #$80            ; 符号
                STA     FAC1+5
                LDA     1,U
                ORA     #$80            ; 明示先頭1を復元
                STA     FAC1+1
                LDD     2,U
                STD     FAC1+2
                CLR     FAC1+4          ; ガード
                JMP     fac1_fin_sng    ; ガード 0 + 型=単精度で復帰 (共通化)
umb_zero:
                JMP     flt_zero1

; unpack_sng_f2: U 指す 4B 内部形式 → FAC2 展開形。破壊: A,B,CC。X/Y/U 温存。
unpack_sng_f2:
                LDA     ,U
                BEQ     umf2_zero
                DECA                    ; 格納形 → 内部展開形 (指数基準 -1)
                STA     FAC2
                LDA     1,U
                ANDA    #$80
                STA     FAC2+5
                LDA     1,U
                ORA     #$80
                STA     FAC2+1
                LDD     2,U
                STD     FAC2+2
                CLR     FAC2+4
                CLR     FAC2+6
                CLR     FAC2+7
                RTS
umf2_zero:
                ; FAC2 のゼロ詰めは iitf2_zero と同一手順なのでそこへ寄せる。
                JMP     iitf2_zero

; float_to_int_trunc: FACTYPE=1 の FAC1 をゼロ方向切り捨て(FIX)し D=符号付き16bit。
;   value = (M/2^31) * 2^(exp-128), M=FAC1+1..+4 (bit31=明示先頭1)。
;   右シフト量 = 159-exp。exp<128(|値|<1)→0。exp>159(巨大)→左シフトし下位16bit。
;   FAC1 を破壊 (終端変換ゆえ許容)。破壊: A,B,X,CC。
float_to_int_trunc:
                JSR     dbl_demote      ; 倍精度は単精度へ落としてから変換
                LDA     FAC1
                BEQ    fti_zero        ; 指数0 → 0
                LDA     #159
                SUBA    FAC1            ; A = 159 - exp
                BMI     fti_big         ; exp>159 → 巨大
                CMPA    #32
                BHS     fti_zero        ; exp<128 → |値|<1 → 0
                JSR     fti_rsh8        ; 8 歩ぶんは 1 バイト移動でまとめる
fti_rsh:
                TSTA
                BEQ     fti_take
                JSR     fac1_shr32
                DECA
                BRA     fti_rsh
fti_take:
                LDD     FAC1+3          ; 下位16bit
                BRA     fti_sign
fti_big:
                LDA     FAC1
                SUBA    #159            ; exp-159
fti_big_lp:
                JSR     fac1_shl32
                DECA
                BNE     fti_big_lp
                LDD     FAC1+3
fti_sign:
                TST     FAC1+5
                BPL     fti_ret
                JSR     negd
fti_ret:
                RTS
fti_zero:
                LDD     #0
                RTS

;==============================================================================
; 変数 SNG 経路 / FOR 制御変数 / NEXT の 内部形式 本体 (トランポリンから JMP される)
;==============================================================================

; pool_next_loop: NEXT 本体。レコード var_addr の bit15 で INT/SNG を切替。
pool_next_loop:
                ; 最も近い繰り返しの枠を探す。その内側に残った WHILE の枠 (GOTO で
                ; 抜けたもの) は捨てる。サブルーチンの枠を越えては探さない。
                LDB     #TAG_FOR
                JSR     stk_find
                BNE     pnl_err
                CMPX    EXEC_SP
                BEQ     pnl_top
                JSR     frame_del       ; 内側の枠を捨てる
                LDX     EXEC_SP
pnl_top:                                ; X = 枠 (= 枠列の先頭)
                LDD     1,X             ; var_addr(+フラグ)
                BMI     pnl_sng
                ; ===== INT パス (16bit) =====
                LDU     1,X
                LDD     ,U
                ADDD    9,X
                ; 制御変数 + 増分 が整数の範囲 (-32768..32767) を外れたら
                ; Overflow で止める。検査が無いと 32767+1 が -32768 へ巻き付き、
                ; FOR I%=32765 TO 32767 が終わらずに周回し続ける。巻き付いた値を
                ; 制御変数へ書かないよう、格納の前に判定する
                ; (参考書籍の誤り一覧 6 = Overflow)。
                LBVS    errc_6
                STD     ,U
                LDD     9,X
                BMI     pnl_ineg
                LDD     ,U
                CMPD    5,X
                BGT     pnl_done
                BRA     pnl_cont
pnl_ineg:
                LDD     ,U
                CMPD    5,X
                BLT     pnl_done
                BRA     pnl_cont
pnl_sng:
                ; 制御変数 (内部形式) → FAC1
                LDD     1,X
                ANDA    #$7F
                TFR     D,U
                JSR     unpack_sng
                ; 増分 (内部形式, 枠+7) → FAC2
                LEAU    7,X
                JSR     unpack_sng_f2
                JSR     fadd_core       ; FAC1 = 変数 + 増分
                ; 変数へ書き戻し
                LDD     1,X
                ANDA    #$7F
                TFR     D,U
                JSR     pack_sng
                ; 終値 (内部形式, 枠+3) → FAC2
                LEAU    3,X
                JSR     unpack_sng_f2
                JSR     fcmp_core       ; A = sign(終値 - 変数)
                LDB     8,X             ; 増分の符号 (内部形式 仮数MSB bit7)
                BMI     pnl_snegstep
                ; 増分>=0: 変数>終値 で終了 → (終値-変数)<0 → A<0
                TSTA
                BMI     pnl_done
                BRA     pnl_cont
pnl_snegstep:
                ; 増分<0: 変数<終値 で終了 → (終値-変数)>0 → A>0
                TSTA
                BGT     pnl_done
pnl_cont:
                LDD     11,X
                STD     CURIP
                LDD     13,X
                STD     CURLINE_PTR
                JSR     exec_ensure     ; ループ先頭の行を作業列へ復元 (IP は同値で有効)
                LDA     #1
                STA     in_cont_flag
                LDX     CURIP
                RTS
pnl_done:
                LEAX    FRM_FOR,X
                JSR     frame_del       ; 繰り返しの枠を捨てる
                CLR     in_cont_flag
                RTS
pnl_err:
                LDB     #1              ; Next Without For
                JMP     impl_error_handler

; (SNG 制御変数の FOR 構築 for_setup_sng は 7t_arrays.inc の空きプールに置く。
;  置き場所は容量の都合。)

; fss_alloc: 繰り返しの枠 (15B) を枠列へ積む。種別と var_addr(for_var_ptr) を格納。
;   返り U = 枠+3 (終値格納先)。X 温存。破壊: A,B,D。溢れは Out Of Memory (frame_ins)。
fss_alloc:
                JSR     pfor_reuse_scan ; 同一制御変数の既存の枠を捨てる
                PSHS    X               ; frame_ins は X を壊す (積んだ X は枠と一緒に降りる)
                LDB     #FRM_FOR
                JSR     frame_ins       ; U = 枠 (= 枠列の先頭)
                PULS    X
                LDA     #TAG_FOR
                STA     ,U
                LDD     for_var_ptr
                STD     1,U             ; var_addr(+フラグ)
                LEAU    3,U             ; U = 終値格納先
                RTS

;==============================================================================
; print_float 指数表記 (E形式) 拡張
;   参考書籍の単精度実数 PRINT 仕様 (有効6桁, 指数形式 E[±]nn) に準拠。
;   通常表記 (fixed) では表しきれない大小 magnitude を d.dddddE±nn 形式で出力する。
;   閾値: 0.1 ≤ |v| < 1E6 は通常表記, それ以外は E形式 (詳細は pfe_try)。
;   本体アドレスは固定入口ではなく, ROM 内から記号参照される内部ルーチン。
;   以降の配置 (末尾のベクタ (ワード (2 バイト))) は末尾 ZMB が長さを吸収して不変に保つ。
;==============================================================================

; --- 補助: FAC 8B コピー / 定数ロード ---
pf_f1tof2:                              ; FAC2 = FAC1 (8B)
                LDD     FAC1
                STD     FAC2
                LDD     FAC1+2
                STD     FAC2+2
                LDD     FAC1+4
                STD     FAC2+4
                LDD     FAC1+6
                STD     FAC2+6
                RTS
; flt_zero1 / fac1_fin_sng は本ファイル前半に置く。

; pool_fn_abs: ABS の本体。型を保ったまま符号だけを落とす (整数は単精度へ昇格)。
;   引数を 16bit 整数へ丸めると端数が落ちるので、丸めは行わない (ABS は大きさ
;   だけを返す)。
;   破壊: A,B,CC。X/Y/U 温存。
pool_fn_abs:
                LDA     FACTYPE
                CMPA    #2
                BEQ     pfab_dbl
                TSTA
                BNE     pfab_sng
                JSR     impl_int_to_float   ; 整数 (-32768 も含め) を単精度へ
pfab_sng:
                CLR     FAC1+5              ; 単精度展開形: 符号バイト
                RTS
pfab_dbl:
                LDA     FAC1+1              ; 倍精度パック形: 仮数 MSB の bit7 が符号
                ANDA    #$7F
                STA     FAC1+1
                RTS

; dbl_widen: FAC1 を倍精度パック形 (FACTYPE=2) へ広げる。既に倍精度なら素通し。
;   整数は単精度を経由する。単精度展開形 (指数/仮数32bit 先頭1明示/符号) から
;   倍精度パック形への変換は、仮数下位 24bit の位置が一致するので
;   「先頭1を符号へ差し替え・下位仮数 24bit をゼロ・指数を +1」で済む
;   (倍精度パックは仮数 f∈[0.5,1) 規約ゆえ指数基準が展開形より 1 大きい)。
;   破壊: A,B,CC。X/Y/U 温存。
dbl_widen:
                LDA     FACTYPE
                CMPA    #2
                BEQ     dw_rts
                TSTA
                BNE     dw_sng
                JSR     impl_int_to_float
dw_sng:
                ; 符号 ($00/$80) を $7F/$FF にしてから仮数 MSB と AND することで
                ; 「明示先頭1を落として符号ビットへ差し替える」を 1 手で行う
                LDA     FAC1+5
                ORA     #$7F
                ANDA    FAC1+1
                STA     FAC1+1
                CLR     FAC1+5          ; 倍精度パックの下位仮数 24bit
                CLR     FAC1+6
                CLR     FAC1+7
                LDA     FAC1
                BEQ     dw_t2           ; 値ゼロは指数 0 のまま
                INCA
                STA     FAC1
dw_t2:
                LDA     #2
                STA     FACTYPE
dw_rts:
                RTS

                include "7t_using_parse1.inc"   ; PRINT USING 10進解析 本体 (プール)
;------------------------------------------------------------------------------
; 連携サービス入口 — コンソール 1 行入力 (復帰: X=入力行バッファ, C=0 正常)
;   ディスク側システム連携の入口 (本体 7t_diskboot.inc)。
;------------------------------------------------------------------------------
                include "7t_strtotmp.inc"       ; 一時領域への複写 (区画の都合でここへ)
                include "7t_datitem.inc"        ; DATA/INPUT の項目判定 (プール)

;------------------------------------------------------------------------------
; chk_alpha — 1 字が英字であることを確かめ、A-Z を 0-25 の添字へ直す。
;   DEF 型宣言文の文字の範囲 (1 字の英字、または 英字-英字) と、変数名の
;   先頭字の検査が共用する。
;   入力: A = 字。出力: A = 添字 (0-25)。英字でなければ Syntax Error (復帰しない)。
;   ('A' を引いた値は、英字以外では必ず 26 以上 (符号なし) になるので 1 回の
;    比較で上下両方の外れを捕まえられる)
;   (呼出元の区画に残量が無いため本プールへ置く。参照はラベル経由のみ)
;------------------------------------------------------------------------------
chk_alpha:
                SUBA    #'A'
                CMPA    #26
                LBHS    errc_2
                RTS
                include "7t_double3.inc"        ; 倍精度型降格/ディスパッチ (プール)

                include "7t_using_exp4.inc"     ; PRINT USING 指数部 2 桁化 (プール)

; pfe_try: FAC1(magnitude, 非零) を見て閾値外なら E形式で出力し carry セットで復帰。
;   閾値内なら FAC1 を無傷で残し carry クリアで復帰 (呼出側が通常表記を続行)。
;   固定表記の条件:
;     |v| ≥ 1E6 → E形式 / 0.1 ≤ |v| < 1E6 → 固定 /
;     |v| < 0.1 → 最下位有効桁が小数第 6 位以内なら固定、第 7 位以下が要るなら
;     E形式。
pfe_try:
                JSR     pf_f1tof2       ; FAC2 = v
                LDX     #pfc_1e6
                BSR     pf_cmp_v        ; A = sign(v - X の定数) / FAC1 = v (共通化)
                BPL     pfe_go          ; v ≥ 1E6 → E形式
                JSR     pf_f1tof2       ; FAC2 = v
                LDX     #pfc_1e_1
                BSR     pf_cmp_v        ; A = sign(v - X の定数) / FAC1 = v (共通化)
                BMI     pfe_small       ; v < 0.1 → 小数第 6 位判定へ
                ANDCC   #$FE            ; carry クリア = 通常表記
                RTS
; >>FOLD pf_cmp_v
;------------------------------------------------------------------------------
; pf_cmp_v — 表示用の十進正規化で使う「値と定数の大小比較」の定型。
;   入力: X = 比較する定数のアドレス / FAC2 = 値 v。
;   出力: A = sign(v - 定数) (TSTA 済) / FAC1 = v (復元済)。
;   十進正規化の 4 箇所で使う。RTS は CC を変えないので、呼出側は BPL/BMI で
;   分岐できる。
;   直前は RTS なので、この定義へ落ち込む経路は無い。
;------------------------------------------------------------------------------
pf_cmp_v:
                JSR     pf_ldc1         ; FAC1 = 定数
                JSR     fcmp_core       ; A = sign(v - 定数)
                PSHS    A               ; pf_f2tof1 が A/B/CC を壊すため退避
                ; FAC1 = v (復元) (ここへ展開)
pf_f2tof1:                              ; FAC1 = FAC2 (8B)
                LDD     FAC2
                STD     FAC1
                LDD     FAC2+2
                STD     FAC1+2
                LDD     FAC2+4
                STD     FAC1+4
                LDD     FAC2+6
                STD     FAC1+6
                PULS    A
                TSTA
                RTS
; <<FOLD pf_cmp_v
pfe_go:
                BSR     pfe_output
                ORCC    #$01            ; carry セット = E形式で処理済
                RTS
pfe_small:
                ; v を退避し十進正規化を試行 → N(pf_ip, 6桁) と cnt(pfe_cnt)。
                ; 最下位有効桁の十進位置 = cnt + (N の末尾ゼロ数 t)。-6 以上なら
                ; 固定表記で表しきれる (それ未満は第 7 位以下が必要 → E形式)。
                LDX     #pf_vsave
; pf_save_v / pf_restore_v: FAC1 8B を X の指すバッファへ/から複写。破壊: A,B,CC。
pf_save_v:
                JSR     fx_st1          ; FAC1 の 6B を X へ (共通本体。X は保たれる)
                LDD     FAC1+6
                STD     6,X
                BSR     pfe_scale       ; FAC1 破壊 (退避済)
                CLRB                    ; B = t (末尾ゼロ数 0..6)
pfs_tz:
                PSHS    B
                JSR     div32by10       ; A = N % 10 (pf_ip 破壊で可)
                PULS    B
                TSTA
                BNE     pfs_tzd
                INCB
                CMPB    #6
                BLO     pfs_tz
pfs_tzd:
                ADDB    pfe_cnt         ; B = cnt + t (符号付き)
                PSHS    B
                LDX     #pf_vsave
                ; FAC1 = v (復元) (ここへ展開)
pf_restore_v:
                LDD     ,X
                STD     FAC1
                LDD     2,X
                STD     FAC1+2
                LDD     4,X
                STD     FAC1+4
                LDD     6,X
                STD     FAC1+6
                PULS    B
                CMPB    #-6
                BGE     pfs_fixed       ; 小数第 6 位以内 → 固定表記
                BSR     pfe_output      ; E形式 (内部で再スケール)
                ORCC    #$01
                RTS
pfs_fixed:
                ANDCC   #$FE
                RTS


; pfe_output: FAC1 = magnitude (正, 非零)。d.dddddE±nn を出力する。
;   十進スケールで |v| を [1E5,1E6) に収め (cnt=十進シフト数), N=round(s) の 6桁 +
;   指数 e10 = cnt+5 を得て整形する。X は print_float が退避済。
pfe_output:
                BSR     pfe_scale
                BRA     pfe_emit

; pfe_scale: |v| を十進スケールで [1E5,1E6) に収め、N=round(s) の 6 桁整数を
;   pf_ip へ、十進シフト数を pfe_cnt へ得る。FAC1 は破壊される。
pfe_scale:
                CLR     pfe_cnt         ; 十進シフトカウンタ (符号付き) = 0
pfe_sd:                                 ; while v ≥ 1E6 : v/=10, cnt++
                JSR     pf_f1tof2       ; FAC2 = v
                LDX     #pfc_1e6
                BSR     pf_cmp_v        ; A = sign(v - X の定数) / FAC1 = v (共通化)
                BMI     pfe_su          ; v < 1E6 → 上方スケールへ
                JSR     pf_f1tof2       ; FAC2 = v (被除数)
                LDX     #ften
                JSR     pf_ldc1         ; FAC1 = 10 (除数)
                JSR     fdiv_core       ; FAC1 = v/10
                INC     pfe_cnt
                BRA     pfe_sd
pfe_su:                                 ; while v < 1E5 : v*=10, cnt--
                JSR     pf_f1tof2       ; FAC2 = v
                LDX     #pfc_1e5
                JSR     pf_cmp_v        ; A = sign(v - X の定数) / FAC1 = v (共通化)
                BPL     pfe_scaled      ; v ≥ 1E5 → 完了
                JSR     pf_f1tof2       ; FAC2 = v
                LDX     #ften
                JSR     pf_ldc1         ; FAC1 = 10
                JSR     fmul_core       ; FAC1 = v*10
                DEC     pfe_cnt
                BRA     pfe_su
pfe_scaled:
                ; FAC1 = s ∈ [1E5,1E6)。dacc へ左シフトして整数部 N を得る (exp>127 確定)。
                JSR     pf_dacc_load    ; 読み込みと桁送り (指数 >127 確定なので左送り)
                ; pf_ip = int(s), pf_frac = 小数部。最近接へ丸める。
                LDA     pf_frac
                BPL     pfe_ovchk       ; 小数部 < 0.5 → 丸め無し
                INC     pf_ip+3
                BNE     pfe_ovchk
                INC     pf_ip+2
                BNE     pfe_ovchk
                INC     pf_ip+1
                BNE     pfe_ovchk
                INC     pf_ip
pfe_ovchk:
                ; 切上あふれ N==1000000 ($000F4240) なら N=100000 ($000186A0), cnt++
                LDD     pf_ip
                CMPD    #$000F
                BNE     pfe_scdone
                LDD     pf_ip+2
                CMPD    #$4240
                BNE     pfe_scdone
                LDD     #$0001
                STD     pf_ip
                LDD     #$86A0
                STD     pf_ip+2
                INC     pfe_cnt
pfe_scdone:
                RTS

pfe_emit:
                ; N (100000..999999) の6桁を pfe_dig[0..5] へ MSB 先頭で格納
                LDX     #pfe_dig+6
                LDB     #6
pfe_dlp:
                PSHS    B               ; div32by10 は B を壊す
                JSR     div32by10       ; A = N%10, N/=10
                PULS    B
                ADDA    #'0'
                STA     ,-X
                DECB
                BNE     pfe_dlp
                ; 仮数先頭桁
                LDA     pfe_dig
                JSR     impl_putchar    ; putchar は X/B 保存
                ; 小数桁 (pfe_dig[1..5]) 末尾ゼロ抑制
                LDB     #5
pfe_strip:
                LDX     #pfe_dig
                LDA     B,X
                CMPA    #'0'
                BNE     pfe_frac
                DECB
                BNE     pfe_strip
                BRA     pfe_exp         ; 小数部なし
pfe_frac:
                JSR     put_dot
                LDX     #pfe_dig+1
pfe_flp:
                JSR     put_x_inc
                DECB
                BNE     pfe_flp
;------------------------------------------------------------------------------
; pfe_exp — 単精度の指数形式で指数記号 'E' と指数値を出す。直前の小数部出力から
;   落ちてくるほか、小数部が無いときは分岐で入る。
; pf_put_e2 — 指数部の符号と 2 桁 (ゼロ詰め) を出力する (単精度・倍精度で共用)
;   指数形式は指数記号のあとに符号と
;   2 桁の指数を続ける。記号 (E / D) は呼出側が出す。
;   入力: A = 符号つき指数値 (-38..38)。破壊: A,B,CC。
;------------------------------------------------------------------------------
pfe_exp:
                LDA     #'E'
                JSR     impl_putchar
                LDA     pfe_cnt
                ADDA    #5              ; e10 = cnt + 5 (符号付き)
pf_put_e2:
                TSTA
                BMI     pfe_eneg
                PSHS    A
                LDA     #'+'
                JSR     impl_putchar
                PULS    A
                BRA     pfe_e2
pfe_eneg:
                NEGA
                PSHS    A
                LDA     #'-'
                JSR     impl_putchar
                PULS    A
pfe_e2:
                ; A = |e10| (0..38) → 2桁 (ゼロ詰め)
                LDB     #'0'
pfe_tens:
                CMPA    #10
                BLO     pfe_units
                SUBA    #10
                INCB
                BRA     pfe_tens
pfe_units:
                PSHS    A               ; 一の位の値を退避
                TFR     B,A
                JSR     impl_putchar    ; 十の位
                PULS    A
                ADDA    #'0'
                JMP     impl_putchar    ; 一の位


; E形式スケール用 float 定数 (6B: 指数 bias, 仮数32bit(先頭1明示), 符号)
pfc_1e5:        FCB     $90,$C3,$50,$00,$00,$00   ; 100000.0
pfc_1e6:        FCB     $93,$F4,$24,$00,$00,$00   ; 1000000.0
pfc_1e_1:       FCB     $7C,$CC,$CC,$CD,$00,$00   ; 0.1 (単精度)

; --- 倍精度リテラル分類 (本体は 7t_double5.inc, 予算都合で本余白へ) ---
; pnd_class: X の指すリテラルを先読みし、倍精度なら C=1。X 温存。破壊: A,B
pnd_class:
                PSHS    X
                CLRB                    ; B = 有効桁数
pc_l0:                                  ; 先行ゼロ/小数点を読み飛ばす
                LDA     ,X+
                CMPA    #'0'
                BEQ     pc_l0
                CMPA    #'.'
                BEQ     pc_l0
                BRA     pc_chk
pc_dl:
                LDA     ,X+
pc_chk:
                CMPA    #'0'
                BLO     pc_nd
                CMPA    #'9'
                BHI     pc_nd
                INCB
                BRA     pc_dl
pc_nd:
                CMPA    #'.'
                BEQ     pc_dl           ; 小数点は桁に数えず継続
                CMPA    #'D'
                BEQ     pc_dbl
                CMPA    #'#'
                BEQ     pc_dbl
                CMPA    #'E'
                BEQ     pc_sng
                CMPA    #'!'
                BEQ     pc_sng
                CMPA    #'%'
                BEQ     pc_sng
                CMPB    #8              ; 接尾辞なし: 桁数で判定
                BHS     pc_dbl
pc_sng:
                PULS    X
                ANDCC   #$FE
                RTS
pc_dbl:
                PULS    X
                ORCC    #$01
                RTS


;------------------------------------------------------------------------------
; 打鍵/報知効果 (ディスク側システム連携が要求する入口。アドレスは固定しない)
;   実装は受理のみ (RTS)。効果音は省略する (表示・進行への副作用は無い)。
;------------------------------------------------------------------------------
                include "7t_using_rep.inc"      ; PRINT USING 反復出力 (プール)
                include "7t_strasg.inc"         ; 文字列変数への格納 (区画の都合でここへ)

; pf_ldc1: FAC1[0..5] = X が指す 6B 定数, ガード0
;   (置き場所は容量の都合。ラベル参照のみで位置依存は無い)
pf_ldc1:
                LDD     ,X++
                STD     FAC1            ; 指数, 仮数最上位
                LDD     ,X++
                STD     FAC1+2
                LDD     ,X++
                STD     FAC1+4          ; 仮数最下位, 符号
                CLR     FAC1+6
                CLR     FAC1+7
                RTS

;------------------------------------------------------------------------------
; pn_decimal: 10 進数値リテラルを読み FAC1 へ。
;   参考書籍の数値定数仕様に整合:
;   ・仮数は 32bit (pn_mant) で累算。あふれる桁は整数部なら十進指数へ繰上げ、
;     小数部なら切り捨てる (有効桁は表示 6 桁に対して十分)。
;   ・16bit 符号付きに収まる純整数 (0..32767) は整数 (FACTYPE=0)。32767 超の整数・小数点/指数付きは単精度へ昇格する。
;     (単精度→16bit の FIX は下位 16bit 一致のため POKE 等へ渡るビットは同じ)
;   ・指数 E の符号は ASCII '+'/'-' とトークン化済み演算子 (TOK_PLUS/TOK_MINUS) の両方を
;     受理する (プログラム本文では符号が演算子トークンに畳まれるため)。
;   ・型接尾辞 %/!/# は読み飛ばす (内部表現は整数/単精度の共通 6 桁精度)。
;   入力: X = 数字/'.' の先頭。出力: FAC1 = 値, X = リテラル直後。
;   破壊: A,B,CC。U/Y 温存 (LET の VARPTR)。スクラッチ: pn_mant/pn_msat/
;   pn_isflt ($43-$48, float コア呼出し前のみ) と pn_dexp ($20)。
;------------------------------------------------------------------------------
pn_decimal:
                BSR     pnd_class       ; 倍精度リテラル (8桁以上/#/D指数) は
                LBCS    pn_double       ;   ワイド再解析へ。単精度/整数はこの下で
                CLR     pn_dexp
                CLR     pn_msat
                CLR     pn_isflt
                CLRA
                CLRB
                STD     pn_mant
                STD     pn_mant+2
pnd_int_lp:                             ; --- 整数部 ---
                BSR     nxt_digit       ; A=0-9 / C=1 で数字でない (A=その文字)
                BCS     pnd_int_done
                JSR     pnd_adddig
                BCC     pnd_int_lp
                INC     pn_dexp         ; 仮数あふれ: 桁を落とし十進指数で補償
                BRA     pnd_int_lp
pnd_int_done:
                CMPA    #'.'
                BNE     pnd_chk_exp
                LEAX    1,X
                INC     pn_isflt
pnd_frac_lp:                            ; --- 小数部 ---
                BSR     nxt_digit       ; A=0-9 / C=1 で数字でない (A=その文字)
                BCS     pnd_chk_exp
                JSR     pnd_adddig
                BCS     pnd_frac_lp     ; 仮数あふれ: 有効桁外 → 切り捨て
                DEC     pn_dexp
                BRA     pnd_frac_lp
;------------------------------------------------------------------------------
; nxt_digit — X の 1 文字が十進数字なら取り込む (数値定数の読取で共用)
;   出力: C=0 … A = 数値 (0-9)、X は 1 文字ぶん前進
;         C=1 … A = その文字 (数字でない)、X は不変
;   単精度・倍精度の整数部/小数部/指数部の 5 箇所で使う。破壊: A,CC (B/Y/U 温存)。
;------------------------------------------------------------------------------
nxt_digit:
                LDA     ,X
                CMPA    #'0'
                BLO     ndg_no
                CMPA    #'9'
                BHI     ndg_no
                LEAX    1,X
                SUBA    #'0'            ; 借りが立たないので C=0 (= 数字)
                RTS
ndg_no:
                ORCC    #$01            ; C=1 (= 数字でない)
                RTS

;------------------------------------------------------------------------------
; pn_exp_tail / pn_sfx_skip — 数値定数の指数部と型接尾辞の読取 (単精度・倍精度で共用)
;   数値定数は指数記号 (単精度 E / 倍精度 D) に
;   続けて符号と指数を書け、末尾に型宣言文字 (% ! #) を付けられる。読み取りは
;   どちらの精度でも同じなので 1 本にまとめてある。
;   pn_exp_tail: X = 指数記号の位置。符号と指数を読んで pn_dexp へ加算し、
;                そのまま型接尾辞の読み飛ばしへ続ける。
;   pn_sfx_skip: X = 型接尾辞が来うる位置。あれば 1 字消費する。
;   出力: X = 読み終えた次の位置。破壊: A,B,CC。
;------------------------------------------------------------------------------
pn_exp_tail:
                LEAX    1,X
                CLRB                    ; B = 指数符号 (0=正)
                LDA     ,X
                CMPA    #'+'
                BEQ     pnx_esk
                CMPA    #TOK_PLUS       ; トークン化済み '+'
                BEQ     pnx_esk
                CMPA    #'-'
                BEQ     pnx_emin
                CMPA    #TOK_MINUS      ; トークン化済み '-'
                BNE     pnx_edig
pnx_emin:
                INCB
pnx_esk:
                LEAX    1,X
pnx_edig:
                PSHS    B               ; 指数符号を退避
                CLRB                    ; B = 指数値
pnx_elp:
                BSR     nxt_digit       ; A=0-9 / C=1 で数字でない (A=その文字)
                BCS     pnx_edone
                PSHS    A
                LDA     #10
                MUL                     ; D = 10 * 指数値
                ADDB    ,S+             ; + 桁
                BRA     pnx_elp
pnx_edone:
                PULS    A               ; 指数符号
                TSTA
                BEQ     pnx_eadd
                NEGB                    ; 負指数
pnx_eadd:
                ADDB    pn_dexp
                STB     pn_dexp
pn_sfx_skip:                             ; --- 型接尾辞 %/!/# は消費 ---
                LDA     ,X
                CMPA    #'%'
                BEQ     pnx_sk
                CMPA    #'!'
                BEQ     pnx_sk
                CMPA    #'#'
                BNE     pnx_no
pnx_sk:
                LEAX    1,X
pnx_no:
                RTS
pnd_chk_exp:                            ; --- 指数部 E[±]nn ---
                LDA     ,X
                CMPA    #'E'
                BNE     pnd_suffix
                INC     pn_isflt
                BSR     pn_exp_tail     ; 指数部 + 型接尾辞 (倍精度側と共用)
                BRA     pnd_build
pnd_suffix:
                BSR     pn_sfx_skip     ; 型接尾辞のみ
pnd_build:                              ; --- 値の構築 ---
                TST     pn_isflt
                BNE     pnd_mkflt
                LDD     pn_mant
                BNE     pnd_mkflt       ; 上位 16bit 非零 → 65536 以上 → 単精度
                LDD     pn_mant+2
                BMI     pnd_mkflt       ; 32768..65535 → 単精度
                JMP     impl_int_to_fac ; 純整数 (整数型のビットのまま)
pnd_mkflt:
                ; FAC1 = float(M): 32bit 仮数を指数 159 (= M そのもの) で置き正規化
                LDD     pn_mant
                STD     FAC1+1
                LDD     pn_mant+2
                STD     FAC1+3
                LDA     pn_mant
                ORA     pn_mant+1
                ORA     pn_mant+2
                ORA     pn_mant+3
                LBEQ    flt_zero1       ; M=0 → 値ゼロ (FACTYPE=1)
                CLR     FAC1+5
                CLR     FAC1+6
                CLR     FAC1+7
                LDA     #159
                STA     FAC1
pnd_norm:
                LDA     FAC1+1
                BMI     pnd_normed      ; bit31 立ち → 正規化完了
                JSR     fac1_shl32
                DEC     FAC1
                BRA     pnd_norm
pnd_normed:
                LDA     #1
                STA     FACTYPE
pnd_scale:                              ; --- 十進スケール pn_dexp を適用 ---
                LDA     pn_dexp
                BEQ     pnd_done
                BMI     pnd_scale_neg
                ; pn_dexp>0: FAC1 *= 10
                LDD     #$83A0
                STD     FAC2
                CLRA
                CLRB
                STD     FAC2+2
                STD     FAC2+4
                JSR     fmul_core
                DEC     pn_dexp
                BRA     pnd_scale
pnd_scale_neg:
                ; pn_dexp<0: FAC1 /= 10  (fdiv_core: FAC2=被除数, FAC1=除数)
                JSR     pf_f1tof2       ; FAC2 = FAC1 (8B コピーの共通本体)
                LDD     #$83A0
                STD     FAC1
                CLRA
                CLRB
                STD     FAC1+2
                STD     FAC1+4
                JSR     fdiv_core
                INC     pn_dexp
                BRA     pnd_scale
pnd_done:
                RTS

; pnd_adddig: A = 桁 (0-9)。pn_mant = pn_mant*10 + A。
;   仮数が既飽和、またはこの桁であふれる場合は取り込まず carry セットで復帰。
;   受理時は carry クリア。破壊: A,B,CC。X/Y/U 温存。
pnd_adddig:
                TST     pn_msat
                BNE     pad_sat
                LDB     pn_mant         ; M > 429496728 ($19999998) なら飽和
                CMPB    #$19            ;   (M*10+9 が 32bit を超えない上限)
                BLO     pad_ok
                BHI     pad_setsat
                LDB     pn_mant+1
                CMPB    #$99
                BLO     pad_ok
                BHI     pad_setsat
                LDB     pn_mant+2
                CMPB    #$99
                BLO     pad_ok
                BHI     pad_setsat
                LDB     pn_mant+3
                CMPB    #$98
                BHI     pad_setsat
pad_ok:
                PSHS    A               ; 桁を退避
                LDD     pn_mant+2       ; M の複製をスタックへ (下位が 2,S)
                PSHS    D
                LDD     pn_mant
                PSHS    D               ; [S..3,S]=M複製, [4,S]=桁
                ASL     pn_mant+3       ; M *= 4
                ROL     pn_mant+2
                ROL     pn_mant+1
                ROL     pn_mant
                ASL     pn_mant+3
                ROL     pn_mant+2
                ROL     pn_mant+1
                ROL     pn_mant
                LDD     pn_mant+2       ; M += 複製 (→ M*5)
                ADDD    2,S
                STD     pn_mant+2
                LDD     pn_mant             ; 上位 16 ビット (桁上げを ADCB → ADCA へ連鎖)
                ADCB    1,S
                ADCA    ,S
                STD     pn_mant
                ASL     pn_mant+3       ; M *= 2 (→ M*10)
                ROL     pn_mant+2
                ROL     pn_mant+1
                ROL     pn_mant
                LEAS    4,S             ; M 複製を破棄
                PULS    A               ; 桁
                ADDA    pn_mant+3       ; M += 桁 (繰上げ伝播)
                STA     pn_mant+3
                BCC     pad_done
                INC     pn_mant+2
                BNE     pad_done
                INC     pn_mant+1
                BNE     pad_done
                INC     pn_mant
pad_done:
                ANDCC   #$FE            ; carry クリア = 受理
                RTS
pad_setsat:
                INC     pn_msat
pad_sat:
                ORCC    #$01            ; carry セット = 桁は取り込まず
                RTS

;------------------------------------------------------------------------------
; fdc_post: fdiv_core の仮数組み立て+指数/符号格納 (本体はプール。
;   丸め位置は単精度の有効桁 (24bit) の直下 (32bit で丸めて下位 8bit を格納時
;   に切り捨てると、商が 1ulp 小さく出る場合がある)。
;   入力: fdv_q = 商 32bit, fdv_rem = 剰余, fdv_m1 = 除数仮数,
;         スタック [0,S:1,S]=eres(16bit), [2,S]=符号バイト(bit0=int_q)。
;   fdiv_core から JMP で継続 (スタックは fdiv_core の積んだまま)。
;------------------------------------------------------------------------------
fdc_post:
                LDA     2,S             ; 符号バイト (bit0=int_q)
                BITA    #1
                BEQ     fdc_round
                LSR     fdv_q           ; int_q=1: Mres = (Q>>1) | $80000000
                ROR     fdv_q+1
                ROR     fdv_q+2
                ROR     fdv_q+3
                LDA     fdv_q
                ORA     #$80
                STA     fdv_q
fdc_round:
                ; 有効仮数 24bit の直下 fdv_q+3 をガードとして最近接丸め。
                ; タイもゼロから遠い側へ切り上げるため、更に下位の剰余は
                ; 判定に影響しない。
                LDA     fdv_q+3
                CLR     fdv_q+3         ; 24bit 未満の桁は残さない
                CMPA    #$80
                BLO     fdc_asm
                ; 仮数 += 1ulp。$FFFFFF → 0 へのあふれは Mres=$800000, eres++
                INC     fdv_q+2
                BNE     fdc_asm
                INC     fdv_q+1
                BNE     fdc_asm
                INC     fdv_q
                BNE     fdc_asm
                LDA     #$80            ; 全ビットあふれ → 2.0 → 1.0 で指数+1
                STA     fdv_q
                LDD     ,S
                ADDD    #1
                STD     ,S
fdc_asm:
                LDD     fdv_q
                STD     FAC1+1
                LDD     fdv_q+2
                STD     FAC1+3
                CLR     FAC1+6
                CLR     FAC1+7
                ; 指数格納
                PULS    D               ; D = eres (16bit)
                TSTA
                BMI     fdc_zero        ; eres 負 → アンダーフロー → 0
                CMPD    #254
                BHI     fdc_ovf         ; 格納形の指数上限を超える → Overflow
                STB     FAC1            ; 指数 = eres 下位バイト
                PULS    A               ; 符号バイト
                ANDA    #$80            ; 符号のみ (int_q bit0 を除去)
                STA     FAC1+5
                JMP     fac_type_sng    ; 型=単精度にして復帰 (共通化)
fdc_ovf:
                JMP     errc_6
fdc_zero:
                LEAS    1,S             ; 符号バイト破棄
                JMP     flt_zero1
;------------------------------------------------------------------------------
; EXP/LOG/ATN 用定数 (展開形 6B: 指数 bias128, 仮数32bit 先頭1明示, 符号)
;   本体は 7t_color_video.inc (impl_fn_exp/log/atn)。当区間の空き不足のため
;   定数表のみ本プールへ配置。
;------------------------------------------------------------------------------
c_log2e:        FCB     $80,$B8,$AA,$3B,$29,$00  ; log2(e) = 1.44269504
c_ln2:          FCB     $7F,$B1,$72,$17,$F8,$00  ; ln(2) = 0.693147181
; EXP の級数の係数表 (高次から低次へ。末尾 2 項の 1.0 が締めくくりの 2 段)
ctab_exp:
                FCB     $76,$B6,$0B,$60,$B6,$00  ; 1/720
                FCB     $79,$88,$88,$88,$89,$00  ; 1/120
                FCB     $7B,$AA,$AA,$AA,$AB,$00  ; 1/24
                FCB     $7D,$AA,$AA,$AA,$AB,$00  ; 1/6
c_half:         FCB     $7F,$80,$00,$00,$00,$00  ; 0.5 (級数の 1 項 兼 汎用)
                FCB     $80,$80,$00,$00,$00,$00  ; 1.0
                FCB     $80,$80,$00,$00,$00,$00  ; 1.0
; LOG の級数の係数表
ctab_log:
                FCB     $7C,$E3,$8E,$38,$E4,$00  ; 1/9
                FCB     $7D,$92,$49,$24,$92,$00  ; 1/7
                FCB     $7D,$CC,$CC,$CC,$CD,$00  ; 1/5
                FCB     $7E,$AA,$AA,$AA,$AB,$00  ; 1/3
                FCB     $80,$80,$00,$00,$00,$00  ; 1.0
; ATN の級数の係数表
ctab_atn:
                FCB     $7C,$9D,$89,$D8,$9E,$00  ; 1/13
                FCB     $7C,$BA,$2E,$8B,$A3,$80  ; -1/11
                FCB     $7C,$E3,$8E,$38,$E4,$00  ; 1/9
                FCB     $7D,$92,$49,$24,$92,$80  ; -1/7
                FCB     $7D,$CC,$CC,$CC,$CD,$00  ; 1/5
                FCB     $7E,$AA,$AA,$AA,$AB,$80  ; -1/3
                FCB     $80,$80,$00,$00,$00,$00  ; 1.0
c_mone:         FCB     $80,$80,$00,$00,$00,$80  ; -1.0
c_tan8:         FCB     $7E,$D4,$13,$CC,$D0,$00  ; tan(pi/8) = 0.414213562
c_qpi:          FCB     $7F,$C9,$0F,$DA,$A2,$00  ; pi/4 = 0.785398163

;------------------------------------------------------------------------------
; ipc_capture — impl_putchar の捕捉モード本体 (STR$ 単精度)
;   pc_capt 非0 の間、出力バイトを pc_capptr の指す RAM へ追記する。
;   putchar の契約どおり A/B/X/U を保存する。
;------------------------------------------------------------------------------
ipc_capture:
                PSHS    X
                LDX     pc_capptr
                STA     ,X+
                STX     pc_capptr
                PULS    X,PC

;------------------------------------------------------------------------------
; pool_str_float — STR$(単精度) 本体
;   PRINT と同一整形 (print_float) を putchar 捕捉で strf_buf へ写し取り、
;   末尾の区切り空白 1 個を除いた部分を文字列値にする (先頭の符号位置の空白は含む。
;   動作観察)。
;------------------------------------------------------------------------------
pool_str_float:
                LDA     #1
                STA     pc_capt
                BSR     psf_base        ; X = 型別捕捉バッファ先頭
                STX     pc_capptr
                BSR     psf_disp        ; 単精度/倍精度の整形出力を捕捉
                CLR     pc_capt
                ; 注: STRPTR ($005A) は FAC2 とアドレス重複のため、print が FAC2 を
                ;     使い終えた後に設定する (FACTYPE は print で不変)
                BSR     psf_base
                STX     STRPTR
                LDD     pc_capptr
                SUBD    STRPTR          ; B = 捕捉長
                DECB                    ; 末尾の区切り空白を除く
                STB     STRLEN
                JMP     fret_str_own    ; FACTYPE=3 を立てて復帰 (共通尾部)
psf_base:
                LDX     #strf_buf
                LDA     FACTYPE
                CMPA    #2
                BNE     psb_r
                LDX     #strf_dbuf      ; 倍精度は最長 24B → 専用バッファ
psb_r:
                RTS
psf_disp:
                LDA     FACTYPE
                CMPA    #2
                BNE     psd_s
                BRA     print_double
psd_s:
                JMP     print_float

;------------------------------------------------------------------------------
; INT(x) / FIX(x) の本体は 7t_numchk.inc に置く (置き場所は容量の都合)。
;   参照はラベル経由のみ。
;------------------------------------------------------------------------------

                include "7t_double4.inc"        ; 倍精度表示 (プール)

;------------------------------------------------------------------------------
; LOAD ",R" 支援ルーチン群 (impl_load から参照)。
;   impl_load が属する 当区間は空きが無いため、追加ロジックを本プール
;   (空きプール) へ置き、impl_load 側は同サイズの呼び出しに留める。
;
; impl_load_fname_r — ファイル名解析 + ",R" (ロード後自動実行) 解析。
;   まず impl_parse_fname でファイル名/装置を確定し、続く ",R" を解析して
;   mlm_run (0=無/1=有) に記録する。X は引数末尾へ進めて返す。
;   参考書籍の LOAD 構文: LOAD "name"[,R] のカンマ 1 個直後が
;   'R'/'r' のときのみロード後自動実行を指定する。
;------------------------------------------------------------------------------
impl_load_fname_r:
                JSR     impl_parse_fname        ; ファイル名 → cmt_filename/dl_device, X 前進
                CLR     mlm_run                 ; ,R フラグ既定 OFF
                JSR     skip_spc                ; ファイル名と ',' の間の空白を除去
                JSR     chk_comma_x     ; A=,X を読み ',' と比較 (共通化)
                BNE     ilfr_done
                JSR     skip1_sp_x
                LDA     ,X
                CMPA    #'R'
                BEQ     ilfr_set
                CMPA    #'r'
                BNE     ilfr_done
ilfr_set:
                LEAX    1,X                     ; 'R' を読み飛ばす
                LDA     #1
                STA     mlm_run                 ; ,R = ロード後自動実行
ilfr_done:
                RTS

;------------------------------------------------------------------------------
; il_ret_run — LOAD ",R" (ロード後自動実行) の共通尾部。
;   ロードが正常終了したテープ/ディスク両経路の末尾から入る。
;   mlm_run=1 (",R" 指定あり) なら、プログラムを先頭から自動実行する。
;   実行は impl_exec_prog (変数/配列テーブル・FOR/GOSUB スタック・DATA ポインタを
;   初期化してから先頭行を実行) を用いる。LOAD ",R" は
;   ロード直後に RUN 相当を行い、変数を初期化する。
;   mlm_run=0 のときは C=0 で復帰する (ロードのみ)。
;------------------------------------------------------------------------------
il_ret_run:
                ; 読込の完了時に変数領域の管理位置を本文末尾へ揃える。
                ;   LOAD は読込完了の時点で全変数を消去する (参考書籍)。
                ;   スカラ領域の起点 VARTAB は各経路が本文末尾に
                ;   設定済なので、ここで配列領域の起点 ARYTAB と末尾 STREND を
                ;   VARTAB へ揃える。揃えないと読込前の古い位置が残り、変数の
                ;   記録が本文へ重なる。
                JSR     vars_clear_all
                ; ロードで本文が入れ替わったので CONT の再開位置を破棄する。
                ; 残しておくと、入れ替わる前の本文の途中を指したポインタで CONT が走り出す。
                JSR     cont_clear
                LDA     mlm_run
                BEQ     ilrr_norun
                LDD     #$FFFF
                STD     RUNLIN                  ; 先頭行から実行 (RUN 行番号指定なし)
                JMP     impl_exec_prog          ; 変数クリア + 先頭から実行 (RUN 相当。戻らない)
ilrr_norun:
                ANDCC   #$FE                    ; C=0 正常 (ロードのみ)
                RTS




;------------------------------------------------------------------------------
; savem_finish — SAVEM 本体の末尾 (impl_savem から JMP で入る)。
;   impl_savem が属する区間には空きが無いため、機械語イメージ
;   出力の末尾 (終端レコード + EOF ブロック + モータ OFF + 復帰) を本プールへ置く。
;   前提: スタックに復帰用 X が積まれている / svm_exec = 実行開始アドレス。
;   終端レコード ($FF 00 00 execHi execLo) と EOF ブロック (type=2) を書き、
;   モータ OFF・PULS X して RTS する。cmt_hdr_buf を 5B スクラッチに流用。
;------------------------------------------------------------------------------
savem_finish:
                ; --- 終端レコード: $FF 00 00 execHi execLo (type=1, 5B) ---
                LDU     #cmt_hdr_buf
                LDA     #$FF
                STA     ,U+                     ; $FF 終端マーカ
                CLR     ,U+                     ; 長さフィールド Hi (=0)
                CLR     ,U+                     ; 長さフィールド Lo (=0)
                LDD     svm_exec
                STA     ,U+                     ; execHi
                STB     ,U+                     ; execLo
                LDD     #$0105
                STD     cmt_blk_type    ; 種別 1 / 長さ 5 (隣り合うので 16 ビットで置く)
                LDD     #cmt_hdr_buf
                STD     sv_ptr
                JSR     cmt_write_block
                ; --- エンドブロック (種別 $FF。参考書籍) ---
                LDA     #$FF
                STA     cmt_blk_type
                CLR     cmt_blk_len
                JSR     cmt_write_block
                JSR     cmt_motor_off           ; モーター OFF
                PULS    X,PC

                ; 単精度: 4B 内部形式 → FAC1 展開形 / 倍精度: 8B 直読み
;------------------------------------------------------------------------------
; 倍精度変数の値スロット入出力。
;   変数表・配列の値サイズは型別表 var_size_tbl が倍精度に 8B を割り当てる。
;   倍精度の値スロットは単精度経路 (unpack_sng / pack_sng) を通さず、FAC1 の
;   倍精度パック 8B と直に往復させて、格納・複製ともに精度を落とさない。
;   本体はラベル参照のみで位置依存が無いため当プールへ置き、呼出元
;   (impl_get_var_val / impl_assign_var) からは JMP 1 個で入る。
;------------------------------------------------------------------------------
; pool_var_load: U 指す値スロット → FAC1。VARTYP が倍精度なら 8B 直読み。
;   破壊: A,B,CC,U。X (本文ポインタ) 温存。
pool_var_load:
                LDA     VARTYP
                CMPA    #VTYPE_DBL
                BEQ     pvl_dbl
                JMP     unpack_sng

pvl_dbl:
                PSHS    X               ; 本文ポインタは呼出元が使う (mcopy8 が壊す)
                LDX     #FAC1
                JSR     mcopy8          ; FAC1(8B) ← 値スロット(8B)
                PULS    X
                JMP     dw_t2           ; FACTYPE=2 を立てて復帰 (共通尾部)

                ; 単精度: FAC1 → 4B 内部形式 / 倍精度: 8B 直書き
; pool_var_store: FAC1 → U 指す値スロット。VARTYP が倍精度なら 8B 直書き。
;   破壊: A,B,CC。X/U 温存。
pool_var_store:
                LDA     VARTYP
                CMPA    #VTYPE_DBL
                BEQ     pvs_dbl
                JMP     pack_sng

pvs_dbl:
                PSHS    X,U             ; 本文ポインタ / 格納先 (mcopy8 が壊す)
                JSR     dbl_widen
                LDX     2,S             ; X = 格納先
                LDU     #FAC1
                JSR     mcopy8          ; 値スロット(8B) ← FAC1(8B)
                PULS    X,U,PC

; tok_match_fn2 — 二次関数予約語 (kw_2nd_fn_tbl) のトークン化。走査本体は
;   7t_tokenizer.inc の tmk_outer を共用し、トークン値は TOK_2ND_FN ($B4) 起点。
;   表のアドレスが関数語表より後ろにあるため、走査本体末尾の判定で $FF 前置つきの
;   2 バイトトークンとして出力される。当区間に余白が無いため本プールへ置いた。
;   出力: 成功 → Z=1, X=語末次, U に $FF tok を書込/前進 / 失敗 → Z=0, X/U 不変。
tok_match_fn2:
                PSHS    Y,X
                TFR     X,Y
                LDX     #kw_2nd_fn_tbl_end
                PSHS    X
                LDX     #kw_2nd_fn_tbl
                LDB     #TOK_2ND_FN
                LBRA    tmk_outer_first ; 入力 1 文字目の畳みを設定してから走査へ

;------------------------------------------------------------------------------
; disk_crlf — コンソール改行 (CR+LF)。行送りの共通ルーチン。
;   一覧表示・数値表示・PRINT の行末など、行を送る箇所から共用で呼ぶ。
;------------------------------------------------------------------------------
disk_crlf:
                LDA     #$0D
                JSR     impl_putchar
                LDA     #$0A
                JMP     impl_putchar

; pool_fn_dispatch — 関数トークン ($FF xx) の呼び分け。7t_expr.inc の
;   epr_nofeconst から JMP で入る (当該区間に余白が無いため本プールへ置いた。
;   参照はラベル経由のみで位置依存なし)。
;   入力: X = $FF を指す位置 / 出力: FAC1 = 関数値, X = 引数の直後。
;   関数語表の語は $FF $80- の連番で関数ジャンプ表 fn_jt を引く。二次関数語
;   ($FF $B4-) は別系列で fn_jt の外にあるため、手前で振り分ける。
pool_fn_dispatch:
                LEAX    1,X
                LDA     ,X+
                CMPA    #TOK_2ND_FN     ; 二次関数トークンか
                BHS     pfd_fn2
                SUBA    #TOK2_FIRST
                TFR     A,B
                ASLB
                ; ハンドラアドレスを Y に求める (X は引数走査に使うため温存)。
                ; 呼出後の X を関数トークン直後 (= 引数の '(' の手前) へ戻すと、
                ; PRINT 等が引数を二度評価してしまう。ハンドラ復帰後の X
                ; (引数の直後) をそのまま返す。
                PSHS    X               ; 入力ポインタ退避
                LDX     #fn_jt
                LDX     B,X             ; X = ハンドラアドレス
                TFR     X,Y
                PULS    X               ; X = 入力ポインタ (引数先頭)
                JSR     ,Y
pfd_ret:
                ; ハンドラは STRPTR 等のため X(本文ポインタ)を壊しうる。
                ; impl_eval_expr_p が保存した引数直後の位置を復元する。
                LDX     fn_xsave
                RTS
; 二次関数語は LPOS 1 語だけである。参考書籍の LPOS(n) は印字装置 n の
;   現在の桁位置を返す。本実装は印字出力を行単位でそのまま送り出す作りで桁位置を
;   保持しないため、書式どおり引数を受け取ったうえで常に行頭 0 を返す。
pfd_fn2:
                JSR     skip_sp_lparen
                BNE     pfd_bare
                JSR     eval_p_num      ; 引数を評価して捨てる
                BRA     pfd_zero
pfd_bare:
                STX     fn_xsave        ; 引数を取らない形は本文位置がここ
pfd_zero:
                JSR     int_to_fac_zero         ; FAC1 = 0
                BRA     pfd_ret


;------------------------------------------------------------------------------
; 以降の CMT 書込/ロード系ルーチン群の置き場所は容量の都合で決めた空きプールで、
; 参照はラベル経由のみ。アドレスは固定していない。
;------------------------------------------------------------------------------
;------------------------------------------------------------------------------

