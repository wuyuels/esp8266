#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <SPI.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

#define TFT_CS        5
#define TFT_RST       4
#define TFT_DC        0
#define TFT_MOSI 13  // Data out
#define TFT_SCLK 14  // Clock out

#define BUFFPIXEL 20

// For ST7735-based displays, we will use this call
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);


#ifndef APSSID
#define APSSID "aida64"
#define APPSK  "lslsls16"
#endif

/* Set these to your desired credentials. */
const char *ssid = APSSID;
const char *password = APPSK;
const unsigned long HTTP_TIMEOUT = 200;
//获取数据计数器
int httpCount = 0;
int count = 0;

ESP8266WebServer server(80);

HTTPClient http;

const char* serverIndex = "<form method='POST' action='/put' enctype='multipart/form-data'>wifiName:<input type='text' name='name'> <br>wifiPwd:<input type='text' name='pwd'><br> Url:<input type='text' name='url'> <br>file:<input type='file' name='pic'><br><input type='submit' value='Update'></form>";

File fsUploadFile;

/**
   启动
*/
void setup() {
  Serial.begin(115200);
  tft.initR(INITR_144GREENTAB); // Init ST7735R chip, green tab
  tft.setRotation(2);
  //文件服务
  bool ok = SPIFFS.begin();
  //检查文件是否存在
  bool exist = SPIFFS.exists("/config.json");
  //检查文件是否存在
  bool existPic = SPIFFS.exists("/logo.bmp");
  if (!exist || !existPic) {
    //打开ap模式
    openApMode();
  } else {
    //播放开机动画
    bootAnimation();
    delay(20000);
    //打开wifi模式
    openWifiMode();
  }
}

/*
   打印开机动画
*/
void bootAnimation() {
  tft.fillScreen(ST7735_BLUE);
  bmpDraw("/logo.bmp", 0, 0);
}

/**
   循环
*/
void loop() {
  // put your main code here, to run repeatedly:
  server.handleClient();
  if (WiFi.status() == WL_CONNECTED) {
    //获取数据
    if (!getData()) {
      httpCount++;
      if (httpCount > 100) {
        SPIFFS.remove("/config.json");
        ESP.restart();
      }
    } else {
      httpCount = 0;
    }
  }
  delay(5000);
}
/**
   读取文件
*/
String readFile(String key) {
  String value = "";
  bool exist = SPIFFS.exists("/config.json");
  if (exist) {
    File f = SPIFFS.open("/config.json", "r");
    if (!f) {
      // 在打开过程中出现问题f就会为空
      Serial.println("Some thing went wrong trying to open the file...");
      SPIFFS.remove("/config.json");

    } else {
      //读取文本内容
      String data = f.readString();
      DynamicJsonDocument doc(1024);
      // Deserialize the JSON document
      deserializeJson(doc, data);
      value = doc[key].as<String>();
      //关闭文件
      f.close();
    }
  }
  return value;
}

/**
   获取数据
*/
bool getData() {
  String url = readFile("url");
  http.setTimeout(HTTP_TIMEOUT);
  //设置请求url
  if (http.begin(url)) {  // HTTP
    Serial.print("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);
      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        //解析响应内容
        parseData(response);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      bootAnimation();
    }
    http.end();

    return true;
  } else {
    Serial.printf("[HTTP} Unable to connect\n");
    bootAnimation();
    return false;
  }
}
/**
   数据解析
*/
bool parseData(String message) {
  int start = message.indexOf("data: ");
  message = message.substring(start + 6, message.length());
  int end = message.indexOf("\n");
  message = message.substring(0, end);
  int commaPosition = 0;
  tft.setTextWrap(true);
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 5);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  do {
    commaPosition = message.indexOf('{|}');//检测字符串中的逗号
    if (commaPosition != -1) {
      message = message.substring(commaPosition + 2, message.length()); //打印字符串，从当前位置+1开始
      int tempEnd = message.indexOf('{|}');
      if (tempEnd > 2) {
        String temp = message.substring(0, tempEnd - 2);
        temp = temp.substring( temp.indexOf('|') + 1, temp.length());
        tft.println(temp);
        Serial.println(temp);
      }
    } else { //找到最后一个逗号，如果后面还有文字，就打印出来
      if (message.length() > 0) Serial.println(message);
    }
  } while (commaPosition >= 0);
  Serial.println(message);
  return true;
}

/*
   Just a little test message.  Go to http://192.168.4.1 in a web browser
   connected to this access point to see it.
*/
void handleRoot() {
  server.send(200, "text/html", serverIndex);
}
/*
    获取上传的数据
*/
void handlePut() {
  DynamicJsonDocument doc(1024);
  String message = "POST form was:\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    doc[server.argName(i)] = server.arg(i);
  }
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    SPIFFS.remove("/config.json");
    return;
  }
  serializeJson(doc, configFile);
  Serial.println( message);
  //关闭文件
  configFile.close();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "OK");
  delay(3000);
  ESP.restart();
}

/*
    获取上传的图片数据
*/
void handlePutPic() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    Serial.print("handlePutPic Name: "); Serial.println(filename);
    SPIFFS.remove("/logo.bmp");
    fsUploadFile = SPIFFS.open("/logo.bmp", "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    //Serial.print("handleFileUpload Data: "); Serial.println(upload.currentSize);
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile)
      fsUploadFile.close();
    Serial.print("handlePutPic Size: "); Serial.println(upload.totalSize);
    bmpDraw("/logo.bmp", 0, 0);
  }
}


