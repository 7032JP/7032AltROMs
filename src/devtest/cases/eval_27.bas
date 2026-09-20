0 REM AltROMs test - MIT, own work.
1 REM アドレスを受け取る文は 16 ビットのアドレスをそのまま受ける。基数付き
2 REM 定数は符号なしなので &H8000 以上の綴りもアドレスとして通ること。
10 SAVEM"T",&HE000,&HE00F,&HE000
20 PRINT "SM"
30 DEFUSR0=&HE000
40 PRINT "DU"
50 CLEAR 100,&H7FFF
60 PRINT "CL"
