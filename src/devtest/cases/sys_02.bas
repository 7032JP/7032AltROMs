0 REM AltROMs test - MIT, own work.
1 REM VARPTR は変数・配列要素の格納アドレスを返す。値は実行環境で
2 REM 変わるので、要素間の間隔 (整数 2 / 単精度 4 / 倍精度 8 バイト) と、
3 REM 整数の格納 (上位・下位の並び) という性質を照合する (真 = -1)。
10 DIM V%(2):PRINT VARPTR(V%(1))-VARPTR(V%(0));VARPTR(V%(2))-VARPTR(V%(0))
20 DIM W(2):PRINT VARPTR(W(1))-VARPTR(W(0))
30 DIM D#(2):PRINT VARPTR(D#(1))-VARPTR(D#(0))
40 Q%=12345:PRINT (VARPTR(Q%)>0) AND (PEEK(VARPTR(Q%))*256+PEEK(VARPTR(Q%)+1)=12345)
