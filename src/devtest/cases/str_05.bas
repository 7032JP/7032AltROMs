0 REM AltROMs test - MIT, own work.
1 REM 文字列関数の境界: STRING$ の文字コード形式、INSTR の開始位置と端
2 REM (見つからない 0 / 開始が長さ超え 0 / 相手が空なら開始位置 / 0 以下は誤り 5)、
3 REM SPACE$ の値域 (0-255)、MID$ の 2 引数形式、LEFT$/RIGHT$ の 0 と長さ超え、
4 REM OCT$。
10 PRINT STRING$(3,65);STRING$(2,"AB")
20 PRINT INSTR(3,"ABCABC","B");INSTR("ABC","X");INSTR(9,"ABC","A");INSTR(2,"ABC","")
30 ON ERROR GOTO 900
40 E=0:PRINT INSTR(0,"ABC","A"):PRINT "E";E
50 E=0:A$=SPACE$(256):PRINT "E";E
60 E=0:B$=SPACE$(0):PRINT LEN(B$);"E";E
70 PRINT MID$("ABCDE",3);LEFT$("ABC",0);"|";RIGHT$("ABC",5)
80 PRINT OCT$(8);" ";OCT$(255)
90 END
900 E=ERR:RESUME NEXT
