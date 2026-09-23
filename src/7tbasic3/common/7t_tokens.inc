; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Naomitsu.Tsugiiwa
;==============================================================================
; 7T-BASIC 3.1 — 予約語トークン値のシンボル定義
;
;   予約語表 (7t_startup.inc の kw_tbl / kw_fn_tbl、7t_runtime.inc の
;   kw_2nd_tbl / kw_2nd_fn_tbl) の各欄に 1 対 1 で対応する定数を、当ファイルへ
;   一元化する。実装側は生の 16 進即値を書かず、必ずここの名前を参照する。
;   トークン値は予約語表の位置から機械的に決まる内部値であり、表の並びは
;   本プロジェクトが独自に定めた規則 (先頭 1 文字でまとめ、同一先頭字の中は
;   綴り長の降順 → 同長は綴り順) による。
;   値の並びは表の並びと同順。$ で終わる関数名は名前から $ を落とす。
;
;   本ファイルと予約語表・ジャンプ表は scripts/gen_7t_keywords.py が同期生成
;   する。手書きで編集しない (表とジャンプ表の対応がずれると暴走するため)。
;==============================================================================

; --- 域の境界と予約値 ----------------------------------------------------
TOK_FIRST       EQU     $80     ; 命令語トークンの起点 (= kw_tbl 0 番, これ未満は生バイト)
TOK2_FIRST      EQU     $80     ; 関数語トークン ($FF xx) 第2バイトの起点 (= kw_fn_tbl 0 番)
TOK_2ND_FIRST   EQU     $EE     ; 二次命令語トークンの起点 (= kw_2nd_tbl 0 番)
TOK_2ND_LAST    EQU     $F3     ; 二次命令語トークンの終端 (= kw_2nd_tbl 最終欄)
TOK_NUM_PFX     EQU     $FE     ; 数値定数・行番号参照の前置バイト (予約値)
TOK_FUNC_PFX    EQU     $FF     ; 関数語トークンの前置バイト (予約値)

