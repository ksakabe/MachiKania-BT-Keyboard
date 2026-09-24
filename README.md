# MachiKania Bluetoothキーボードブリッジ

**Bluetoothキーボード → Raspberry Pi Pico 2 W → USB HID → MachiKania type P** を実現するファームウェアです。Classic HIDとBLE HID over GATT（HOGP）を、書き込むUF2で選択します。

| 使用するキーボード | 書き込むUF2（distフォルダ） |
|---|---|
| Buffalo BSKBB24BK、通常のClassic HID | `machikania_keyboard_classic.uf2` |
| Apple Wireless Keyboard A1314 | `machikania_keyboard_apple.uf2` |
| BLE HID over GATTキーボード | `machikania_keyboard_ble.uf2` |

1台のキーボードを接続します。ClassicとBLEの同時待受・自動切替は行いません。A1314用にはApple版を使用します。

BSKBB24BKはBluetooth 3.0／HID対応なので、BLEではなくClassic HIDを使用します。MachiKaniaには、レポートIDを持たない8バイトのUSB Boot Keyboardとして接続します。文字列に変換せず、キーコードと修飾キーを転送します。

**現状：試作実装です。実機でのペアリングとMachiKaniaへの入力確認は別途必要です。BSKBB24BK・A1314・BLEキーボードの実際のHIDディスクリプタはまだ取得していません。**

## 接続

```text
BSKBB24BK（内蔵電池）
    │ Bluetooth Classic HID
    ▼
Pico 2 W（このファームウェア、USBデバイス）
    │ Pico 2 Wのmicro-USB端子
    │ USBデータ通信対応ケーブル
    │ USB-Aメス ← micro-USB OTGホストアダプター
    ▼
MachiKania type PのPico（USBホスト）
```

- ブリッジ用に**追加のPico 2 W**を使います。MachiKania側のPicoにこのUF2を書き込まないでください。
- MachiKania側は、有線USBキーボードが使えるファームウェア・給電構成にしておきます。通常のUSBキーボードが動作することを先に確認します。
- OTGアダプターはMachiKania側に接続します。Pico 2 W側はUSBデバイスです。
- MachiKania側のUSB VBUSからPico 2 Wへ5 Vを供給する構成です。OTGアダプター自体は電源を生成しません。ホスト側に給電できる構成が必要です。
- BSKBB24BK付属ケーブルはメーカー仕様で**充電用**です。ブリッジとMachiKaniaの接続には、別途データ通信対応ケーブルを使ってください。
- キーボードの充電端子とMachiKaniaは接続しません。

## UF2の書き込み

1. 上表で選んだUF2を用意します（再生成方法は後述）。
2. ブリッジ用Pico 2 WのBOOTSELボタンを押しながらPCへ接続します。
3. 表示されたRP2350のドライブへUF2をコピーします。
4. 再起動後、PCから取り外し、上図のようにMachiKaniaへ接続します。

## 初回ペアリング（Buffalo / Classic）

初回はUARTログを見ながら行うと、PIN方式の違いや接続失敗を確認できます。USB側はキーボード専用で、シリアルポートは出しません。

| Pico 2 W | USB-UARTアダプター |
|---|---|
| GP0 / 物理ピン1（UART TX） | RX（3.3 Vロジック） |
| GND / 物理ピン3 | GND |

UART設定は **115200 baud、8N1、フロー制御なし**。ログ受信だけなのでアダプターTXや電源ピンの接続は不要です。5 VロジックやRS-232電圧のアダプターは接続しません。

