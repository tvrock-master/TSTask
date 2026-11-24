汎用TS処理プログラム実装研究資料
================================
汎用TS処理プログラム実装研究資料(略称 TSTask)は、パーソナルコンピュータ上において、MPEG-2 TS の処理を行うプログラムの実装を研究する目的で頒布される研究資料です。

この資料は MPEG-2 TS を扱うための基本的な機能を実装しています。  


このフォークについて
--------------------

このリポジトリは、MPEG-2 TS の録画に使うために [DBCTRADO/TSTask](https://github.com/DBCTRADO/TSTask) を改編したフォークです。
TvRock依存のコードを除外し、対応する TVTest プラグインを読み込めるように改編しています。
TVTestのプラグインである TvRockOnTVTest プラグイン経由で TvRockGO からの選局や録画操作を受け付けます。  
TVTestと同じフォルダにTSTaskの実行ファイル群を置くことで利用しやすくなります。


| 項目 | 追加・修正された内容 |
|---|---|
| TVTest プラグイン対応 | `TSTask.ini` の `[Plugins]` に指定した対応プラグインを読み込めるようになりました。TvRockOnTVTest 経由で TvRockGO からの選局・録画操作・終了を受け付け、対応する TS 後処理プラグインに TS パケットを渡せます。 [プラグインの設定](#プラグイン) · [TvRockGO との連携](#tvrockontvtest-で-tvrockgo-と連携する) |
| 録画データの書き込み | ストリーム切り替え時に書き込み待ちのデータを保持し、部分書き込みが起きた場合は残りを書き込みます。録画停止時に保存しきれなかったデータはログに記録します。 [TsRecorder.cpp](BonTsEngine/TsRecorder.cpp#L433) |
| 録画先の切り替え | DiskRelay は空き容量が設定値以下になったときに予備の保存先へ切り替えます。TSTask 本体は空き容量が設定値未満になったときに警告を出します。書き込みエラー時に既存の代替保存先へ切り替える処理も、録画停止と競合しにくくなっています。 [DiskRelay の設定](#diskrelay-で録画中に保存先を切り替える) · [TSTaskCore.cpp](TSTask/TSTaskCore.cpp#L981) |
| 録画ファイル名 | ファイル名のテンプレートで、番組の開始・終了日時や放送時間を指定できるようになりました。 [VariableString.cpp](Common/VariableString.cpp#L204) |
| 予約録画 | 停止タイマーを録画ごとのIDに紐付け、前の録画の停止通知が遅れて届いたときに、次の録画まで停止するのを防ぎます。 [ReserveManager.cpp](TSTask/ReserveManager.cpp#L279) |
| 起動・選局・終了 | 初期化後に TSTaskCentre などからのパイプ経由の操作を受け付け、終了時は先にその受付を止めるようになりました。選局や番組情報の更新通知はメインスレッドで処理し、受信処理とのロック競合を抑えています。 [TSTask.cpp](TSTask/TSTask.cpp#L353) · [TSTaskCore.cpp](TSTask/TSTaskCore.cpp#L1669) |
| TSTaskCentreの表示・通信 | 開始通知を受けると一覧にタスクの行を先に表示し、通知登録とデバイスID・ドライバー・チャンネル・番組情報の初回取得は別スレッドで進めます。起動直後に通知登録が失敗した場合は再試行します。終了処理中と判明しているプロセスについては、パイプが消えた際の `Code 0x2` の誤表示も抑えています。 [MainBoard.cpp](TSTaskCentre/MainBoard.cpp#L359) · [TSTaskManager.cpp](TSTaskCentre/TSTaskManager.cpp#L986) · [MessageClient.cpp](Common/MessageClient.cpp#L100) |
| TSTaskCentreの画面下端表示 | 「設定」→「一般」の「画面下端に横幅いっぱいで表示する」を有効にすると、ウィンドウのあるモニターの作業領域の横幅いっぱいに広がり、下端に固定されます。`TSTaskCentre.ini` の `[Settings]` では `MainBoard.DockBottom=true`、起動オプションでは `/dockbottom` で指定できます。`/dockbottom-` は起動時に無効にします。 [MainBoard.cpp](TSTaskCentre/MainBoard.cpp#L467) |
| TSTaskCentreの常駐 | 既定ではすべての TSTask が終了しても TSTaskCentre は起動したままになります。自動終了するには「設定」→「一般」で「全ての TSTask が終了したら TSTaskCentre を終了する」を有効にします。 |
| PowerShellビルド | `-Platform Both` で x64 と x86 を続けてビルドでき、`-Platform x86` も指定できます。 [build.ps1](build.ps1#L19) |


PowerShell でビルドする
----------------------

- PowerShell 5.1 以降を使用します。
- Visual Studio 2022 または Visual Studio 2022 Build Tools で「C++ によるデスクトップ開発」をインストールします。MSBuild、MSVC v143 の x64/x86 ビルドツール、Windows SDK が必要です。

PowerShell で `TSTask.sln` と `build.ps1` があるリポジトリのルートを開き、次のいずれかを実行します。

```powershell
# x64 と x86 の Release 版をビルド
.\build.ps1 -Platform Both

# x64 の Release 版をビルド（既定）
.\build.ps1

# x86 の Release 版をビルド
.\build.ps1 -Platform x86

# x86 の Debug 版を再ビルド
.\build.ps1 -Platform x86 -Configuration Debug -Rebuild
```

`-Configuration` の既定値は `Release` です。`-Rebuild` を付けると、指定した構成を最初からビルドし直します。スクリプトは MSBuild を自動で探します。特定の MSBuild を使う場合は `-MSBuildPath "C:\path\to\MSBuild.exe"` を指定できます。

ビルドした `TSTask.exe`、`TSTaskCentre.exe`、`BonDriver_TSTask.dll` は、リポジトリのルートから見て次のフォルダに出力されます。

| 対象 | Release | Debug |
| --- | --- | --- |
| x64 | `x64\Release` | `x64\Debug` |
| x86 | `Win32\Release` | `Win32\Debug` |


GitHub Actions でテスト・リリースする
-------------------------------------

ブランチへ push すると、x64 と x86 の Release ビルドを行い、実行ファイルと DLL のアーキテクチャ、および実行ファイルのバージョン情報を検証します。手元ではビルド後に `./test.ps1 -Platform x64` または `./test.ps1 -Platform x86` で同じ検証を実行できます。

プラグイン連携テストでは、模擬 BonDriver が送る TS を使い、実際の TvRockOnTVTest と模擬 TvRockDTV を通じて選局・録画開始・停止・終了を確認します。DiskRelay は実際の TVTest SDK サンプルを使い、空き容量の応答をテスト用に差し替えます。切り替え前後のファイルに選局したサービスの TS が保存されることを検証します。プラグインの欠落と初期化失敗では、ログを出し、通常の録画を続けられることを確認します。

ローカルで連携テストを実行するには、TSTask の Release ビルド後に `./tests/Test-PluginIntegration.ps1 -Platform x64 -TvRockOnTVTestSource ../tvrockontvtest -TvTestSource ../TVTest` を使います。x86 版は `-Platform x86` を指定します。

リリースするコミットに `vMAJOR.MINOR.PATCH` 形式のタグを付け、GitHub へ push します。

```powershell
git tag -a v0.2.1 -m 'TSTask 0.2.1'
git push origin v0.2.1
```

タグのコミットからビルドし、`TSTask.exe` と `TSTaskCentre.exe` にタグのバージョン（例では `0.2.1`）を埋め込みます。両アーキテクチャの検証に通ると、`TSTask-v0.2.1-x64.zip` と `TSTask-v0.2.1-x86.zip` を Actions の成果物と GitHub Release の両方から取得できます。ローカルで同じバージョンを埋め込むには、`./build.ps1 -Platform Both -VersionTag v0.2.1` を使います。


プラグイン
----------
TSTask.ini の [Plugins] に指定された .tvtp を、TSTask.exe と同じフォルダにある Plugins フォルダから読み込みます。/ini で設定ファイルを指定した場合は、そのファイルの [Plugins] を使います。

```ini
[Plugins]
Plugin1=xxx.tvtp
; Plugin1.Module=xxx.xxx
```

ファイル名だけを指定します。
`xxx.tvtp` と `xxx.xxx` は、それぞれ拡張子を含む実際のファイル名に置き換えてください。
複数のプラグインは Plugin1、Plugin2 のように列挙します。TS 後処理用のプラグインは番号順に実行されます。空欄やコメントアウトで無効にできます。設定変更は TSTask の再起動後に反映されます。

TS 後処理プラグインがフィルターモジュールを必要とする場合は、同じ番号の `PluginN.Module` のコメントを外して有効にしてください。モジュールは `TSTask.exe` の隣に置きます。モジュールを必要としないプラグインでは、この項目は省略できます。

TSTask は対応する TVTest API で TS 後処理を登録し、入力パケットごとに同期的に 1 個の TS パケットを返すプラグインをサポートします。プラグインは TSTask.exe と同じアーキテクチャを使用してください。読み込みや初期化に失敗した場合はログに記録し、ほかのプラグインや録画処理を継続します。

TSTask のビルドには同梱の TVTest SDK ヘッダー（CC0）を使用します。


TvRockOnTVTest で TvRockGO と連携する
------------------------------------
[Plugins] に TvRockOnTVTest.tvtp または TvRockOnTVTest.x64.tvtp を追加します。すでに `Plugin1` を使っている場合は、`Plugin2` など空いている番号を指定してください。

    [Plugins]
    Plugin2=TvRockOnTVTest.x64.tvtp

プラグインを Plugins フォルダに置き、同じアーキテクチャの TvRockDTV.dll（x64 では TvRockDTV.x64.dll も可）を TSTask.exe の隣または Plugins フォルダに置きます。TvRockGO からは従来と同じく /did A などを指定して起動します。実際の x64・x86 のファイル名に合わせて、INI の値を変更してください。

プラグイン経由で選局、録画開始・停止、終了を受け付け、受信状態と TS を TvRockDTV に渡します。ファイル名だけの録画要求には `Recording.Directory` で設定した保存先を使用します。プラグイン側のチューニング空間を指定する場合は、.tvtp と同じ場所・同じベース名の .ini に [DID_A] などを設定します。起動時のエラーは標準ログに記録します。


起動時の選局
------------
`/d` で BonDriver を指定して起動すると、コマンドラインの選局指定が優先されます。選局指定を省略すると、同じ BonDriver で前回選局したサービスを復元します。選局履歴はチューナーを閉じるときに保存されます。設定ファイルが `TSTask.ini` なら、履歴ファイルは同じフォルダの `TSTask.ini.channels.ini` です。

前回の選局先を指定できない場合に使うサービスは、設定ファイルの `[Settings]` に SID で指定できます。

```ini
[Settings]
Tuner.DefaultServiceID=161
```

空欄、`0`、または指定した SID がチャンネル一覧から選局できない場合は、`.ch2` または `.ch1` の有効なチャンネルを先頭から試します。そこから選局できない場合は、BonDriver が返すチャンネル一覧を使います。


DiskRelay で録画中に保存先を切り替える
------------------------------------
TSTask と同じアーキテクチャの `DiskRelay.tvtp` を `Plugins` フォルダに置き、TSTask.ini の `[Plugins]` に空いている番号で追加します。

```ini
[Plugins]
Plugin3=DiskRelay.tvtp
```

`DiskRelay.tvtp` と同じ場所に `DiskRelay.ini` を置きます。既存の予備フォルダをフルパスで指定します。`LowFreeSpace` の単位は MiB です。`DiskRelay.x64.tvtp` というファイル名で使用する場合は、設定ファイル名も `DiskRelay.x64.ini` にします。

```ini
[Settings]
Folder0=E:\Recordings
LowFreeSpace=1024
```

予備フォルダは `Folder0` から `Folder2` まで指定できます。録画先の空き容量がしきい値以下になると、しきい値より空きのある予備フォルダへ録画を継続します。切り替え後は `.part2.ts` などの別ファイルに保存されます。予備フォルダの指定がない場合は、標準ログに記録して DiskRelay を無効にします。


録画ファイル名と空き容量
------------------------
録画ファイル名には、番組の開始日・開始時刻（%start-date%、%start-time%）、終了日・終了時刻（%end-date%、%end-time%）、番組の長さ（%event-duration-hour%、%event-duration-min%、%event-duration-sec%）を指定できます。長さの各項目は末尾に 2 を付けると 2 桁になります。取得できない開始・終了日時は x に置き換わります。

録画中、保存先の空き容量が Recording.MinFreeSpace（バイト単位）を下回ると、ファイルごとに一度ログに警告を出します。0 を指定すると空き容量の警告を無効にできます。

ライセンス
----------
本プロジェクトは [DBCTRADO/TSTask](https://github.com/DBCTRADO/TSTask) の改変版です。フォークの公開履歴で最初の改編コミットは 2026-11-24 に記録されました。TSTask 本体は上流と同じ [GNU General Public License, version 2](LICENSE.md) に従います。原著作者と同梱素材の表記は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。

GitHub Actions の各配布 ZIP には、ライセンス文書と、バイナリのビルドに使用したコミットのソース一式 `TSTask-source-<タグ>.zip` を同梱します。
