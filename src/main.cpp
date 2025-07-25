/**
 * @file main.cpp
 * @brief M5TimerCAM Timer Photo with Supabase Upload
 * @version 1.0
 * @date 2025-07-24
 * 
 * Based on M5Stack TimerCAM example
 */

#include "M5TimerCAM.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_sleep.h"
#include "time.h"
#include "config.h"  // 設定ファイル

// WiFi設定（config.hから読み込み）
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// NTPサーバー設定（config.hから読み込み）
const char* ntpServer = NTP_SERVER;
const long gmtOffset_sec = GMT_OFFSET_SEC;
const int daylightOffset_sec = DAYLIGHT_OFFSET_SEC;

// Supabase設定（config.hから読み込み）
const char* SUPABASE_URL_CONST = SUPABASE_URL;  
const char* SUPABASE_SERVICE_KEY_CONST = SUPABASE_SERVICE_KEY;
const char* BUCKET_NAME_CONST = BUCKET_NAME;

// タイマー撮影設定（config.hから読み込み）
const unsigned long PHOTO_INTERVAL = PHOTO_INTERVAL_HOURS * 3600000; // 時間をミリ秒に変換
unsigned long lastPhotoTime = 0;

// WiFi再接続設定
const int MAX_WIFI_RETRY = 3;
const unsigned long WIFI_RETRY_DELAY = 5000; // 5秒

// Light Sleep設定
const unsigned long LIGHT_SLEEP_DURATION = 30000000; // 30秒（マイクロ秒）

// ピン定義
#define CAMERA_LED_GPIO 2
#define EXTERNAL_BUTTON_GPIO 4  // 外部ボタン（EXT_PIN_1）- プルアップ抵抗付きで制御可能
#define POWER_BUTTON_GPIO 38  // 本体ボタン（input-only, 制御不可）

// 関数宣言
bool uploadPhotoToSupabase(uint8_t* imageData, size_t imageSize, String filename);
void takeAndUploadPhoto();
void handleShutdown();
bool connectToWiFi();
String getFormattedTimestamp();
void enterLightSleep();
void loadEnvironmentVariables();

// 設定読み込み関数（config.hから設定を読み込み）
void loadEnvironmentVariables() {
    Serial.println("[CONFIG] Loading configuration from config.h");
    Serial.print("[CONFIG] WiFi SSID: ");
    Serial.println(ssid);
    Serial.print("[CONFIG] Supabase URL: ");
    Serial.println(SUPABASE_URL_CONST);
    Serial.print("[CONFIG] Bucket Name: ");
    Serial.println(BUCKET_NAME_CONST);
    Serial.print("[CONFIG] Photo Interval: ");
    Serial.print(PHOTO_INTERVAL_HOURS);
    Serial.println(" hour(s)");
}

// 写真撮影関数
bool takePhoto() {
    // メモリチェック
    if (ESP.getFreeHeap() < 20000) {
        Serial.println("[SAFETY] Insufficient memory for camera operation");
        return false;
    }
    
    bool result = TimerCAM.Camera.get();
    
    if (result) {
        // フレームサイズの妥当性チェック
        if (TimerCAM.Camera.fb->len == 0) {
            Serial.println("[SAFETY] Invalid frame size (0 bytes)");
            TimerCAM.Camera.free();
            return false;
        }
        
        if (TimerCAM.Camera.fb->len > 500000) {
            Serial.println("[SAFETY] Frame too large, may cause memory issues");
            TimerCAM.Camera.free();
            return false;
        }
        
        // バッファの妥当性チェック
        if (TimerCAM.Camera.fb->buf == nullptr) {
            Serial.println("[SAFETY] Invalid buffer pointer");
            TimerCAM.Camera.free();
            return false;
        }
        
        Serial.print("[PHOTO] Photo captured - Size: ");
        Serial.print(TimerCAM.Camera.fb->len);
        Serial.println(" bytes");
    } else {
        Serial.println("[ERROR] Photo capture failed!");
    }
    
    return result;
}