1. 周辺の別のBluetoothキーボードはペアリングモードを解除します。本実装は最初に見つけたKeyboardクラスの機器を選びます。型番文字列での限定は行いません。
2. BSKBB24BKの電源を入れ、**Fnを押しながらペアリングキー1**を押し、LEDの点滅を確認します。キー位置は[メーカーの案内](https://www.buffalo.jp/support/faq/detail/15429.html)を参照してください。
3. ブリッジを起動します。基板LEDの点滅は探索・接続待ち、点灯はHIDの準備完了です。USB接続の成否はLEDだけでは判断できません。
4. UARTに `PAIR: type 0000 ...` が出た場合は、BSKBB24BKで `0000` を入力してEnterを押します。これは本ファームウェアが選択する旧方式のPINです。
5. UARTに別の6桁の番号が出た場合は、その番号をBSKBB24BKで入力してEnterを押します。SSPの番号は固定ではありません。
6. `Keyboard ready:` と表示されたら、MachiKaniaで英数字、Enter、Backspace、矢印、Shiftを確認します。

接続先アドレスとBluetoothリンクキーはフラッシュに保存されます。以後は保存した機器へ再接続します。ペアリング方式がどちらになるかは実機ログで確認してください。

## Apple Wireless Keyboard A1314

1. `machikania_keyboard_apple.uf2`を書き込みます。
2. Buffaloなど別のClassicキーボードが登録済みの場合は、次節のGP15操作で接続情報を消します。Apple版とClassic版は接続先の保存領域を共有します。
3. 以前接続していたMacなどからの自動接続を止め、キーボードを電源ONにして、検出待ちのLED点滅を確認します。[Apple公式の案内](https://support.apple.com/en-gb/119917)も参照してください。
4. UARTログの`PAIR:`に表示された番号をA1314で入力し、Returnを押します。旧方式では`0000`、SSPでは表示された6桁です。入力待ちには120秒を設けています。

通常のDeleteはBackspaceとして転送します。Apple独自のFn情報がReport Protocolで届く場合、Fn＋DeleteをForward Delete、Fn＋←／→をHome／End、Fn＋↑／↓をPage Up／Page Downに変換します。キーを押している途中でFnを離しても、押下時の変換を解放まで保持します。

ControlはCtrl、OptionはAlt、CommandはGUIとして転送します。CommandをCtrlに置き換える処理はありません。F1〜F12は標準のファンクションキーとして扱い、音量・輝度制御には変換しません。英数・かなを含むJISキーは元のUsageを転送し、MachiKania側が対応するキーだけが機能します。Boot Protocolへのフォールバック時は、独自Fn情報を利用できません。

## BLE HID over GATT

1. `machikania_keyboard_ble.uf2`を書き込みます。
2. BLEキーボードをペアリングモードにします。手順は各キーボードの説明書に従ってください。
3. ブリッジは広告またはスキャン応答のHIDサービスUUID **0x1812**（UUIDリストまたはService Data）、Keyboard Appearance **0x03C1**、または名前 **EWIN BT5.1 Keyboard**（大文字小文字は区別しない）から候補を選びます。初回は最初の候補を選ぶので、近くの他のHID機器のペアリングモードを解除しておきます。
4. UARTに6桁の番号が表示された場合は、BLEキーボードで入力してEnterを押します。Just Works方式の機器では番号入力はありません。
5. `BLE keyboard ready`が表示されてから、MachiKaniaで入力を確認します。

暗号化の成功後にHIDサービス・Report Mapを取得し、Input Reportの通知を購読します。Report IDあり／なし、複数HIDサービスのレポートを共通のUSBキーボード形式に変換します。Report MapにKeyboard Usage Pageがない機器は切断します。

ボンディング情報はSDKのLE Device DBに、接続先のidentity addressはClassicとは別の保存タグに記録します。再接続時はその接続先をホワイトリストに設定し、SDKのResolving Listも読み込みます。IRKを交換する機器のプライベートアドレス変更に対応する構成ですが、実機検証は未実施です。ボンディング情報を提供しない機器は再起動後に再ペアリングが必要です。

BLE版のGP15起動操作はBLEの保存先とBLEボンディング情報を消去します。Classicのリンクキーは消去しません。再暗号化に失敗し続ける場合は、キーボード側の登録も解除して再ペアリングしてください。

BLE版の制限：Report Protocol対応のHOGP機器が対象です。Boot Protocolへの自動フォールバック、アドレスによる手動指定、Numeric Comparisonの手動承認、キーボードLEDへの出力、マウス・Consumer Controlは未対応です。周辺のHIDマウスなどが最初に見つかると再試行の妨げになるため、その機器のペアリングモードを解除してください。

### EWIN BT5.1 Keyboardで探索から進まない場合

旧BLE版のログが`Scanning for BLE HID (0x1812)...`で止まる報告に対し、名前による候補検出と段階別ログを追加しました。**EWIN実機の広告を採取できていないため、UUID省略が原因だったと確定したものではありません。** 表示名の「BT5.1」だけでもClassic／BLEは判定できません。

1. 更新版の`machikania_keyboard_ble.uf2`を書き込み、UART起動ログに`EWIN discovery v2`があることを確認します。
2. キーボードがMacへ自動再接続しないようにし、キーボードを改めてペアリング待ちにします。
3. 保存済みの別のBLE機器へ接続している場合はGP15起動で登録を消します。UF2の上書きだけでは登録は消えません。
4. 30秒程度のUARTログを確認します。

| ログ | 分かること |
|---|---|
| `BLE scan command 200c status=00` | コントローラーがスキャン制御コマンドを受理 |
| `BLE scan: 0 reports` | この期間にBLE広告を受信していない |
| `BLE ADV ... name='...' ...` | 名前付き機器・接続候補の検出。各探索の最初の12件まで表示 |
| `Connecting BLE HID ...` | 候補を選び接続を要求 |
| `BLE connection complete: status=...` | 無線接続の成否 |
| `BLE pairing complete: status=... reason=...` | 認証結果 |
| `BLE HID instance=... descriptor=...` | HIDサービス／キーボードレポートの検出 |
| `BLE keyboard ready` | HID準備完了 |

探索中の受信件数は15秒ごとに表示します。15秒後にも出ずLEDも変化しない場合は、単に広告が条件不一致というだけでなく、処理の停止も調べる必要があります。LED更新は実時間に基づく0.5秒ごとの点滅に変更しました。接続完了はLEDだけでなく`BLE keyboard ready`で確認してください。

BLEスキャンが成功し、多数の広告を受信していてもEwinが候補にならない場合は、Classic版でも切り分けできます。`machikania_keyboard_classic.uf2`の`inquiry diagnostic v3`以降はClassic Inquiryの機器名（応答に含まれる場合）・アドレス・Class of Deviceを表示します。

キーボードをMacから切断してペアリング待ちにし、Classic版で30秒程度のログを取得します。以前BuffaloやAppleを登録した場合は、GP15起動でClassicの登録を消してから探索してください。`Classic found ...`で候補を確認し、`Connecting to ...`、`PAIR:`、`Keyboard ready:`まで進むかを調べます。これは接続方式の診断であり、EwinがClassic専用と確定したことを意味しません。名前が応答にない場合もKeyboardクラスなら接続を試みます。

### 接続情報を消す

ブリッジの電源を切り、**GP15（物理ピン20）をGNDへ接続した状態で起動**します。起動時に保存先とリンクキーが消去されます。ジャンパーを外してから再ペアリングしてください。キーボード側に古い接続先が残る場合も、ペアリングモードに入れ直します。

## ビルド

検証対象はPico SDK **2.2.0**、Arm GNU Toolchain **14.2.Rel1**、`PICO_BOARD=pico2_w`です。SDKのBTstack・TinyUSB・CYW43などのサブモジュールが必要です。

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DPICO_BOARD=pico2_w \
  -DPICO_TOOLCHAIN_PATH=/path/to/arm-toolchain \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

生成物：`build/machikania_keyboard_classic.uf2`、`build/machikania_keyboard_apple.uf2`、`build/machikania_keyboard_ble.uf2`。インストール済みpicotoolが自動検出されない場合は、`-Dpicotool_DIR=/path/to/picotool/cmake-directory`を指定します。

ホスト上のテスト（Clang/GCCとAddressSanitizer／UndefinedBehaviorSanitizerが必要）：

```sh
cmake -S tests -B build/tests -DPICO_SDK_PATH="$PICO_SDK_PATH"
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

## 実装範囲と制限

- Classic HIDのReport Protocolを使用し、Boot Protocolへのフォールバックも扱います。
- 通常キーは最大6個＋修飾キー8ビット。6個を超える場合はUSBのErrorRollOverを送ります。
- 最大8組のレポートID／サービスのキー状態を統合します。メディア用レポートで通常キーを解放しない構成です。
- USB送信待ちのキューで押下と解放の順序を保持します。満杯になった場合は古いイベントを破棄し、全キー解放後に最新状態へ復帰します。
- Bluetooth切断時には全キーを解放します。接続処理のタイムアウト後、スタックが終了できない場合はブリッジを再起動します。
- Caps LockなどのUSB Output Reportは受け取りますが、Bluetooth側のLEDには反映しません。
- マウス、タッチパッド、音量などのConsumer Controlは対象外です。
- JIS／US配列の文字解釈はMachiKania側に依存します。Apple版のFn変換は上記の範囲です。
- USB VID/PIDは試作用です。製品として配布する場合は適切な識別子・依存ライブラリのライセンスを確認してください。

## 実機確認項目

- 初回ペアリング成功、再起動後の再接続、キーボードのスリープ復帰
- 英数字と記号、左右Shift/Ctrl/Alt、矢印、Enter、Backspace
- 素早い連続入力、複数キー、長押しのリピート
- キーを押した状態でキーボードをOFFにし、押しっぱなしにならないこと
- USBを抜き差しした後の復帰、GP15によるペアリング情報消去

## 参照

- [BSKBB24BK公式仕様](https://www.buffalo.jp/product/detail/bskbb24bk.html)
- [BSKBB24BK取扱説明書](https://www.buffalo.jp/support/download/detail/?dl_contents_id=30333)
- [MachiKania type P / phyllosoma](https://github.com/machikania/phyllosoma)
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [BTstack HID Host](https://github.com/bluekitchen/btstack/blob/master/example/hid_host_demo.c)
- [BTstack HOG Host（SDK 2.2.0同梱ソースを実装時に参照）](https://github.com/bluekitchen/btstack/blob/master/example/hog_host_demo.c)
- [LinuxのApple HID対応（Fn Usageの仕様確認）](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-apple.c)

依存ライブラリのライセンスはSDK内の各LICENSEを参照してください。ソースとUF2は実機検証前の試作品です。
