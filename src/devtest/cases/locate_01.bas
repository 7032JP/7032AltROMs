0 REM AltROMs test - MIT, own work.
1 REM LOCATE は水平位置・垂直位置へカーソルを移す (参考書籍の規定)。
2 REM 水平位置を 2 桁ずつ、垂直位置を 1 行ずつ進めて 6 回書き、各行がその位置から
3 REM 始まることを確かめる。
10 CLS
20 FOR K=0 TO 5
30 LOCATE 3+K*2,K
40 PRINT "<";K;">"
50 NEXT K
60 END
