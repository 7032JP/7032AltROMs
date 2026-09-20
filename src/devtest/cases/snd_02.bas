0 REM AltROMs test - MIT, own work.
1 REM SOUND の引数の値域: レジスタ番号は 0-13、データは 0-255。範囲外は
2 REM 誤り 5 (Illegal Function Call) になり、範囲内は受理される。
10 ON ERROR GOTO 900
20 E=0:SOUND 14,0:PRINT "E";E
30 E=0:SOUND 15,0:PRINT "E";E
40 E=0:SOUND 8,256:PRINT "E";E
50 E=0:SOUND 13,0:PRINT "E";E
99 END
900 E=ERR:RESUME NEXT
