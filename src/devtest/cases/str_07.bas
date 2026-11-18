0 REM AltROMs test (MIT)
1 REM VAL: 符号は 1 つ ("--5" の 2 文字目は数字でないので 0)。指数が小さすぎる値は 0、
2 REM 大きすぎる値は Overflow (誤り 6)。
5 ON ERROR GOTO 100
10 PRINT VAL("--5");VAL("-5");VAL("+-5")
20 PRINT VAL("1E-999")
30 PRINT VAL("1E999X")
40 PRINT "END":END
100 PRINT "ERR";ERR;"ERL";ERL:RESUME NEXT
