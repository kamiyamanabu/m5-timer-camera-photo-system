/**
 * @file config.example.h
 * @brief Configuration template for M5TimerCAM project
 * 
 * Copy this file to config.h and edit the values
 */

#ifndef CONFIG_H
#define CONFIG_H

// WiFi設定
#define WIFI_SSID "Your_WiFi_SSID"
#define WIFI_PASSWORD "Your_WiFi_Password"

// Supabase設定
#define SUPABASE_URL "https://your-project.supabase.co"
#define SUPABASE_SERVICE_KEY "your_service_role_key_here"
#define BUCKET_NAME "photos"

// タイマー設定
#define PHOTO_INTERVAL_HOURS 1  // 撮影間隔（時間）

// NTP設定
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC (9 * 3600)  // JST (UTC+9)
#define DAYLIGHT_OFFSET_SEC 0

#endif // CONFIG_H
