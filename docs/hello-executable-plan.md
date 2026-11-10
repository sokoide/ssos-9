# 外部実行可能モジュールと Hello の実装計画

## 目標

クロスコンパイラで作成したプログラムを S9M1 に変換して読み込み、shell から起動する。`Hello\n` を表示して終了し、shell が次のコマンドを受け付けることを確認する。既存の foreground/background、参照数、終了待ちと回収の契約を維持する。

Linux の `execve` と異なり、shell は残り、子 process を spawn して foreground なら wait する。プロセスは supervisor mode の共有アドレス空間で動くため、検証は不正な入力の拒否でありメモリ保護ではない。

## 構成と境界

1. `~/.elf2x68k` の m68k クロスツールで module 用 ELF を生成する。linker script はコード、初期データ、BSS、入口、GOT の配置を明示する。
2. host 側 packer が対応する ELF のみを検証し、既存 S9M1 の code、data、BSS、GOT、fixup を生成する。未知の relocation、未解決シンボル、範囲外、非対応 section は拒否する。初版の対応範囲は実際の compiler 出力と試験で確定する。
3. bounded reader で image を arena に読み込み、既存 validator を通して registry に登録する。登録成功時と失敗時の所有権、参照中 unload の拒否、解放を明示する。ファイルシステム未実装の間は ROM 内の生成済み image を reader の入力として使い、外部ファイル対応とは区別する。
4. stdout 用の最小 write syscall と user wrapper を追加する。固定幅 ABI、buffer の範囲検証、部分書込み、エラー、console の直列化、kill と preemption の境界を定義する。既存 syscall の番号や layout を変更しない。
5. startup が `main` を呼び、その戻り値を exit syscall へ渡す。entry からの `rts` は使わない。引数と C runtime の対応範囲は必要最小限に限定する。
6. shell から Hello を起動し、終了を待つ。子の stack、private data、module 参照を回収し、shell の入力に戻る。背景起動と Ctrl-C は既存契約を維持する。

## 初版 write ABI の設計

既存の `SS9_SYS_WRITE` 番号を使い、D1 は 4 byte 整列した request を指す。request は `uint32_t fd`, `uint32_t buffer`, `uint32_t length` の順とする。初版は fd 1 の console のみ受け付け、成功時は書き込んだ byte 数、失敗時は負のエラーを D0 に返す。length は一回につき最大 256 byte とし、0 byte は buffer に触れず 0 を返す。予約されている番号の実装であり、既存 request の意味は変更しない。

request 全体を検証してからローカルにコピーし、buffer 全体について process の読取り可能範囲を検証する。加算の overflow と範囲外を拒否し、検証後も syscall guard の下で process の資源を保持する。console 出力は既存の直列化を使い、NUL を含むデータを文字列 API で切り詰めない byte 単位の入口を設ける。IOCS 出力に部分失敗を表す手段がなければ、検証後の全 byte を出力して length を返す。console backend の検証可能な失敗はエラーとして扱う。

試験では request の非整列、範囲外、アドレス overflow、未対応 fd、長さ上限、0 byte、NUL を含む入力を確認する。QEMU 側は代替 console backend で出力 byte を記録し、実 IOCS の検証とは分ける。

## 現在の実装範囲

`examples/hello` は `~/.elf2x68k` のツールで C から ELF と S9M1 を生成し、OS に読み込み元として組み込む。初回 `run hello` は reader で検証・動的登録し、spawn して foreground wait する。別途 `load 1 HELLO.S9M` はドライブ 1 の 2HD データディスクから検証・登録する。QEMU では代替 IOCS を用いて生成したデータディスクから load・run・wait・unload を検証している。

現行 packer は配置済み `.text` と `R_68K_PC16` のみを扱い、空でない relocation section を必須とする。実データ、BSS、GOT の生成や一般的な C プログラムは未対応。relocation section があっても絶対アドレス命令や手書きコードの PIC 性を ELF から証明できないため、信頼できるソースと指定 build profile に限定する。外部読み込みは別の 2HD ディスク上の 1024 バイト sector、MS-DOS 型 FAT12 の root にある ASCII 8.3 名に限定する。

2026-11-01 にユーザーは XEiJ 0.26.10.08 上で `run hello` により `Hello` と終了状態 0、shell 復帰を確認したと報告した。起動用に案内した `F:\\SS9TEST.X` の SHA-256 は `6469cd00b36e0dc7f63c148d2901b0dab6c0555524b5c4dba557f621ce653321`（実際に選択したファイル名の明示的な確認は未取得）。これは組み込み読み込み元の検証で、外部ディスクの IOCS と実機の検証ではない。

その後 `ss9_write` の request を明示的に 4 バイト整列した構造体に変更した。`m68k-xelf-gcc` 13.5.0 の Hello 逆アセンブルでは、request address をスタック位置から求めて `andl #-4` で整列し、`trap #0` の D1 に渡すことを確認した。変更後の生成 Hello は QEMU で実行済みだが、XEiJ での再確認は未実施。新しい検証用 `.x` は `SS9T4.X`、別データディスクは `DATA4.XDF` である。

## 検証

- packer: 正常 ELF と、未知 relocation、未定義 symbol、破損、範囲外、サイズ溢れを検査する。生成 S9M1 を既存 validator に通す。
- kernel: reader の短い入力・失敗時の資源回収、write の buffer 検証、出力と終了 status、wait と参照数を検査する。
- QEMU: 生成した Hello を実際に spawn し、出力、exit、shell 相当の親への復帰を検証する。Timer D の代用経路であることを記録する。
- クロスビルド: `source ~/.elf2x68k` 後に Hello の ELF と S9M1、および OS image を生成する。`readelf` と `objdump` で入口と relocation を確認する。
- 実機または対応エミュレータ: IOCS 出力、shell 復帰、実 MFP 割り込みを別途確認する。QEMU 成功をその証拠としない。

## 実装順と完了条件

1. 出力 ABI と ROM reader の契約を決め、失敗系の試験を追加する。
2. 最小 ASM module の ELF から S9M1 を生成し、既存 validator と QEMU で起動・終了を検証する。
3. write syscall と console 接続を実装し、Hello の出力と shell 復帰を検証する。
4. C startup、wrapper、必要な relocation を追加し、C の Hello を同じ経路でビルド・実行する。
5. ファイルシステム実装後、同じ bounded reader 契約に外部ファイルを接続し、ロード・unload と失敗時の回収を検証する。

完了は C の Hello のビルドだけでなく、生成 image の検証、出力、終了、親への復帰までを指す。ROM 版が動いても外部ファイルのロードは完了扱いにしない。
