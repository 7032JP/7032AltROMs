0 REM AltROMs test - MIT, own work.
1 REM ERASE: 消去後は同名を別の大きさで DIM し直せる。複数指定できる。
2 REM 存在しない名前は何も起こらない。消去せずに再 DIM すると誤り 10
3 REM (Duplicate Definition) になる。
10 DIM Z(3):Z(1)=4:ERASE Z:DIM Z(5):PRINT Z(1)
20 DIM Y(2),X(2):Y(1)=7:X(1)=8:ERASE Y,X:DIM Y(9):PRINT Y(1)
30 ERASE Q:PRINT "OK"
40 DIM Z(5):PRINT "NG"
