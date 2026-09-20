0 REM AltROMs test - MIT, own work.
1 REM 実行時エラーの番号: 1 Next Without For / 3 Return Without Gosub /
2 REM 4 Out Of Data / 8 Undefined Line Number / 11 Division By Zero /
3 REM 18 Undefined User Function / 25 Wend Without While /
4 REM 20 Resume Without Error。ON ERROR GOTO で捕捉して番号を印字する。
10 ON ERROR GOTO 900
20 E=0:NEXT:GOSUB 800
30 E=0:RETURN:GOSUB 800
40 E=0:READ Q:GOSUB 800
50 E=0:GOTO 9999
55 GOSUB 800
60 E=0:Q=1/0:GOSUB 800
70 E=0:Q=FNZ(1):GOSUB 800
80 E=0:WEND:GOSUB 800
90 E=0:RESUME:GOSUB 800
95 END
800 PRINT E;:RETURN
900 E=ERR:RESUME NEXT