// WiFi接続関数（エラーハンドリング付き）
bool connectToWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    
    for (int retry = 0; retry < MAX_WIFI_RETRY; retry++) {
        Serial.print("[WiFi] Connecting attempt ");
        Serial.print(retry + 1);
        Serial.print("/");
        Serial.println(MAX_WIFI_RETRY);
        
        WiFi.begin(ssid, password);
        
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
            delay(500);
            Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println();
            Serial.println("[WiFi] Connected successfully!");
            Serial.print("[WiFi] IP address: ");
            Serial.println(WiFi.localIP());
            Serial.print("[WiFi] Signal strength: ");
            Serial.print(WiFi.RSSI());
            Serial.println(" dBm");
            return true;
        } else {
            Serial.println();
            Serial.print("[WiFi] Connection failed. Status: ");
            Serial.println(WiFi.status());
            
            if (retry < MAX_WIFI_RETRY - 1) {
                Serial.print("[WiFi] Retrying in ");
                Serial.print(WIFI_RETRY_DELAY / 1000);
                Serial.println(" seconds...");
                delay(WIFI_RETRY_DELAY);
            }
        }
    }
    
    Serial.println("[WiFi] Failed to connect after all retries!");
    return false;
}

// タイムスタンプ生成関数
String getFormattedTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("[TIME] Failed to obtain time");
        // NTP取得失敗時はmillis()を使用
        return String(millis());
    }
    
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y%m%d_%H%M%S", &timeinfo);
    return String(timeStringBuff);
}

// Light Sleep関数
void enterLightSleep() {
    Serial.println("[SLEEP] Entering light sleep for 30 seconds...");
    Serial.flush();
    
    // WiFiを一時的に無効化
    WiFi.setSleep(true);
    
    // Light Sleep実行
    esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_DURATION);
    esp_light_sleep_start();
    
    Serial.println("[SLEEP] Woke up from light sleep");
    
    // WiFiを再有効化
    WiFi.setSleep(false);
}