/*
   打开WiFi模式
*/
void openWifiMode() {
  count = 0;
  WiFi.mode(WIFI_STA);
  String wifiName = readFile("name");
  String pwd = readFile("pwd");
  Serial.println(wifiName);
  Serial.println(pwd);
  WiFi.begin(wifiName, pwd);
  while (WiFi.status() != WL_CONNECTED) {
    if (count > 15) {
      tft.setTextWrap(false);
      tft.setCursor(0, 30);
      tft.setTextColor(ST77XX_WHITE);
      tft.setTextSize(1);
      tft.println("WiFi connected fail");
      Serial.println("WiFi connected fail");
      SPIFFS.remove("/config.json");
      ESP.restart();
    }
    tft.setTextWrap(false);
    tft.setCursor(0, 30);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(1);
    tft.println("WiFi connecteding~~~~");
    delay(10000);
    Serial.print(".");
    count++;
  }
  tft.setTextWrap(false);
  tft.setCursor(0, 30);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.println("WiFi connected success");
  Serial.println("WiFi connected success");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}
/**
   打开ap模式
*/
void openApMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/put", HTTP_POST, handlePut, handlePutPic);
  server.begin();
  Serial.println("HTTP server started");
  
  tft.setTextWrap(false);
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 30);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(1);
  tft.println("AP IP address: ");
  tft.println( myIP);
  tft.println("AP name: " );
  tft.println(ssid);
  tft.println("AP pwd: " );
  tft.println(password);
}

/*
   图片显示
*/
void bmpDraw(char *filename, uint8_t x, uint8_t y) {
  File     bmpFile;
  int      bmpWidth, bmpHeight;   // W+H in pixels
  uint8_t  bmpDepth;              // Bit depth (currently must be 24)
  uint32_t bmpImageoffset;        // Start of image data in file
  uint32_t rowSize;               // Not always = bmpWidth; may have padding
  uint8_t  sdbuffer[3 * BUFFPIXEL]; // pixel buffer (R+G+B per pixel)
  uint8_t  buffidx = sizeof(sdbuffer); // Current position in sdbuffer
  boolean  goodBmp = false;       // Set to true on valid header parse
  boolean  flip    = true;        // BMP is stored bottom-to-top
  int      w, h, row, col;
  uint8_t  r, g, b;
  uint32_t pos = 0, startTime = millis();

  if ((x >= tft.width()) || (y >= tft.height())) return;

  Serial.println();
  Serial.print("Loading image '");
  Serial.print(filename);
  Serial.println('\'');
  bmpFile = SPIFFS.open(filename, "r");
  // Open requested file on SD card
  if (!bmpFile) {
    Serial.print("File not found");
    return;
  }

  // Parse BMP header
  if (read16(bmpFile) == 0x4D42) { // BMP signature
    Serial.print("File size: "); Serial.println(read32(bmpFile));
    (void)read32(bmpFile); // Read & ignore creator bytes
    bmpImageoffset = read32(bmpFile); // Start of image data
    Serial.print("Image Offset: "); Serial.println(bmpImageoffset, DEC);
    // Read DIB header
    Serial.print("Header size: "); Serial.println(read32(bmpFile));
    bmpWidth  = read32(bmpFile);
    bmpHeight = read32(bmpFile);
    if (read16(bmpFile) == 1) { // # planes -- must be '1'
      bmpDepth = read16(bmpFile); // bits per pixel
      Serial.print("Bit Depth: "); Serial.println(bmpDepth);
      if ((bmpDepth == 24) && (read32(bmpFile) == 0)) { // 0 = uncompressed

        goodBmp = true; // Supported BMP format -- proceed!
        Serial.print("Image size: ");
        Serial.print(bmpWidth);
        Serial.print('x');
        Serial.println(bmpHeight);

        // BMP rows are padded (if needed) to 4-byte boundary
        rowSize = (bmpWidth * 3 + 3) & ~3;

        // If bmpHeight is negative, image is in top-down order.
        // This is not canon but has been observed in the wild.
        if (bmpHeight < 0) {
          bmpHeight = -bmpHeight;
          flip      = false;
        }

        // Crop area to be loaded
        w = bmpWidth;
        h = bmpHeight;
        if ((x + w - 1) >= tft.width())  w = tft.width()  - x;
        if ((y + h - 1) >= tft.height()) h = tft.height() - y;

        // Set TFT address window to clipped image bounds
        tft.setAddrWindow(x, y, x + w - 1, y + h - 1);

        for (row = 0; row < h; row++) { // For each scanline...

          // Seek to start of scan line.  It might seem labor-
          // intensive to be doing this on every line, but this
          // method covers a lot of gritty details like cropping
          // and scanline padding.  Also, the seek only takes
          // place if the file position actually needs to change
          // (avoids a lot of cluster math in SD library).
          if (flip) // Bitmap is stored bottom-to-top order (normal BMP)
            pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
          else     // Bitmap is stored top-to-bottom
            pos = bmpImageoffset + row * rowSize;
          if (bmpFile.position() != pos) { // Need seek?
            bmpFile.seek(pos, SeekSet);
            buffidx = sizeof(sdbuffer); // Force buffer reload
          }

          for (col = 0; col < w; col++) { // For each pixel...
            // Time to read more pixel data?
            if (buffidx >= sizeof(sdbuffer)) { // Indeed
              bmpFile.read(sdbuffer, sizeof(sdbuffer));
              buffidx = 0; // Set index to beginning
            }

            // Convert pixel from BMP to TFT format, push to display
            b = sdbuffer[buffidx++];
            g = sdbuffer[buffidx++];
            r = sdbuffer[buffidx++];
            tft.pushColor(tft.color565(r, g, b));
          } // end pixel
        } // end scanline
        Serial.print("Loaded in ");
        Serial.print(millis() - startTime);
        Serial.println(" ms");
      } // end goodBmp
    }
  }
  bmpFile.close();
  if (!goodBmp) Serial.println("BMP format not recognized.");
}


uint16_t read16(File f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}
