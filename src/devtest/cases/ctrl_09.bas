0 REM AltROMs test (MIT)
1 REM WHILE/WEND の入れ子を行をまたいで書く: 外側と内側の WHILE が別の行の
2 REM 同じ位置にあっても、それぞれの WEND が対の WHILE へ戻る。
10 I=0:J=0
20 WHILE I<2
30 WHILE J<2
40 PRINT I;J
50 J=J+1
60 WEND
70 I=I+1:J=0
80 WEND
90 PRINT "END"
