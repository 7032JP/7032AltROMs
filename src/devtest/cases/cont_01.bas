0 REM AltROMs test - MIT, own work.
1 REM CONT の再開不能: 中断後に CLEAR (変数の初期化) を行うと Can't Continue
2 REM (誤り 17) になる。(実行中の打鍵は cont_01.stdin: CLEAR → CONT)
10 PRINT "A"
20 STOP
30 PRINT "B"
