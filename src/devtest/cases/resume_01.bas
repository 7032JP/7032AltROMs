0 REM AltROMs test - MIT, own work.
1 REM RESUME (引数なし) の再開位置は誤りが起きた文の先頭。IF~THEN の節は
2 REM 切れ目に数えないので IF の条件判定からやり直す。: で区切った行は
3 REM : の直後からやり直す。
10 ON ERROR GOTO 100
20 Z=0:T=0:D=0
30 IF D=0 THEN X=1/Z
40 PRINT "A";T;X
50 U=0:V=0:W=0:U=U+1:W=1/V
60 PRINT "B";U;W
70 END
100 T=T+1:IF ERR=11 THEN Z=2:V=4:RESUME
110 END
