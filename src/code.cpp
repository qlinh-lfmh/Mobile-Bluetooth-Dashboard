#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <AudioFileSourcePROGMEM.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "arial18.h" // File font Vietnamese

// Define PIN and time
#define BUTTON_PIN 21
#define CLEAR_BUTTON 22
#define DEBOUNCE_TIME 50
#define LONG_PRESS_TIME 800 // 0.8s for changing mode
#define READ_ONLY_TIME 2000 // 2s for enable/disable Read−only mode

// Define for I2S
#define I2S_DOUT 27
#define I2S_BCLK 26
#define I2S_LRC 25
#define BUTTON_SPEAK_PIN 32

// Define UART for HC−05
#define RX2 16
#define TX2 17

// Initialize bluetooth, speaker, TFT
AudioOutputI2S *out;
HardwareSerial SerialBT(2); // Select UART 2
TFT_eSPI tft = TFT_eSPI();
Preferences pref;
TFT_eSprite sprite = TFT_eSprite(&tft);

// wifi's information
const char* ssid = "wifi_name";
const char* password = "wifi_password";

// State variable
String activeMsg = "Không có tin nhắn";
int messageCount = 0;
int currentMsgIdx = 0;
int currentMode = 0;
int xPos = 0;
int msWidth = 0;
int speed = 3;

// Button control variables
unsigned long buttonPressedTime = 0;
bool lastBtnState = HIGH;
bool isLongPressHandled = false;
bool readOnlyMode = false;
bool isProcessingBT = false; // Bluetooth spam protection

bool isNumber(String s) {
    if (s.length() == 0) return false;
    for (int i = 0; i < s.length(); i++) {
        if (!isDigit(s[i])) return false;
    }
    return true;
}

String getModeName() {
    if (readOnlyMode) return "READ-ONLY MODE (R-O)";
    switch (currentMode) {
        case 0: return "STATIC";
        case 1: return "R-to-L";
        case 2: return "L-to-R";
        case 3: return "BLINK";
        default: return "NONE";
    }
}

void updateUI() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    
    // Drawing Header
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("MSG: " + String(currentMsgIdx) + "/" + String(messageCount), 5, 5, 0);
    
    // Drawing Footer
    tft.setTextColor(readOnlyMode ? TFT_RED : TFT_MAGENTA, TFT_BLACK);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("MODE: " + getModeName(), 5, tft.height() - 5, 0);
    
    // Accurately calculate message width using Arial16 font (Font index = 0)
    msWidth = tft.textWidth(activeMsg, 0);
    
    // Reset text position at startup
    if (currentMode == 1) xPos = tft.width();
    else if (currentMode == 2) xPos = -msWidth;
    else xPos = (tft.width() - msWidth) / 2;
}

void loadMessage(int idx) {
    if (idx < 1 || idx > messageCount) return;
    pref.begin("flashMessages", true);
    activeMsg = pref.getString(String(idx).c_str(), "Trống");
    pref.end();
    currentMsgIdx = idx;
    updateUI();
}

void clearFlash() {
    pref.begin("flashMessages", false);
    pref.clear();
    pref.putInt("msgCount", 0);
    pref.end();
    messageCount = 0;
    currentMsgIdx = 0;
    activeMsg = "Đã xóa bộ nhớ";
    updateUI();
    delay(500);
}

void handleButton() {
    bool clearButtonState = digitalRead(CLEAR_BUTTON);
    bool btnState = digitalRead(BUTTON_PIN);
    unsigned long holdDuration = 0;
    
    // Clear message's memory
    if (!clearButtonState) {
        clearFlash();
        return;
    }
    
    // Pressing button
    if (lastBtnState == HIGH && btnState == LOW) {
        buttonPressedTime = millis();
        isLongPressHandled = false;
    }
    
    // Checking hold button
    if (btnState == LOW && !isLongPressHandled) {
        holdDuration = millis() - buttonPressedTime;
        // Checking hold duration is enough for R−O mode
        if (holdDuration >= READ_ONLY_TIME) {
            readOnlyMode = !readOnlyMode;
            isLongPressHandled = true;
            updateUI();
        }
    }
    
    // Release button
    if (lastBtnState == LOW && btnState == HIGH) {
        holdDuration = millis() - buttonPressedTime;
        if (!isLongPressHandled) {
            // Checking hold duration is enough for text animation
            if (holdDuration >= LONG_PRESS_TIME) {
                currentMode = (currentMode + 1) % 4;
                updateUI();
            }
            // Displaying old messages
            else if (holdDuration >= DEBOUNCE_TIME) {
                if (messageCount > 0) {
                    currentMsgIdx--;
                    if (currentMsgIdx < 1)
                        currentMsgIdx = messageCount;
                    loadMessage(currentMsgIdx);
                }
            }
        }
        isLongPressHandled = true;
    }
    lastBtnState = btnState;
}

