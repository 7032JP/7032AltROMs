0 REM AltROMs test - MIT, own work.
1 REM PRINT の区切り: ; は連続、, は 14 桁のゾーン区切り (1 行 2 ゾーン)、
2 REM TAB(n) は桁位置合わせ、SPC(n) は空白 n 個、行末 ; は改行の抑止。
3 REM 数値は符号 (正は空白) + 数字 + 後続空白 1 個の書式。
10 PRINT "A";"B"
20 PRINT 1,2
30 PRINT TAB(10);"X";SPC(3);"Y"
40 PRINT "P";
50 PRINT "Q"
60 PRINT -1;2;-3
70 PRINT 1,2,3,4
