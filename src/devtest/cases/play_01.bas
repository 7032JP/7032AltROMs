0 REM AltROMs test - MIT, own work.
1 REM PLAY の基本: 音名 C D E は音階どおりの周期 (実行環境の音源レジスタで
2 REM 294 / 262 / 233) で順に鳴り、既定 (T120 L4) の長さは各 0.5 秒 (30 フレーム)。
3 REM 演奏が終わると無音に戻る (期待値は play_01.expected_sound)。
10 PLAY "CDE"
