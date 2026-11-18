0 REM AltROMs test (MIT)
1 REM VAL: &・&O・&H だけの文字列は 0。文字列の終わりの先は読まない。
10 PRINT VAL("&");VAL("&O");VAL("&H")
20 PRINT VAL(LEFT$("&17",1));VAL(LEFT$("&H1F",2))
30 PRINT VAL("&O17");VAL("&H1F");VAL("&O7")
40 A$="&":PRINT VAL(A$+"O12");VAL(A$)
