; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 文字列領域の詰め直し。割付側は 7t_strheap.inc にある。
;   本片は空きプールに置く。参照は全てラベル経由で位置依存なし。
;==============================================================================
; (str_own は容量の都合で 7t_strown.inc (空きプールの空きプール) へ移設した。
;  参照はラベル経由のみで位置依存はない)
;------------------------------------------------------------------------------
; str_gc — 恒久ブロックの詰め直し。
;   記述子 (スカラの文字列変数と文字列配列の全要素) を総なめし、
;   「gc_dst 以下でいちばん上に在る生きたブロック」を gc_dst へ寄せる。
;   これを見つからなくなるまで繰り返し、最後に FRETOP を gc_dst にする。
;   死んだブロック (どの記述子からも指されていない実体) は寄せられずに
;   上書きされて消える。
;   ブロックは動くので、恒久ブロックを指したままの STRPTR は無効になる。
;     呼出側 (av_str) が複写元を先に一時領域へ退避しておく約束である。
;   破壊: D。X/Y/U と CC 以外の呼出側の文脈は保つ。
;------------------------------------------------------------------------------
str_gc:
                PSHS    U,Y,X
                CLR     str_gcflg       ; ここで回収するので印を倒す
                LDD     MEMTOP
                STD     gc_dst          ; 詰め先は領域の最上位から
gc_pass:
                LDD     STRBOT
                STD     gc_bptr         ; 候補なし (下限そのものを初期値にする)
                ; --- スカラ変数 (VARTAB..ARYTAB) ---
                LDY     VARTAB
gc_sc:
                CMPY    ARYTAB
                BHS     gc_ar_init
                TFR     Y,U
                JSR     gvr_valptr      ; U = 値先頭 (= 文字列なら記述子)
                LDA     ,Y              ; ヘッダ [型|名前長]
                ANDA    #$C0            ; 型ビットだけ残す (vn_valsize も同じ所しか見ない)
                CMPA    #VTYPE_STR
                BNE     gc_sc_nx
                JSR     gc_try          ; gc_try は A を使わないので退避が要らない
gc_sc_nx:
                JSR     vn_valsize      ; B = 値サイズ (A,X 破壊)
                CLRA
                LEAY    D,U             ; 次エントリ = 値先頭 + 値サイズ
                BRA     gc_sc
                ; --- 配列 (ARYTAB..STREND) ---
gc_ar_init:
                LDY     ARYTAB
gc_ar:
                CMPY    STREND
                BHS     gc_have
                TFR     Y,X
                JSR     arr_entry_size  ; D = エントリ総バイト数, VARTYP, arr_ndim
                LEAU    D,X             ; U = 次エントリ先頭
                STU     gc_ent
                LDA     VARTYP
                CMPA    #VTYPE_STR
                BNE     gc_ar_nx
                JSR     arr_hdrskip     ; X = len 欄先頭
                LDB     2,X             ; ndim (len 欄 +2)
                LSLB
                ADDB    #3              ; 要素先頭 = len 欄 + 3 + 2*次元数
                CLRA
                LEAU    D,X
gc_el:
                CMPU    gc_ent
                BHS     gc_ar_nx
                JSR     gc_try
                LEAU    3,U             ; 文字列要素は 3 byte
                BRA     gc_el
gc_ar_nx:
                LDY     gc_ent
                BRA     gc_ar
                ; --- 選ばれたブロックを詰める ---
gc_have:
                LDX     gc_bptr
                CMPX    STRBOT
                BLS     gc_fin          ; もう無い
                LDB     gc_blen
                CLRA
                LEAU    D,X             ; U = 実体の直後
                LDX     gc_dst
                LEAX    -1,X            ; 詰め先の末尾 + 1 (上位 2 byte は空ける)
gc_mv:
                LDA     ,-U
                STA     ,-X
                DECB
                BNE     gc_mv
                LDU     gc_bdsc
                STX     1,U             ; 記述子を新しい実体先頭へ張り直す
                LEAX    -1,X
                STX     gc_dst          ; 次の詰め先
                BRA    gc_pass
gc_fin:
                LDD     gc_dst
                STD     FRETOP
                PULS    X,Y,U,PC

; (gc_try は容量の都合で別の区画の空きプール (7t_strgc2.inc) へ移した。
;  参照はラベル経由のみで位置依存はない)
