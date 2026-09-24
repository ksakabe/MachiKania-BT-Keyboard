# 検証記録（2026-09-24）

対象：MachiKania type P、Buffalo BSKBB24BK、Apple Wireless Keyboard A1314、BLE HOGPキーボード、ブリッジ用Raspberry Pi Pico 2 W。

## 確認済み

- Pico SDK 2.2.0、Arm GNU Toolchain 14.2.Rel1でReleaseビルド成功。
- 最終ビルドログにエラー・警告なし。
- picotoolによるUF2検査：`rp2350-arm-s`、`pico_board: pico2_w`、SDK 2.2.0。
- ホスト側のCTestに合格。SDKの実際のHIDパーサーを使用し、AddressSanitizer／UndefinedBehaviorSanitizerを有効化。
- テスト内容：Shift＋文字、重複抑止、短いレポートの拒否、未知IDの無視、押下／解放順序、サービス間のキー状態統合、6キー超のロールオーバー、キュー満杯からの復帰、切断時の解放、Bluetooth Boot Keyboard形式。
- Classic・Apple・BLEの3種類のUF2を生成。
- CTestはClassic設定・Apple設定の2件。両設定でBLEのReport IDあり／なし、短い通知の拒否、異なるHIDSインスタンスの入力統合を検証。
- Apple向けの合成ディスクリプタでFn＋Delete／矢印、Fnを先に離す操作、Option／Command、F1、JIS Usageの維持を検証。実機から採取したディスクリプタではありません。
- EWIN表示名を使った合成広告のテストを追加。UUIDなしの名前照合、大小文字、Keyboard Appearance、Service Data、無関係な機器の除外、不正長をASan／UBSan付きで検証。CTestは計3件。

## EWINの実機報告と修正

- ユーザー環境：EWIN R018 180187、macOS表示名`EWIN BT5.1 Keyboard`。
- 旧版のUARTは起動メッセージと`Scanning for BLE HID (0x1812)...`まで。LEDは点灯との報告。
- 接続成功は確認できていない。実際の広告・GATT情報も未取得。
- 更新版は名前／Appearanceでも候補を選び、受信件数・スキャンコマンド結果・接続・認証・HID探索をログ出力する。
- UUID非掲載が実際の原因かは未確定。LED更新を実時間基準に変更し、処理が継続しているか判別しやすくした。
- v2の追加実機ログ：スキャンコマンド200b／200c成功、15秒間に広告1,031件を受信。表示された名前は別機器で、接続候補はなし。受信件数には同一機器の繰り返しを含む。Ewinの広告受信・接続方式は依然未確定。
- 切り分け用にClassic版へ拡張Inquiryと機器名・Class of Device・検出件数のログを追加（`inquiry diagnostic v3`）。無線接続動作の実機確認は未実施。

## 未確認

- BSKBB24BK・A1314実機の認証方式、SDPディスクリプタとレポート、ペアリング・再接続。
- BLE機器の広告検出、ペアリング・再暗号化、GATT探索・通知、ボンド保存、RPA変更後の再接続。BLE機種は未指定。
- USBの実機列挙とMachiKania上の文字・記号・キーリピート。
- MachiKania側VBUS給電、スリープ復帰、異常終了からの復帰。
- 無線接続状態遷移とUSB制御要求については、コードとSDK APIの照合までで、ハードウェア試験・自動統合試験は未実施。

UF2の生成・テスト合格は、実機で動作確認済みであることを意味しません。実機試験はREADMEの手順に沿って行ってください。
