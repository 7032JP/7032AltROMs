0 REM AltROMs test (MIT)
1 REM DEF FN: 同じ名前の関数を 2 度定義すると Duplicate Definition (誤り 10)。
10 DEF FNA(X)=X*2:PRINT FNA(3)
20 DEF FNA(X)=X+1:PRINT FNA(3)
30 PRINT "END"
