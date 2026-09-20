0 REM AltROMs test - MIT, own work.
1 REM POS(0) は画面上のカーソルの水平位置を与える (参考書籍の規定)。
2 REM LOCATE の直後、文字を書いた後、改行した後の 3 通りを読む。
10 CLS
20 LOCATE 12,2:P1=POS(0)
30 PRINT "ABCD";:P2=POS(0)
40 LOCATE 30,6:P3=POS(0)
50 PRINT
60 P4=POS(0)
70 LOCATE 0,9
80 PRINT P1;P2;P3;P4
90 END
