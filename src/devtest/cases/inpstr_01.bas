0 REM AltROMs test (MIT)
1 REM INPUT$(n): キーボードから n 文字を受け取る。打った文字は画面に出ない。
2 REM 式の途中にも書ける。打鍵は inpstr_01.stdin の ABCDE。
10 A$=INPUT$(3)
20 PRINT LEN(A$);A$
30 B$=CHR$(60)+INPUT$(2)+CHR$(62)
40 PRINT B$;LEN(INPUT$(0))