; --- 命令語トークン (kw_tbl / cmd_jt と同順、105 語) ---------------------
TOK_APOS        EQU     $80     ; '
TOK_MUL         EQU     $81     ; *
TOK_PLUS        EQU     $82     ; +
TOK_MINUS       EQU     $83     ; -
TOK_DIV         EQU     $84     ; /
TOK_LT          EQU     $85     ; <
TOK_EQ          EQU     $86     ; =
TOK_GT          EQU     $87     ; >
TOK_QMARK       EQU     $88     ; ?
TOK_AUTO        EQU     $89     ; AUTO
TOK_AND         EQU     $8A     ; AND
TOK_BUBINI      EQU     $8B     ; BUBINI
TOK_BEEP        EQU     $8C     ; BEEP
TOK_BUBR        EQU     $8D     ; BUBR
TOK_BUBW        EQU     $8E     ; BUBW
TOK_CONNECT     EQU     $8F     ; CONNECT
TOK_CONSOLE     EQU     $90     ; CONSOLE
TOK_CIRCLE      EQU     $91     ; CIRCLE
TOK_CLEAR       EQU     $92     ; CLEAR
TOK_CLOSE       EQU     $93     ; CLOSE
TOK_COLOR       EQU     $94     ; COLOR
TOK_CONT        EQU     $95     ; CONT
TOK_CLS         EQU     $96     ; CLS
TOK_COM         EQU     $97     ; COM
TOK_DEFDBL      EQU     $98     ; DEFDBL
TOK_DEFINT      EQU     $99     ; DEFINT
TOK_DEFSNG      EQU     $9A     ; DEFSNG
TOK_DEFSTR      EQU     $9B     ; DEFSTR
TOK_DELETE      EQU     $9C     ; DELETE
TOK_DATA        EQU     $9D     ; DATA
TOK_DEF         EQU     $9E     ; DEF
TOK_DIM         EQU     $9F     ; DIM
TOK_ERROR       EQU     $A0     ; ERROR
TOK_EDIT        EQU     $A1     ; EDIT
TOK_ELSE        EQU     $A2     ; ELSE
TOK_EXEC        EQU     $A3     ; EXEC
TOK_END         EQU     $A4     ; END
TOK_EQV         EQU     $A5     ; EQV
TOK_ERL         EQU     $A6     ; ERL
TOK_ERR         EQU     $A7     ; ERR
TOK_FILES       EQU     $A8     ; FILES
TOK_FOR         EQU     $A9     ; FOR
TOK_FN          EQU     $AA     ; FN
TOK_GCURSOR     EQU     $AB     ; GCURSOR
TOK_GET         EQU     $AC     ; GET
TOK_GO          EQU     $AD     ; GO
TOK_HARDC       EQU     $AE     ; HARDC
TOK_INTERVAL    EQU     $AF     ; INTERVAL
TOK_IMP         EQU     $B0     ; IMP
TOK_IF          EQU     $B1     ; IF
TOK_KILL        EQU     $B2     ; KILL
TOK_KEY         EQU     $B3     ; KEY
TOK_LOCATE      EQU     $B4     ; LOCATE
TOK_LINE        EQU     $B5     ; LINE
TOK_LIST        EQU     $B6     ; LIST
TOK_LOAD        EQU     $B7     ; LOAD
TOK_LET         EQU     $B8     ; LET
TOK_MERGE       EQU     $B9     ; MERGE
TOK_MOTOR       EQU     $BA     ; MOTOR
TOK_MOD         EQU     $BB     ; MOD
TOK_MON         EQU     $BC     ; MON
TOK_NEXT        EQU     $BD     ; NEXT
TOK_NEW         EQU     $BE     ; NEW
TOK_NOT         EQU     $BF     ; NOT
TOK_OPEN        EQU     $C0     ; OPEN
TOK_OFF         EQU     $C1     ; OFF
TOK_ON          EQU     $C2     ; ON
TOK_OR          EQU     $C3     ; OR
TOK_PRESET      EQU     $C4     ; PRESET
TOK_PAINT       EQU     $C5     ; PAINT
TOK_PRINT       EQU     $C6     ; PRINT
TOK_POKE        EQU     $C7     ; POKE
TOK_PSET        EQU     $C8     ; PSET
TOK_PUT         EQU     $C9     ; PUT
TOK_RANDOMIZE   EQU     $CA     ; RANDOMIZE
TOK_RESTORE     EQU     $CB     ; RESTORE
TOK_RESUME      EQU     $CC     ; RESUME
TOK_RETURN      EQU     $CD     ; RETURN
TOK_RENUM       EQU     $CE     ; RENUM
TOK_READ        EQU     $CF     ; READ
TOK_REM         EQU     $D0     ; REM
TOK_RUN         EQU     $D1     ; RUN
TOK_SYMBOL      EQU     $D2     ; SYMBOL
TOK_SKIPF       EQU     $D3     ; SKIPF
TOK_SAVE        EQU     $D4     ; SAVE
TOK_SPC         EQU     $D5     ; SPC(
TOK_STEP        EQU     $D6     ; STEP
TOK_STOP        EQU     $D7     ; STOP
TOK_SWAP        EQU     $D8     ; SWAP
TOK_SUB         EQU     $D9     ; SUB
TOK_TROFF       EQU     $DA     ; TROFF
TOK_TAB         EQU     $DB     ; TAB(
TOK_TERM        EQU     $DC     ; TERM
TOK_THEN        EQU     $DD     ; THEN
TOK_TRON        EQU     $DE     ; TRON
TOK_TO          EQU     $DF     ; TO
TOK_UNLIST      EQU     $E0     ; UNLIST
TOK_USING       EQU     $E1     ; USING
TOK_USR         EQU     $E2     ; USR
TOK_WHILE       EQU     $E3     ; WHILE
TOK_WIDTH       EQU     $E4     ; WIDTH
TOK_WEND        EQU     $E5     ; WEND
TOK_XOR         EQU     $E6     ; XOR
TOK_IDIV        EQU     $E7     ; \
TOK_POW         EQU     $E8     ; ^

; --- 関数語トークン第2バイト (kw_fn_tbl / fn_jt と同順、43 語) -----------
TOK2_ANPORT     EQU     $80     ; ANPORT
TOK2_ABS        EQU     $81     ; ABS
TOK2_ASC        EQU     $82     ; ASC
TOK2_ATN        EQU     $83     ; ATN
TOK2_CSRLIN     EQU     $84     ; CSRLIN
TOK2_CDBL       EQU     $85     ; CDBL
TOK2_CHR        EQU     $86     ; CHR$
TOK2_CINT       EQU     $87     ; CINT
TOK2_CSNG       EQU     $88     ; CSNG
TOK2_COS        EQU     $89     ; COS
TOK2_DATE       EQU     $8A     ; DATE
TOK2_EOF        EQU     $8B     ; EOF
TOK2_EXP        EQU     $8C     ; EXP
TOK2_FIX        EQU     $8D     ; FIX
TOK2_FRE        EQU     $8E     ; FRE
TOK2_HEX        EQU     $8F     ; HEX$
TOK2_INKEY      EQU     $90     ; INKEY$
TOK2_INPUT      EQU     $91     ; INPUT
TOK2_INSTR      EQU     $92     ; INSTR
TOK2_INT        EQU     $93     ; INT
TOK2_LEFT       EQU     $94     ; LEFT$
TOK2_LEN        EQU     $95     ; LEN
TOK2_LOF        EQU     $96     ; LOF
TOK2_LOG        EQU     $97     ; LOG
TOK2_MID        EQU     $98     ; MID$
TOK2_OCT        EQU     $99     ; OCT$
TOK2_POINT      EQU     $9A     ; POINT
TOK2_PEEK       EQU     $9B     ; PEEK
TOK2_PEN        EQU     $9C     ; PEN
TOK2_POS        EQU     $9D     ; POS
TOK2_RIGHT      EQU     $9E     ; RIGHT$
TOK2_RND        EQU     $9F     ; RND
TOK2_STRING     EQU     $A0     ; STRING$
TOK2_SCREEN     EQU     $A1     ; SCREEN
TOK2_SPACE      EQU     $A2     ; SPACE$
TOK2_STR        EQU     $A3     ; STR$
TOK2_SGN        EQU     $A4     ; SGN
TOK2_SIN        EQU     $A5     ; SIN
TOK2_SQR        EQU     $A6     ; SQR
TOK2_TIME       EQU     $A7     ; TIME
TOK2_TAN        EQU     $A8     ; TAN
TOK2_VARPTR     EQU     $A9     ; VARPTR
TOK2_VAL        EQU     $AA     ; VAL

; --- 二次予約語トークン (kw_2nd_tbl / kw_2nd_fn_tbl と同順) --------------
TOK_2ND_CHAIN   EQU     $EE     ; CHAIN
TOK_2ND_ERASE   EQU     $EF     ; ERASE
TOK_2ND_LPRINT  EQU     $F0     ; LPRINT
TOK_2ND_LLIST   EQU     $F1     ; LLIST
TOK_2ND_PLAY    EQU     $F2     ; PLAY
TOK_2ND_SOUND   EQU     $F3     ; SOUND
TOK_2ND_FN      EQU     $B4     ; 二次関数語トークン ($FF xx) 第2バイトの起点 (LPOS)

; --- 別名 (値は上の定義と同一) -------------------------------------------
TOK_REM_APOS    EQU     TOK_APOS        ; 注記の短縮形 ' のトークン (REM と同じ扱い)
TOK_UPLUS       EQU     TOK_PLUS        ; '+' 演算子トークン ('-' は +1。生成器が隣接を検査)
