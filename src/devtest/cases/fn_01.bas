0 REM AltROMs test - MIT, own work.
1 REM DEF FN: 定義・入れ子・仮引数と同名の変数の保護・再定義・式内複数使用・
2 REM 文字列関数の定義。
10 DEF FNA(X)=X*X+1:PRINT FNA(3)
20 DEF FNB(X)=FNA(X)+X:PRINT FNB(3)
30 X=99:PRINT FNA(2);X
40 DEF FNA(X)=X+100:PRINT FNA(3)
50 PRINT FNA(1)+FNA(2)*FNA(0)
60 DEF FNS$(A$)=A$+"!":PRINT FNS$("HI")
