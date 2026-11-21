0 REM AltROMs test (MIT)
1 REM SQR と単精度・倍精度の値の表示を並べた繰り返しが最後まで走る。
40 C=0:D#=0:FOR I=1 TO 12:PRINT I;:C=C+SQR(I):PRINT "C";:A=SQR(I*7.3):PRINT "A";A;:D#=D#+A*1024:PRINT D#:NEXT
80 PRINT "DONE":END
