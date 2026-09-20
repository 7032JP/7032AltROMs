0 REM AltROMs test - MIT, own work.
1 REM STOP / CONT: Break In <行番号> を表示して中断し、CONT で停止した文の
2 REM 次の文から再開する。中断中に直接モードで代入した値は再開後に反映される。
3 REM (実行中の打鍵は ctrl_08.stdin: CONT → A=100 → CONT)
10 A=1:PRINT "P1";
20 STOP
30 PRINT "P2";A;
40 A=A+9:STOP
50 PRINT "P3";A
