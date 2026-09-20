0 REM AltROMs test - MIT, own work.
1 REM PLAY の高さ指定: On (オクターブ)、Nn (通し番号)、# + - (半音)。
2 REM O4A=440Hz(周期175)、O5A=880Hz(87)、N46=O4A、C#=C+=D-=277。
3 REM 文字列式 (変数) も使える (期待値は play_02.expected_sound)。
10 PLAY "O4AO5AN46"
20 A$="O4C#DC+ED-":PLAY A$
