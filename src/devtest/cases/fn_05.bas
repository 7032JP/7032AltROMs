0 REM AltROMs test (MIT)
1 REM DEF FN の実引数は、パラメータへ代入する前に全部評価される: 実引数の式が
2 REM パラメータと同じ名前の変数を読むと、呼出側の値が見える。呼出しの後、
3 REM パラメータと同じ名前の変数は元の値のまま。
10 DEF FNA(X,Y)=X*10+Y
20 X=5:Y=7
30 PRINT FNA(1,X);FNA(Y,X);X;Y
40 DEF FNS$(A$,B$)=A$+B$
50 A$="P":B$="Q"
60 PRINT FNS$("Z",A$);FNS$(B$+"1",A$+"2");A$;B$
70 DEF FNM(A%,B,C#)=A%+B+C#
80 A%=1:B=2:C#=3
90 PRINT FNM(B,C#,A%);FNM(FNA(1,2),FNA(X,Y),1);A%;B;C#
100 DEF FNZ=X+Y:PRINT FNZ
