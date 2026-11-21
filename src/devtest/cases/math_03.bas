0 REM AltROMs test (MIT)
1 REM 単精度の加減算で丸めの繰り上がりが仮数の全桁に及ぶとき (1-1E-8 など) の値と、
2 REM それを使う SIN / COS / TAN / ATN の小さな値の範囲。
10 X=1E-4:S=X*X:PRINT 1-S;(1-S)*X;1+S
20 PRINT SIN(1E-4);SIN(-1E-4);TAN(1E-4);ATN(1E-4)
30 PRINT COS(878.075);ATN(.9997041);ATN(5000)
40 A=SIN(1E-4):PRINT A*1;SIN(3E-4)