void setup() {
    // TimerCAM初期化
    TimerCAM.begin();
    
    // 環境変数読み込み
    loadEnvironmentVariables();
    
    // リセット理由を確認
    esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.print("[SYSTEM] Reset reason: ");
    switch(reset_reason) {
        case ESP_RST_POWERON: Serial.println("Power-on reset"); break;
        case ESP_RST_EXT: Serial.println("External reset"); break;
        case ESP_RST_SW: Serial.println("Software reset"); break;
        case ESP_RST_PANIC: Serial.println("PANIC RESET!"); break;
        case ESP_RST_INT_WDT: Serial.println("WATCHDOG RESET!"); break;
        case ESP_RST_TASK_WDT: Serial.println("TASK WATCHDOG RESET!"); break;
        case ESP_RST_WDT: Serial.println("OTHER WATCHDOG RESET!"); break;
        case ESP_RST_DEEPSLEEP: Serial.println("Deep sleep reset"); break;
        case ESP_RST_BROWNOUT: Serial.println("BROWNOUT RESET!"); break;
        case ESP_RST_SDIO: Serial.println("SDIO reset"); break;
        default: Serial.println("Unknown reset"); break;
    }
    
    // メモリ情報を詳細表示
    Serial.print("[SYSTEM] Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("[SYSTEM] Largest free block: ");
    Serial.println(ESP.getMaxAllocHeap());
    Serial.print("[SYSTEM] Total heap: ");
    Serial.println(ESP.getHeapSize());
    
    // LED設定
    pinMode(CAMERA_LED_GPIO, OUTPUT);
    digitalWrite(CAMERA_LED_GPIO, HIGH); // 起動時LED点灯
    
    // 外部ボタン設定（GPIO 4 - 制御可能）
    pinMode(EXTERNAL_BUTTON_GPIO, INPUT_PULLUP);
    
    // 本体ボタン設定（GPIO 38 - input-only, 制御不可）
    pinMode(POWER_BUTTON_GPIO, INPUT);  // プルアップなし
    
    // 起動時のピン状態を確認
    Serial.print("EXTERNAL_BUTTON_GPIO (4) initial state: ");
    Serial.println(digitalRead(EXTERNAL_BUTTON_GPIO) ? "HIGH (Released)" : "LOW (Pressed)");
    Serial.print("POWER_BUTTON_GPIO (38) initial state: ");
    Serial.println(digitalRead(POWER_BUTTON_GPIO) ? "HIGH" : "LOW");
    
    // もしボタンが押されている場合は警告
    if (digitalRead(EXTERNAL_BUTTON_GPIO) == LOW) {
        Serial.println("INFO: External button (GPIO 4) is pressed at startup!");
    }
    
    Serial.println("NOTE: M5TimerCAM has no physical power button.");
    Serial.println("Use external button on GPIO 4 for power control.");
    
    Serial.println("M5TimerCAM Timer Photo Starting...");

    // カメラ初期化
    if (!TimerCAM.Camera.begin()) {
        Serial.println("Camera Init Fail");
        // エラー時はLED点滅
        for(int i = 0; i < 10; i++) {
            digitalWrite(CAMERA_LED_GPIO, !digitalRead(CAMERA_LED_GPIO));
            delay(200);
        }
        return;
    }
    Serial.println("Camera Init Success");

    // カメラ設定（タイマー撮影用に最適化）
    TimerCAM.Camera.sensor->set_pixformat(TimerCAM.Camera.sensor, PIXFORMAT_JPEG);
    TimerCAM.Camera.sensor->set_framesize(TimerCAM.Camera.sensor, FRAMESIZE_VGA); // VGAサイズに変更（軽量化）
    TimerCAM.Camera.sensor->set_vflip(TimerCAM.Camera.sensor, 1);
    TimerCAM.Camera.sensor->set_hmirror(TimerCAM.Camera.sensor, 0);
    TimerCAM.Camera.sensor->set_quality(TimerCAM.Camera.sensor, 12); // 品質設定（10-63, 低いほど高品質）

    // WiFi接続
    if (!connectToWiFi()) {
        Serial.println("[ERROR] WiFi connection failed! System will continue without network.");
        // WiFi接続失敗でもカメラ機能は使用可能
    } else {
        // NTP時刻同期
        Serial.println("[TIME] Configuring time...");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        
        // 時刻同期完了まで待機
        struct tm timeinfo;
        int timeoutCount = 0;
        while (!getLocalTime(&timeinfo) && timeoutCount < 10) {
            Serial.println("[TIME] Waiting for time sync...");
            delay(1000);
            timeoutCount++;
        }
        
        if (timeoutCount < 10) {
            Serial.println("[TIME] Time synchronized successfully");
            Serial.println(getFormattedTimestamp());
        } else {
            Serial.println("[TIME] Time sync failed, using millis() timestamps");
        }
    }
    
    // WiFi接続成功でLED 3回点滅
    digitalWrite(CAMERA_LED_GPIO, LOW);
    for(int i = 0; i < 3; i++) {
        digitalWrite(CAMERA_LED_GPIO, HIGH);
        delay(300);
        digitalWrite(CAMERA_LED_GPIO, LOW);
        delay(300);
    }

    Serial.println("Timer photo system ready!");
    Serial.println("Photos will be taken every 1 hour and uploaded to Supabase.");
    Serial.println("System features:");
    Serial.println("  - Auto WiFi reconnection");
    Serial.println("  - Light sleep power saving");
    Serial.println("  - Timestamp-based filenames");
    Serial.println("  - Environment variable support");
    Serial.println("Supabase configuration:");
    Serial.print("  URL: ");
    Serial.println(SUPABASE_URL_CONST);
    Serial.print("  Bucket: ");
    Serial.println(BUCKET_NAME_CONST);
    Serial.println("Configuration:");
    Serial.println("  Edit src/config.h to change settings");
    Serial.println("Environment variables:");
    Serial.println("  SUPABASE_SERVICE_KEY - Set your Supabase Service Role Key");
    
    // 初回撮影時刻を設定
    lastPhotoTime = millis();
    
    // Deep sleepからの復帰処理
    if (reset_reason == ESP_RST_DEEPSLEEP) {
        Serial.println("[WAKEUP] System woke up from deep sleep");
        // ボタンで起動した場合の処理
        if (digitalRead(EXTERNAL_BUTTON_GPIO) == LOW) {
            Serial.println("[WAKEUP] External button pressed - system ready");
        }
    }
    
    // Deep sleep用のボタン割り込み設定
    esp_sleep_enable_ext0_wakeup((gpio_num_t)EXTERNAL_BUTTON_GPIO, 0); // GPIO 4がLOWで起動
}

void loop() {
    // 外部ボタン監視（GPIO 4）
    static unsigned long buttonPressTime = 0;
    static bool buttonPressed = false;
    
    // 外部ボタン（GPIO 4）の処理
    if (digitalRead(EXTERNAL_BUTTON_GPIO) == LOW) {
        if (!buttonPressed) {
            buttonPressed = true;
            buttonPressTime = millis();
            Serial.println("[BUTTON] External button pressed...");
            
            // LED点滅で押下を知らせる
            digitalWrite(CAMERA_LED_GPIO, HIGH);
            delay(100);
            digitalWrite(CAMERA_LED_GPIO, LOW);
        } else if (millis() - buttonPressTime > 3000) {
            Serial.println("[BUTTON] External button long pressed (3+ seconds) - entering deep sleep!");
            
            // LED 3回点滅でdeep sleep予告
            for(int i = 0; i < 3; i++) {
                digitalWrite(CAMERA_LED_GPIO, HIGH);
                delay(200);
                digitalWrite(CAMERA_LED_GPIO, LOW);
                delay(200);
            }
            
            handleShutdown();
            return; // 関数を終了してシャットダウン処理を確実に実行
        }
    } else {
        if (buttonPressed) {
            unsigned long pressDuration = millis() - buttonPressTime;
            Serial.print("[BUTTON] External button released after ");
            Serial.print(pressDuration);
            Serial.println(" ms");
            
            if (pressDuration < 3000) {
                Serial.println("[BUTTON] Short press detected - taking photo now!");
                // 短押しで即座に撮影
                takeAndUploadPhoto();
            }
        }
        buttonPressed = false;
    }
    
    // タイマー撮影処理（1時間ごと）
    unsigned long timeSinceLastPhoto = millis() - lastPhotoTime;
    if (timeSinceLastPhoto >= PHOTO_INTERVAL) {
        takeAndUploadPhoto();
        lastPhotoTime = millis();
    } else {
        // 次の撮影まで十分時間がある場合はLight sleep
        unsigned long timeToNextPhoto = PHOTO_INTERVAL - timeSinceLastPhoto;
        if (timeToNextPhoto > 60000) { // 1分以上ある場合
            enterLightSleep();
        }
    }
    
    // システム状態表示（5分ごと）
    static unsigned long lastSystemDebugTime = 0;
    if (millis() - lastSystemDebugTime > 300000) { // 5分 = 300,000ms
        unsigned long nextPhotoIn = (PHOTO_INTERVAL - (millis() - lastPhotoTime)) / 1000;
        Serial.print("[SYSTEM] Uptime: ");
        Serial.print(millis() / 1000 / 60);
        Serial.print(" min, Free heap: ");
        Serial.print(ESP.getFreeHeap());
        Serial.print(" bytes, Next photo in: ");
        Serial.print(nextPhotoIn / 60);
        Serial.print(":");
        if (nextPhotoIn % 60 < 10) Serial.print("0");
        Serial.print(nextPhotoIn % 60);
        Serial.println(" (mm:ss)");
        
        // WiFi状態確認
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("[WiFi] Connected, RSSI: ");
            Serial.print(WiFi.RSSI());
            Serial.println(" dBm");
        } else {
            Serial.print("[WiFi] Disconnected, Status: ");
            Serial.println(WiFi.status());
        }
        
        lastSystemDebugTime = millis();
    }
    
    delay(100); // CPU負荷軽減
}

