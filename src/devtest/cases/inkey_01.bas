0 REM AltROMs test - MIT, own work.
1 REM INKEY$: 押されたキー 1 文字を返す。押されるまでは長さ 0 の文字列。
2 REM (実行中の打鍵は inkey_01.stdin: A の 1 文字)
10 K$=INKEY$
20 IF K$="" GOTO 10
30 PRINT ASC(K$);"[";K$;"]"
