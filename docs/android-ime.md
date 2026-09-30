# Xiaomi Pad / Gboard: GLOBE が消える経路の調査

調査日: 2026-10-01。

## 結論と確定した範囲

TOTEM は GLOBE を送信している。Xiaomi Pad の Bluetooth HCI ログで、
Consumer usage `0x029D` の到着を5回確認した。
しかし Android の入力デバイスにはそのキーが登録されず、`getevent` に出ない。
Gboard に渡るより前の HID 処理で消えている。

最新の生成済みファームウェアから取り出した HID Descriptor を同じ端末で
`hid` コマンドにより再現すると、元の Consumer Usage Maximum `0x0FFF` で
同じ欠落が発生した。Usage Maximum だけを `0x02FF` に変更すると、
`KEY_KBD_LAYOUT_NEXT` が登録される。Logical Maximum だけの変更では直らない。
両方を `0x02FF` にした形式では GLOBE の押下・解放イベントも確認できた。

したがって、今回の GLOBE 欠落は **Consumer の宣言する Usage 範囲の大きさ**に
依存すると実機上で切り分けられた。BASIC 設定で送信を拒否される問題ではない。

さらに下の機構としては、カーネルの HID フィールド用メモリ確保失敗が強く疑われる。
稼働カーネルのコミットに対応する公開ソースは `kzalloc()` を使い、確保に失敗すると
そのフィールドを登録せずに継続する。広い Usage 範囲と端末稼働後のメモリ断片化で
失敗する既知の修正内容とも一致する。ただし、この実機の allocation failure の
カーネルログは取得できておらず、メモリ断片化そのものの直接確認とは区別する。

## 回避策のリバート

前の作業の差分は `Ctrl+Space` を送る `ime_toggle_android` と Adjust レイヤーへの
割り当てだった。この2点を削除して、`config/totem.keymap` が HEAD と一致することを
`git diff --exit-code -- config/totem.keymap` で確認した。

既存の `ime_toggle_hybrid` は GLOBE のタップ後に Alt+Grave を送る元の定義。
調査時点ではファームウェアの変更・書き込みは行っていない。

## 調査対象

- Xiaomi Pad: モデル `25091RP04G`、Android 16、HyperOS `OS3.0.303.0.WPYMIXM`。
- 稼働カーネル: `6.6.77-android15-8-g4a507830d890-ab13636293-4k`。
- TOTEM: HID vendor/product `1d50:615e`、ワイヤレス ADB で観測。
- 比較用ファームウェア: Actions run `35495055304`、config commit
  `f956291c32a813273c280fec74d21273aea6b6fd`、ZMK commit `9ebbeff`。
- そのビルドログ: `CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL=y`、
  `CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6`。

書き込み済みファームウェアのコミット自体は取得していない。
実機の HCI 通知の Consumer 入力は12バイトで、16ビット usage を6個持つ FULL 形式と一致する。
実機の sysfs `report_descriptor` は shell から読み出す権限がなかったため、
Descriptor 全体のバイト一致までは確認していない。

## 実キーの送信を確認した証拠

端末の bugreport に含まれる `btsnooz_hci.log` を解析した。
S+H の操作に対応して、次の順序が5回記録されている。

| ATT 通知のハンドル | 通知値の先頭3バイト | 内容 |
| --- | --- | --- |
| `0x002C` | `9D 02 00` | Consumer GLOBE 押下 |
| `0x002C` | `00 00 00` | Consumer 解放 |
| `0x0028` | `04 00 00` | Left Alt 押下 |
| `0x0028` | `04 00 35` | Alt+Grave 押下 |
| `0x0028` | `04 00 00` | Grave 解放 |
| `0x0028` | `00 00 00` | Alt 解放 |

これは ADB によるキー注入ではなく、実機から届いた BLE の受信記録。
サマリーログの ACL パケットは切り詰められているため、通知値について確認できるのは
この先頭3バイトまで。元の長さも記録されており、Consumer 入力12バイト、Keyboard 入力8バイト。

実機の `getevent` では Alt と Grave の押下・解放だけを観測した。
キーの対応一覧にも `KEY_KBD_LAYOUT_NEXT` がない。
端末の `/system/usr/keylayout/Generic.kl` には、
`key usage 0x0c029D LANGUAGE_SWITCH FALLBACK_USAGE_MAPPING` が存在する。

## 同じ端末内での Descriptor 比較

最新の左側 UF2 から176バイトの HID Descriptor を抽出し、
Android の標準 `hid` コマンドで一時的な UHID デバイスを作った。
Bluetooth サービスのキャッシュを経由せず、同じカーネルで登録・入力を比較した。
テスト用のデバイスは各比較の終了時に削除される。

| Logical Maximum | Usage Maximum | GLOBE の対応登録 | GLOBE 入力 |
| --- | --- | --- | --- |
| `0x0FFF` | `0x0FFF` | なし。再登録しても再現 | 押下・解放が出ない。通常の A キーは出る |
| `0x02FF` | `0x0FFF` | なし | 登録のみ比較 |
| `0x0FFF` | `0x02FF` | あり | 登録のみ比較 |
| `0x02FF` | `0x02FF` | あり | `MSC_SCAN 000c029d` と `KEY_KBD_LAYOUT_NEXT` の DOWN / UP |

