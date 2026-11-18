0 REM AltROMs test (MIT)
1 REM 8 進定数: & に続く 8 進の数字はそのまま 8 進として読む (&O を書いても同じ)。
2 REM プログラムの中の定数・VAL・DATA で同じ値。& の後に数字が無ければ 0 で、続く文も動く。
10 PRINT VAL("&7");VAL("&17");VAL("&O17");VAL("&H1F")
20 PRINT &17;&O17;&H1F;&7
30 A=&:PRINT "A";A
40 B=&17+1:PRINT "B";B
50 READ C,D:PRINT C;D
60 DATA &17,&O17
