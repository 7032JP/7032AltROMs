0 REM AltROMs test - MIT, own work.
1 REM PRINT USING の数値フィールドの桁位置 # は 24 個まで。25 個以上は
2 REM 誤り 5 (Illegal Function Call) になる。
10 ON ERROR GOTO 900
20 E=0:PRINT USING "########################";1
30 PRINT "E";E
40 E=0:PRINT USING "#########################";1
50 PRINT "E";E
99 END
900 E=ERR:RESUME NEXT
