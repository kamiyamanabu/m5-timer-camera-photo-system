# M5TimerCAM Timer Photo System

ESP32ベースのM5TimerCAMを使用した自動タイマー撮影システムです。撮影した写真をSupabase Storageに自動アップロードします。

## 特徴

- ⏰ **1時間間隔の自動撮影** (設定可能)
- 🔋 **Light Sleep電力管理** (バッテリー寿命最適化)
- 📡 **WiFi自動再接続** (ネットワーク障害時の自動復旧)
- ☁️ **Supabase Storage連携** (クラウド自動アップロード)
- 🕐 **NTPタイムスタンプ** (正確な時刻付きファイル名)
- 🔘 **外部ボタン制御** (手動撮影・電源管理)
- ⚡ **包括的エラーハンドリング** (堅牢な動作)

## ハードウェア要件

- **M5Stack Timer CAM** (ESP32ベース)
- **外部ボタン** (GPIO 4接続、プルアップ抵抗付き)
- **WiFi接続環境**

## セットアップ

### 1. 環境準備

```bash
# PlatformIOのインストール
pip install platformio

# プロジェクトクローン
git clone https://github.com/kamiyamanabu/m5-timer-camera-photo-system.git
cd m5-timer-camera-photo-system
```

### 2. 設定ファイル作成

```bash
# 設定テンプレートをコピー
cp src/config.example.h src/config.h
```

### 3. config.h を編集

```cpp
// WiFi設定
#define WIFI_SSID "Your_WiFi_SSID"
#define WIFI_PASSWORD "Your_WiFi_Password"

// Supabase設定
#define SUPABASE_URL "https://your-project.supabase.co"
#define SUPABASE_SERVICE_KEY "your_service_role_key_here"
#define BUCKET_NAME "photos"

// タイマー設定
#define PHOTO_INTERVAL_HOURS 1  // 撮影間隔（時間）
```

### 4. ビルド・アップロード

```bash
# ビルド
pio run

# M5TimerCAMにアップロード
pio run --target upload

# シリアルモニター
pio device monitor --baud 115200
```

## Supabase設定

### 1. プロジェクト作成
- [Supabase Dashboard](https://supabase.com/dashboard)でプロジェクト作成

### 2. Storage設定
```sql
-- photosバケット作成
INSERT INTO storage.buckets (id, name, public)
VALUES ('photos', 'photos', true);

-- アップロード権限設定
CREATE POLICY "Allow uploads" ON storage.objects
FOR INSERT WITH CHECK (bucket_id = 'photos');
```

### 3. Service Role Key取得
- Settings → API → service_role key をコピー
- `config.h`の`SUPABASE_SERVICE_KEY`に設定

## 使用方法

### 自動撮影
- システム起動後、設定した間隔で自動撮影
- 撮影した写真は自動的にSupabaseにアップロード

### 手動撮影
- **外部ボタン短押し** (1秒未満): 即座に撮影
- **外部ボタン長押し** (3秒以上): Deep Sleep移行

### LED表示
- **点灯**: 撮影中
- **2回点滅**: アップロード成功
- **3回点滅**: WiFi接続失敗
- **5回点滅**: アップロード失敗

## 電力管理

- **Light Sleep**: 30秒間隔で自動実行
- **Deep Sleep**: 長押しで手動移行
- **自動復帰**: 外部ボタンでWake Up

## システム監視

5分ごとにシリアルコンソールに以下を表示:
- 稼働時間
- 空きメモリ
- 次回撮影までの時間
- WiFi接続状態

## トラブルシューティング

### WiFi接続失敗
- SSID・パスワードを確認
- WiFi信号強度を確認
- 2.4GHz帯であることを確認

### アップロード失敗
- Supabase Service Keyを確認
- バケット権限を確認
- インターネット接続を確認

### カメラエラー
- M5TimerCAMの接続を確認
- メモリ不足の場合は再起動

## 開発情報

### プロジェクト構造
```
m5-timer-camera-photo-system/
├── src/
│   ├── main.cpp           # メインプログラム
│   ├── config.h           # 設定ファイル (git除外)
│   └── config.example.h   # 設定テンプレート
├── platformio.ini         # PlatformIO設定
└── README.md             # このファイル
```

### 技術仕様
- **プラットフォーム**: ESP32 (M5Stack Timer CAM)
- **フレームワーク**: Arduino
- **WiFi**: WPA2/WPA3対応
- **画像形式**: JPEG
- **解像度**: VGA (640x480)
- **ストレージ**: Supabase Storage

## ライセンス

MIT License

## 貢献

Issue・Pull Requestを歓迎します。

## 作者

- GitHub: [@kamiyamanabu](https://github.com/kamiyamanabu)

---

**注意**: `src/config.h`には機密情報が含まれるため、Gitにコミットしないよう注意してください。
