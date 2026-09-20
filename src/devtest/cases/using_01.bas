0 REM AltROMs test - MIT, own work.
1 REM PRINT USING (数値): # の右詰め、小数の丸め、コンマ区切り、** と円記号の
2 REM 埋め、符号 (先頭 + / 末尾 + / 末尾 -)、収まらない値の % 表示、
3 REM 書式に混ぜた文字と _ による打消し。
10 PRINT USING "####";42
20 PRINT USING "###.##";3.14159
30 PRINT USING "#,###,###";123456
40 PRINT USING "**####";12
50 PRINT USING "\\####";12
60 PRINT USING "**\####";12
70 PRINT USING "+###";42;-42
80 PRINT USING "###+";42;-42
90 PRINT USING "###-";42;-42
100 PRINT USING "###";12345
110 PRINT USING "A#B_#C#D";7;8
