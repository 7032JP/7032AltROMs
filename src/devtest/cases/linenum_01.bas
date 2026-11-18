0 REM AltROMs test (MIT)
1 REM 行番号の綴りが 65535 を超えるとき: GOTO は Undefined Line Number になり、
2 REM 行として打ち込んだものは Syntax Error で本文に入らない。
5 IF C=1 THEN PRINT "NG":END
10 ON ERROR GOTO 100
20 C=1:GOTO 65536
30 END
100 PRINT "E";ERR;ERL:END
65537 PRINT "NG2"
