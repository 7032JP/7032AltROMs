0 REM AltROMs test - MIT, own work.
1 REM KEY: 割り当て・空文字列による消去・KEY LIST の一覧表示 (PF<番号> に
2 REM 続けて桁 9 から割り当て文字列。割り当てのないキーは PF<番号> のみ)。
3 REM KEY ON / KEY OFF は受け付けず Syntax Error (誤り 2) になる。
10 KEY 1,"AAA":KEY 2,"BB":KEY 3,"C3":KEY 4,"D4":KEY 5,"E5"
20 KEY 6,"F6":KEY 7,"G7":KEY 8,"H8":KEY 9,"I9":KEY 10,"RUN"
30 KEY 3,""
40 KEY LIST
50 ON ERROR GOTO 900
60 E=0:KEY ON
70 PRINT "E";E
99 END
900 E=ERR:RESUME NEXT
