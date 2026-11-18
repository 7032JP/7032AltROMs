0 REM AltROMs test (MIT)
1 REM DEF FN: 文字列の関数に文字列定数・式・変数を渡す。
2 REM 定義と別の行から呼んでも引数と値は崩れない。
10 DEF FNB$(S$)=LEFT$(S$,2)+"<"
20 PRINT FNB$("ABCDEF")
30 PRINT FNB$("AB"+"CD")
40 A$="QWERTY":PRINT FNB$(A$)
50 DEF FNC$(S$)=S$+"!"
60 PRINT FNC$("HELLO")
70 DEF FNK$(X)="ABC"
80 PRINT FNK$(1);FNK$(2)+"Z"