// 写真撮影とアップロード処理
void takeAndUploadPhoto() {
    digitalWrite(CAMERA_LED_GPIO, HIGH); // LED点灯で撮影開始を知らせる
    
    Serial.println("[PHOTO] Taking photo...");
    
    if (takePhoto()) {
        // WiFi接続確認・再接続
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Connection lost, attempting to reconnect...");
            if (!connectToWiFi()) {
                Serial.println("[ERROR] WiFi reconnection failed! Photo saved locally only.");
                // WiFi接続失敗時はLED 3回点滅
                digitalWrite(CAMERA_LED_GPIO, LOW);
                for(int i = 0; i < 3; i++) {
                    digitalWrite(CAMERA_LED_GPIO, HIGH);
                    delay(100);
                    digitalWrite(CAMERA_LED_GPIO, LOW);
                    delay(100);
                }
                TimerCAM.Camera.free();
                return;
            }
        }
        
        // タイムスタンプ付きファイル名生成
        String timestamp = getFormattedTimestamp();
        String filename = "photo_" + timestamp + ".jpg";
        
        Serial.print("[PHOTO] Generated filename: ");
        Serial.println(filename);
        
        // Supabaseにアップロード
        if (uploadPhotoToSupabase(TimerCAM.Camera.fb->buf, TimerCAM.Camera.fb->len, filename)) {
            Serial.println("[UPLOAD] Photo uploaded successfully!");
            
            // 成功時はLED 2回点滅
            digitalWrite(CAMERA_LED_GPIO, LOW);
            for(int i = 0; i < 2; i++) {
                digitalWrite(CAMERA_LED_GPIO, HIGH);
                delay(150);
                digitalWrite(CAMERA_LED_GPIO, LOW);
                delay(150);
            }
        } else {
            Serial.println("[UPLOAD] Photo upload failed!");
            
            // 失敗時はLED 5回高速点滅
            for(int i = 0; i < 5; i++) {
                digitalWrite(CAMERA_LED_GPIO, HIGH);
                delay(100);
                digitalWrite(CAMERA_LED_GPIO, LOW);
                delay(100);
            }
        }
        
        // メモリクリーンアップ
        TimerCAM.Camera.free();
    } else {
        Serial.println("[PHOTO] Photo capture failed!");
        digitalWrite(CAMERA_LED_GPIO, LOW);
    }
}

