0 REM AltROMs test - MIT, own work.
1 REM TIME$ / DATE$ の設定と参照 (設定した値を読み返す)。DATE$ は YY/MM/DD。
2 REM 何も押していないときの INKEY$ は長さ 0。
10 TIME$="12:34:56":PRINT TIME$
20 DATE$="26/07/23":PRINT DATE$
30 PRINT LEN(INKEY$)
