0 REM AltROMs test - MIT, own work.
1 REM 機能キー行を出した後に第 3 引数を 0 へ戻すと表示が消える
2 REM (参考書籍「CONSOLE 文」)。
10 KEY 1,"run":KEY 6,"ab"
20 CONSOLE 0,18,1
30 CONSOLE 0,20,0
40 PRINT "B"
50 GOTO 50
