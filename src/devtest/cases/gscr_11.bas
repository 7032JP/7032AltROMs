0 REM AltROMs test - MIT, own work.
1 REM 画面比較: CONSOLE のスクロール領域 (開始行 16 から 4 行) と COLOR の
2 REM 文字色。領域の中だけがスクロールし、LOCATE の行は領域に関係なく
3 REM 画面の絶対行。
10 WIDTH 40:CLS
20 CONSOLE 16,4
30 COLOR 6
40 FOR I=1 TO 10:PRINT "S";I:NEXT
50 LOCATE 10,0:COLOR 3:PRINT "ABS";
60 GOTO 60