void handleSpeakRequest() {
    static bool lastSpeakBtnState = HIGH;
    bool btnState = digitalRead(BUTTON_SPEAK_PIN);
    if (lastSpeakBtnState == HIGH && btnState == LOW) {
        if (activeMsg != "" && activeMsg != "Không có tin nhắn") {
            speakVietnamese(activeMsg);
        } else {
            speakVietnamese("Không có tin nhắn để đọc");
        }
        delay(200);
    }
    lastSpeakBtnState = btnState;
}

void handleAnimation() {
    sprite.fillSprite(TFT_BLACK);
    sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
    sprite.setTextSize(1);
    int fontIdx = 0;
    
    switch (currentMode) {
        case 0: // Static (Central)
            sprite.setTextDatum(MC_DATUM);
            sprite.drawString(activeMsg, tft.width() / 2, sprite.height() / 2, fontIdx);
            break;
        case 1: // Right to Left
            sprite.setTextDatum(TL_DATUM);
            sprite.drawString(activeMsg, xPos, 10, fontIdx);
            xPos -= speed;
            if (xPos < -msWidth) {
                xPos = tft.width();
            }
            break;
        case 2: // Left to Right
            sprite.setTextDatum(TL_DATUM);
            sprite.drawString(activeMsg, xPos, 10, fontIdx);
            xPos += speed;
            if (xPos > tft.width()) {
                xPos = -msWidth;
            }
            break;
        case 3: // Blink
            if ((millis() / 500) % 2 == 0) {
                sprite.setTextDatum(MC_DATUM);
                sprite.drawString(activeMsg, tft.width() / 2, sprite.height() / 2, fontIdx);
            }
            break;
    }
    sprite.pushSprite(0, (tft.height() / 2) - 20);
}

String urlEncode(String str) {
    String encodedString = "";
    char c;
    char code0;
    char code1;
    for (int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (c == ' ') {
            encodedString += '+';
        } else if (isalnum(c)) {
            encodedString += c;
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) {
                code1 = (c & 0xf) - 10 + 'A';
            }
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9) {
                code0 = c - 10 + 'A';
            }
            encodedString += '%';
            encodedString += code0;
            encodedString += code1;
        }
    }
    return encodedString;
}

