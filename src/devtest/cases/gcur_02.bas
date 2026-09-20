0 REM AltROMs test - MIT, own work.
1 REM GCURSOR の座標の組は 1 個以上 10 個まで。組が無いときと 11 個あるときは
2 REM いずれも綴りの誤り (Syntax Error = 2) として捕まる。
10 ON ERROR GOTO 900
20 E=0:GCURSOR (100,50):PRINT "A";E
30 E=0:GCURSOR (100,50),(X,Y),(X,Y),(X,Y),(X,Y),(X,Y),(X,Y),(X,Y),(X,Y),(X,Y),(X,Y),(X,Y)
40 PRINT "B";E
50 END
900 E=ERR:RESUME NEXT