// SupabaseストレージにアップロードするHTTPS関数（エラーハンドリング強化）
bool uploadPhotoToSupabase(uint8_t* imageData, size_t imageSize, String filename) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[UPLOAD] WiFi not connected!");
        return false;
    }
    
    if (imageData == nullptr || imageSize == 0) {
        Serial.println("[UPLOAD] Invalid image data!");
        return false;
    }
    
    Serial.print("[UPLOAD] Uploading ");
    Serial.print(imageSize);
    Serial.print(" bytes to Supabase as ");
    Serial.println(filename);
    
    // WiFiClientSecureを使用（HTTPS）
    WiFiClientSecure client;
    
    // SSL証明書の検証を無効化（簡単な実装のため）
    client.setInsecure();
    
    // 接続タイムアウト設定
    client.setTimeout(30000); // 30秒
    
    // Supabaseのホスト名を抽出
    String host = String(SUPABASE_URL_CONST);
    host.replace("https://", "");
    host.replace("http://", "");
    
    Serial.print("[UPLOAD] Connecting to host: ");
    Serial.println(host);
    
    // 接続リトライ機能
    int connectionRetries = 3;
    bool connected = false;
    
    for (int i = 0; i < connectionRetries && !connected; i++) {
        Serial.print("[UPLOAD] Connection attempt ");
        Serial.print(i + 1);
        Serial.print("/");
        Serial.println(connectionRetries);
        
        if (client.connect(host.c_str(), 443)) {
            connected = true;
            Serial.println("[UPLOAD] Connected to Supabase");
        } else {
            Serial.print("[UPLOAD] Connection failed, attempt ");
            Serial.println(i + 1);
            if (i < connectionRetries - 1) {
                delay(2000); // 2秒待機してリトライ
            }
        }
    }
    
    if (!connected) {
        Serial.println("[UPLOAD] Failed to connect after all retries!");
        return false;
    }
    
    // パス生成
    String path = "/storage/v1/object/" + String(BUCKET_NAME_CONST) + "/" + filename;
    Serial.print("[UPLOAD] Upload path: ");
    Serial.println(path);
    
    // HTTPヘッダー作成（認証付き）
    String httpRequest = "POST " + path + " HTTP/1.1\r\n";
    httpRequest += "Host: " + host + "\r\n";
    httpRequest += "Authorization: Bearer " + String(SUPABASE_SERVICE_KEY_CONST) + "\r\n";
    httpRequest += "Content-Type: image/jpeg\r\n";
    httpRequest += "Content-Length: " + String(imageSize) + "\r\n";
    httpRequest += "x-upsert: true\r\n";
    httpRequest += "Connection: close\r\n\r\n";
    
    Serial.println("[UPLOAD] Sending headers...");
    
    // ヘッダー送信
    client.print(httpRequest);
    
    Serial.println("[UPLOAD] Sending image data...");
    
    // 画像データを分割して送信（大きなデータの安全な送信）
    const size_t chunkSize = 1024; // 1KBずつ送信
    size_t totalSent = 0;
    
    while (totalSent < imageSize) {
        size_t currentChunkSize = min(chunkSize, imageSize - totalSent);
        size_t bytesWritten = client.write(imageData + totalSent, currentChunkSize);
        
        if (bytesWritten != currentChunkSize) {
            Serial.print("[UPLOAD] Write error at byte ");
            Serial.print(totalSent);
            Serial.print(": ");
            Serial.print(bytesWritten);
            Serial.print("/");
            Serial.println(currentChunkSize);
            client.stop();
            return false;
        }
        
        totalSent += bytesWritten;
        
        // 進捗表示（10%ごと）
        if ((totalSent * 10 / imageSize) != ((totalSent - bytesWritten) * 10 / imageSize)) {
            Serial.print("[UPLOAD] Progress: ");
            Serial.print(totalSent * 100 / imageSize);
            Serial.println("%");
        }
        
        delay(1); // 少し待機してESP32の負荷を軽減
    }
    
    Serial.println("[UPLOAD] Data sent, waiting for response...");
    
    // レスポンス読み取り
    unsigned long timeout = millis();
    while (client.available() == 0) {
        if (millis() - timeout > 15000) { // 15秒タイムアウト
            Serial.println("[UPLOAD] Response timeout!");
            client.stop();
            return false;
        }
        delay(10);
    }
    
    // HTTPステータスコード確認
    bool success = false;
    String statusLine = "";
    
    while (client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        
        if (line.startsWith("HTTP/1.1")) {
            statusLine = line;
            Serial.print("[UPLOAD] HTTP Response: ");
            Serial.println(line);
            
            if (line.indexOf("200") > 0 || line.indexOf("201") > 0) {
                success = true;
            }
        }
        
        // 空行でヘッダー終了
        if (line.length() == 0) {
            break;
        }
    }
    
    // レスポンスボディも読み取り（エラー情報のため）
    if (!success && client.available()) {
        Serial.println("[UPLOAD] Error response body:");
        while (client.available()) {
            String line = client.readStringUntil('\n');
            Serial.println(line);
            if (line.length() > 100) break; // 長すぎる場合は途中で止める
        }
    }
    
    client.stop();
    
    if (success) {
        Serial.print("[UPLOAD] Successfully uploaded: ");
        Serial.println(filename);
    } else {
        Serial.println("[UPLOAD] Upload failed - check Supabase configuration");
    }
    
    return success;
}

