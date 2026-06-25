#include <WiFi.h>
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// 定義資料結構，使用 char 陣列避免 String 造成記憶體碎片
typedef struct {
    char ssid[33];
    int rssi;
} WifiData_t;

QueueHandle_t wifiQueue;

// ==========================================
// 核心 0 任務：搜尋 WiFi 訊號
// ==========================================
void ScannerTask(void *pvParameters) {
    for (;;) {
        // 掃描周圍 WiFi
        int n = WiFi.scanNetworks();
        for (int i = 0; i < n; ++i) {
            WifiData_t data;
            strncpy(data.ssid, WiFi.SSID(i).c_str(), sizeof(data.ssid) - 1);
            data.ssid[sizeof(data.ssid) - 1] = '\0'; // 確保字串結尾
            data.rssi = WiFi.RSSI(i);
            
            // 將資料送入佇列
            xQueueSend(wifiQueue, &data, portMAX_DELAY);
        }
        WiFi.scanDelete();
        vTaskDelay(3000 / portTICK_PERIOD_MS); // 每 3 秒掃描一次
    }
}

// ==========================================
// 核心 1 任務：計算訊號強弱並透過 USB 輸出
// ==========================================
void ProcessorTask(void *pvParameters) {
    WifiData_t data;
    for (;;) {
        // 從佇列接收資料
        if (xQueueReceive(wifiQueue, &data, portMAX_DELAY) == pdTRUE) {
            // 計算訊號強弱 (將 -100~ -50 dBm 映射為 0~100%)
            int quality = 0;
            if (data.rssi <= -100) quality = 0;
            else if (data.rssi >= -50) quality = 100;
            else quality = 2 * (data.rssi + 100);

            // 透過 USB 串口 (COM5) 輸出 JSON 格式
            Serial.print("{\"ssid\":\"");
            Serial.print(data.ssid);
            Serial.print("\",\"rssi\":");
            Serial.print(data.rssi);
            Serial.print(",\"quality\":");
            Serial.print(quality);
            Serial.println("}");
        }
    }
}

void setup() {
    // 初始化 USB 串口 (確保在 PlatformIO/Arduino 設定中啟用 USB CDC On Boot)
    Serial.begin(115200);
    
    // 等待 USB 串口就緒 (最多等待 3 秒，避免未接 USB 時死當)
    unsigned long start = millis();
    while (!Serial && millis() - start < 3000) { 
        delay(100); 
    }

    // 初始化 WiFi 為 Station 模式
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // 建立 FreeRTOS 佇列 (最多容納 20 筆資料)
    wifiQueue = xQueueCreate(20, sizeof(WifiData_t));

    // 將任務固定綁定到不同核心
    // Core 0: 負責掃描
    xTaskCreatePinnedToCore(ScannerTask, "ScannerTask", 4096, NULL, 1, NULL, 0);
    // Core 1: 負責計算與輸出
    xTaskCreatePinnedToCore(ProcessorTask, "ProcessorTask", 4096, NULL, 1, NULL, 1);
}

void loop() {
    // 主迴圈留空，所有工作由 FreeRTOS 任務處理
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}