レポート ID `2`、16ビット幅、report count `6` と送信データは同じ。
変更した Descriptor の項目は上記の最大値だけ。
再現用の Descriptor、送信レポート、比較結果は `android-ime-hid-repro.json` に保存した。

## メモリ確保問題との対応

稼働カーネルのコミット `4a507830d890` の `hid_register_field()` は、
Usage の数に応じた配列を `kzalloc()` で一括確保する。
`hid_add_field()` は確保が失敗したとき、その入力フィールドを追加せず正常扱いで継続する。
この動作なら、Keyboard と Mouse は使えて Consumer だけ消える今回の結果を説明できる。

`0x0000..0x0FFF` は4096個の Usage を宣言し、公開ソースの構造体では
フィールド用に約144 KiB、allocator の区分では256 KiBの連続領域が必要になる。
`0x0000..0x02FF` は768個で、同じ配列は約27 KiB、区分は32 KiBまで縮まる。
これは総空きメモリ量だけで決まる問題ではない。

Linux の既知の修正 `748fe4399f9194285a91ec8c09141e49a6b470b4` は、この関数を
`kvzalloc()` に変更し、端末稼働後の断片化による連続領域の確保失敗を避けるもの。
稼働カーネルの公開ソースにはこの変更が入っていない。
ただし、端末の現在の allocator 状態は権限上取得できていない。
「時間が経つと不調になる」説明としては強く整合するが、今回は Descriptor の
範囲依存を確定した段階であり、実機のメモリ確保失敗ログまで得たという意味ではない。

## 修正方針

GLOBE を残すには、Consumer の Usage Maximum を `0x02FF` 程度に制限する方針が有効。
GLOBE `0x029D` を範囲に含み、元と同じ16ビット幅のレポートを使える。
標準 ZMK の FULL は既に有効であり、`FULL=y` を追加するだけでは修正にならない。
BASIC の上限 `0x00FF` では GLOBE を送れない。

今回の調査で確認したのは端末内の UHID 入力までで、
修正版ファームウェアによる BLE 再接続後・長時間利用の検証は未実施。
GLOBE 到達を直した後も、既存 hybrid に続く Alt+Grave の Gboard への影響は別途確認する必要がある。

## 修正版のビルド構成

ZMK のフォークを使わず、このリポジトリの Actions でビルド前にパッチを適用する。

- `config/west.yml`: ZMK を直近の成功ビルドと同じ
  `9ebbeff0a8b69a42f14aec022cdf16c7a107b9e0` に固定。
- `.github/workflows/build-patched.yml`: そのコミットの公式ワークフローを元に、
  パッチ適用と生成済み UF2 の検査を追加。ビルドコンテナも直近の成功ビルドの digest に固定。
- `patches/zmk-consumer-hid-02ff.patch`: `app/include/zmk/hid.h` の Consumer 上限、
  Logical Maximum、Usage Maximum の3か所を `0x02FF` にする。
- `config/totem.conf`: GLOBE を送るために FULL / 16ビット形式を明示。
- `scripts/verify_consumer_hid.py`: 左側の UF2 に修正後の Descriptor が存在し、
  元の `0x0FFF` の Descriptor が残っていないことを検査。

パッチの適用失敗や生成物の検査失敗はビルドを失敗させる。
パッチ済みの ZMK ソースはキャッシュに保存しない。
キーマップと `ime_toggle_hybrid` の定義は変更しない。

書き込み対象は `totem_left-xiao_ble__zmk-zmk.uf2`。
HID Descriptor が変わるため、Xiaomi Pad 側でも Descriptor の再認識が必要。
単なる再接続で反映しなければ、Pad 側の登録を削除し、対応する TOTEM の Bluetooth
プロファイルで `BT_CLR` を実行して再ペアリングする。`settings_reset` の UF2 は通常の
更新には使わない。書き込み後に S+H、待機後、BLE 再接続後の入力イベントと日英切り替えを確認する。

## 参照

- [対象 Actions run](https://github.com/dwarfsawman/zmk-config-totem/actions/runs/35495055304)
- [ZMK の GLOBE 定義](https://github.com/zmkfirmware/zmk/blob/9ebbeff/app/include/dt-bindings/zmk/keys.h)
- [ZMK の HID Descriptor](https://github.com/zmkfirmware/zmk/blob/9ebbeff/app/include/zmk/hid.h)
- [ZMK の BLE HID 通知](https://github.com/zmkfirmware/zmk/blob/9ebbeff/app/src/hog.c)
- [稼働カーネルに対応する HID ソース](https://android.googlesource.com/kernel/common/+/4a507830d890/drivers/hid/hid-core.c)
- [HID のメモリ確保を変更する修正](https://android.googlesource.com/kernel/common/+/160291044c92ba826e9856b3ebd7c5c0ea12f08b)
- [Android の hid コマンド](https://android.googlesource.com/platform/frameworks/base/+/HEAD/cmds/hid/)
- [HID Descriptor の再認識に関する ZMK 設定資料](https://zmk.dev/docs/config/system)