void handleShutdown() {
    Serial.println("[SHUTDOWN] Entering deep sleep mode...");
    
    // システム情報表示
    Serial.print("[DEBUG] Free heap before shutdown: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("[DEBUG] Uptime before shutdown: ");
    Serial.print(millis() / 1000);
    Serial.println(" seconds");
    
    // LED 5回点滅でdeep sleep予告
    Serial.println("[SHUTDOWN] LED signaling deep sleep...");
    for(int i = 0; i < 5; i++) {
        digitalWrite(CAMERA_LED_GPIO, HIGH);
        delay(200);
        digitalWrite(CAMERA_LED_GPIO, LOW);
        delay(200);
    }
    
    // システムクリーンアップ
    Serial.println("[SHUTDOWN] Cleaning up system...");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    TimerCAM.Camera.deinit();
    
    Serial.println("[SHUTDOWN] System cleanup complete");
    Serial.flush(); // シリアル出力を確実に送信
    
    delay(1000); // 1秒待機
    
    // Deep sleep設定
    Serial.println("[SHUTDOWN] Configuring deep sleep wake-up...");
    esp_sleep_enable_ext0_wakeup((gpio_num_t)EXTERNAL_BUTTON_GPIO, 0); // GPIO 4がLOWで起動
    
    Serial.println("[SHUTDOWN] Entering deep sleep... Press external button (GPIO 4) to wake up.");
    Serial.flush();
    
    delay(500); // 確実にメッセージを送信
    
    // Deep sleepに入る
    esp_deep_sleep_start();
}
