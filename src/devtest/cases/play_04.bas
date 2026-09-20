0 REM AltROMs test - MIT, own work.
1 REM PLAY の響き: Vn (音量 0-15) と Sn / Mn (エンベロープ形状・周期)。
2 REM V0 は音を出さない。エンベロープ中の音量レジスタは形状ビットが立つ
3 REM (期待値は play_04.expected_sound)。
10 PLAY "V0C"
20 PLAY "V15CV4D"
30 PLAY "S8M1000E"