void speakVietnamese(String text) {
    Serial.printf("\nĐọc: %s\n", text.c_str());
    // Google TTS limit ~200 char
    if (text.length() > 150) {
        Serial.println(" Text dài, chia nhỏ...");
        int cutPos = 150;
        while (cutPos > 50 && text.charAt(cutPos) != ' ' && text.charAt(cutPos) != ',') {
            cutPos--;
        }
        String part1 = text.substring(0, cutPos);
        String part2 = text.substring(cutPos + 1);
        speakVietnamese(part1);
        delay(200);
        speakVietnamese(part2);
        return;
    }
    String encodedText = urlEncode(text);
    Serial.printf("Encode text: ", encodedText);
    String url = "http://translate.google.com/translate_tts?ie=UTF-8&tl=vi&client=tw-ob&q=" + encodedText;
    
    // ===== DOWNLOAD MP3 =====
    HTTPClient http;
    http.setTimeout(10000);
    http.begin(url);
    http.addHeader("User-Agent", "Mozilla/5.0");
    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf(" HTTP error: %d\n", httpCode);
        http.end();
        return;
    }
    
    const size_t MAX_BUFFER = 60000; // 60KB max
    // ===== CHECKING RAM BEFORE MALLOC =====
    if (ESP.getFreeHeap() < MAX_BUFFER + 10000) {
        Serial.printf(" Không đủ RAM! Cần %d, có %d\n", MAX_BUFFER + 10000, ESP.getFreeHeap());
        http.end();
        return;
    }
    
    // Allocate buffer
    uint8_t* buffer = (uint8_t)malloc(MAX_BUFFER);
    if (!buffer) {
        Serial.println(" malloc failed!");
        http.end();
        return;
    }
    
    // Download with timeout
    WiFiClient* stream = http.getStreamPtr();
    size_t downloaded = 0;
    unsigned long startTime = millis();
    unsigned long lastDataTime = millis();
    
    while (http.connected() && downloaded < MAX_BUFFER) {
        size_t available = stream->available();
        if (available) {
            int toRead = min(available, MAX_BUFFER - downloaded);
            int bytesRead = stream->readBytes(buffer + downloaded, toRead);
            downloaded += bytesRead;
            lastDataTime = millis(); // Reset timeout when have data
        }
        // Timeout: 2s without new data −> done
        if (millis() - lastDataTime > 2000) {
            Serial.println(" Download xong!");
            break;
        }
        // Total timeout: 10s
        if (millis() - startTime > 10000) {
            Serial.println(" Download timeout!");
            break;
        }
        yield();
        delay(1);
    }
    http.end();
    
    if (downloaded < 100) {
        Serial.println(" File quá nhỏ, bỏ qua");
        free(buffer);
        return;
    }
    
    // ===== PLAY FROM BUFFER =====
    AudioFileSourcePROGMEM *audioSrc = new AudioFileSourcePROGMEM(buffer, downloaded);
    AudioGeneratorMP3 *player = new AudioGeneratorMP3();
    if (!player->begin(audioSrc, out)) {
        Serial.println(" MP3 begin failed!");
        delete audioSrc;
        delete player;
        free(buffer);
        return;
    }
    
    Serial.println("Đang phát...");
    
    // Play with timeout
    unsigned long playStart = millis();
    while (player->isRunning()) {
        if (!player->loop()) {
            break;
        }
        // Timeout playing: 15s
        if (millis() - playStart > 15000) {
            Serial.println("Play timeout!");
            break;
        }
        yield();
    }
    
    // Cleanup
    player->stop();
    delete audioSrc;
    delete player;
    free(buffer);
    Serial.println("Phát xong!");
    delay(10);
}

void setup() {
    Serial.begin(115200);
    SerialBT.begin(9600, SERIAL_8N1, RX2, TX2);
    pinMode(CLEAR_BUTTON, INPUT_PULLUP);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(BUTTON_SPEAK_PIN, INPUT_PULLUP);
    
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    
    // ========== LOADING FONT ARRAY INTO TFT & SPRITE ==========
    tft.loadFont(arial18);
    sprite.loadFont(arial18);
    sprite.setColorDepth(16);
    sprite.createSprite(tft.width(), 40);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Đang kết nối WiFi...", tft.width()/2, tft.height()/2, 0);
    
    pref.begin("flashMessages", true);
    messageCount = pref.getInt("msgCount", 0);
    pref.end();
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(ssid, password);
    
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - wifiStart > 15000) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("Lỗi kết nối WiFi!", tft.width()/2, tft.height()/2, 0);
            delay(1500);
            break;
        }
        delay(200);
    }
    
    out = new AudioOutputI2S();
    out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    
    if (messageCount > 0) loadMessage(messageCount);
    else updateUI();
    
    if (WiFi.status() == WL_CONNECTED) {
        speakVietnamese("Thiết bị sẵn sàng nhận tin nhắn");
    }
    Serial.println("Hệ thống sẵn sàng");
}

void loop() {
    handleButton();
    handleSpeakRequest();
    
    // Processing Bluetooth's data (prevent spam + support Vietnamese text clean)
    if (SerialBT.available() && !isProcessingBT) {
        isProcessingBT = true;
        String input = SerialBT.readStringUntil('\n');
        input.trim();
        
        if (readOnlyMode) {
            if (isNumber(input)) {
                int idx = input.toInt();
                if (idx >= 1 && idx <= messageCount) {
                    loadMessage(idx);
                } else {
                    activeMsg = "Không tìm thấy!";
                    updateUI();
                }
            }
        }
        else if (input.length() > 0) {
            speakVietnamese("Bạn có tin nhắn mới");
            pref.begin("flashMessages", false);
            messageCount++;
            pref.putString(String(messageCount).c_str(), input);
            pref.putInt("msgCount", messageCount);
            pref.end();
            activeMsg = input;
            currentMsgIdx = messageCount;
            updateUI();
            speakVietnamese(activeMsg);
        }
        
        // Clear stale buffer data while audio is playing
        while (SerialBT.available() > 0) {
            SerialBT.read();
        }
        isProcessingBT = false;
    }
    
    handleAnimation();
    delay(10);
}