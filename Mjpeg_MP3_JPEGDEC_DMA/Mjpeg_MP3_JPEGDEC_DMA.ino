/*
 * require libraries:
 * https://github.com/moononournation/Arduino_GFX.git
 * https://github.com/earlephilhower/ESP8266Audio.git
 * https://github.com/bitbank2/JPEGDEC.git
 */
#define MP3_FILENAME "/22050.mp3"
#define FPS 15
#define MJPEG_FILENAME "/320_15fps.mjpeg"
#define MJPEG_BUFFER_SIZE (320 * 240 * 2 / 4)

#include <WiFi.h>
#include <SD_MMC.h>

/* Arduino_GFX */
#include <Arduino_GFX_Library.h>
#define TFT_BRIGHTNESS 128
#define TFT_BL 23
static Arduino_DataBus *bus = new Arduino_ESP32SPI(19 /* DC */, 5 /* CS */, 22 /* SCK */, 21 /* MOSI */, -1 /* MISO */, VSPI, true);
static Arduino_ILI9341 *gfx = new Arduino_ILI9341(bus, 18 /* RST */, 1 /* rotation */);

/* MP3 Audio */
#include <AudioFileSourceFS.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
static AudioGeneratorMP3 *mp3;
static AudioFileSourceFS *aFile;
static AudioOutputI2S *out;

/* MJPEG Video */
#include "MjpegClass.h"
static MjpegClass mjpeg;

/* variables */
static int next_frame = 0;
static int skipped_frames = 0;
static unsigned long total_play_audio = 0;
static unsigned long total_read_video = 0;
static unsigned long total_decode_video = 0;
static unsigned long total_show_video = 0;
static unsigned long total_remain = 0;
static unsigned long start_ms, curr_ms, next_frame_ms;

// pixel drawing callback
static void drawMCU(JPEGDRAW *pDraw)
{
  // Serial.printf("Draw pos = %d,%d. size = %d x %d\n", pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight);
  unsigned long s = millis();
  gfx->startWrite();
  gfx->writeAddrWindow(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight);
  gfx->writeBytes((uint8_t *)pDraw->pPixels, pDraw->iWidth * pDraw->iHeight * 2);
  gfx->endWrite();
  total_show_video += millis() - s;
} /* drawMCU() */

void setup()
{
  WiFi.mode(WIFI_OFF);
  Serial.begin(115200);

  // Init Video
  gfx->begin();
  gfx->fillScreen(BLACK);

#ifdef TFT_BL
  ledcSetup(1, 12000, 8);       // 12 kHz PWM, 8-bit resolution
  ledcAttachPin(TFT_BL, 1);     // assign TFT_BL pin to channel 1
  ledcWrite(1, TFT_BRIGHTNESS); // brightness 0 - 255
#endif

  // Init SD card
  if ((!SD_MMC.begin()) && (!SD_MMC.begin())) /* 4-bit SD bus mode */
  // if ((!SD_MMC.begin("/sdcard", true)) && (!SD_MMC.begin("/sdcard", true))) /* 1-bit SD bus mode */
  {
    Serial.println(F("ERROR: SD card mount failed!"));
    gfx->println(F("ERROR: SD card mount failed!"));
  }
  else
  {
    aFile = new AudioFileSourceFS(SD_MMC, MP3_FILENAME);
    out = new AudioOutputI2S(0, 1, 64); // Output to builtInDAC
    // out->SetGain(0.5);
    mp3 = new AudioGeneratorMP3();

    File vFile = SD_MMC.open(MJPEG_FILENAME);
    if (!vFile || vFile.isDirectory())
    {
      Serial.println(F("ERROR: Failed to open " MJPEG_FILENAME " file for reading"));
      gfx->println(F("ERROR: Failed to open " MJPEG_FILENAME " file for reading"));
    }
    else
    {
      uint8_t *mjpeg_buf = (uint8_t *)malloc(MJPEG_BUFFER_SIZE);
      if (!mjpeg_buf)
      {
        Serial.println(F("mjpeg_buf malloc failed!"));
      }
      else
      {
        Serial.println(F("PCM audio MJPEG video start"));

        start_ms = millis();
        curr_ms = millis();
        mjpeg.setup(&vFile, mjpeg_buf, drawMCU, true, true);
        mp3->begin(aFile, out);
        next_frame_ms = start_ms + (++next_frame * 1000 / FPS);

        while (vFile.available())
        {
          // Play audio
          if ((mp3->isRunning()) && (!mp3->loop()))
          {
            mp3->stop();
          }
          total_play_audio += millis() - curr_ms;
          curr_ms = millis();

          // Read video
          mjpeg.readMjpegBuf();
          total_read_video += millis() - curr_ms;
          curr_ms = millis();

          if (millis() < next_frame_ms) // check show frame or skip frame
          {
            // Play video
            mjpeg.drawJpg();
            total_decode_video += millis() - curr_ms;

            int remain_ms = next_frame_ms - millis();
            if (remain_ms > 0)
            {
              total_remain += remain_ms;
              delay(remain_ms);
            }
          }
          else
          {
            ++skipped_frames;
            Serial.println(F("Skip frame"));
          }

          curr_ms = millis();
          next_frame_ms = start_ms + (++next_frame * 1000 / FPS);
        }
        int time_used = millis() - start_ms;
        int total_frames = next_frame - 1;
        Serial.println(F("PCM audio MJPEG video end"));
        vFile.close();
        int played_frames = total_frames - skipped_frames;
        float fps = 1000.0 * played_frames / time_used;
        Serial.printf("Played frames: %d\n", played_frames);
        Serial.printf("Skipped frames: %d (%0.1f %%)\n", skipped_frames, 100.0 * skipped_frames / total_frames);
        Serial.printf("Time used: %d ms\n", time_used);
        Serial.printf("Expected FPS: %d\n", FPS);
        Serial.printf("Actual FPS: %0.1f\n", fps);
        Serial.printf("Play MP3: %lu ms (%0.1f %%)\n", total_play_audio, 100.0 * total_play_audio / time_used);
        Serial.printf("SDMMC read MJPEG: %lu ms (%0.1f %%)\n", total_read_video, 100.0 * total_read_video / time_used);
        Serial.printf("Decode video: %lu ms (%0.1f %%)\n", total_decode_video, 100.0 * total_decode_video / time_used);
        Serial.printf("Show video: %lu ms (%0.1f %%)\n", total_show_video, 100.0 * total_show_video / time_used);
        Serial.printf("Remain: %lu ms (%0.1f %%)\n", total_remain, 100.0 * total_remain / time_used);

#define CHART_MARGIN 24
#define LEGEND_A_COLOR 0xE0C3
#define LEGEND_B_COLOR 0x33F7
#define LEGEND_C_COLOR 0x4D69
#define LEGEND_D_COLOR 0x9A74
#define LEGEND_E_COLOR 0xFBE0
#define LEGEND_F_COLOR 0xFFE6
#define LEGEND_G_COLOR 0xA2A5
        gfx->setCursor(0, 0);
        gfx->setTextColor(WHITE);
        gfx->printf("Played frames: %d\n", played_frames);
        gfx->printf("Skipped frames: %d (%0.1f %%)\n", skipped_frames, 100.0 * skipped_frames / total_frames);
        gfx->printf("Actual FPS: %0.1f\n\n", fps);
        int16_t r1 = ((gfx->height() - CHART_MARGIN - CHART_MARGIN) / 2);
        int16_t r2 = r1 / 2;
        int16_t cx = gfx->width() - gfx->height() + CHART_MARGIN + CHART_MARGIN - 1 + r1;
        int16_t cy = r1 + CHART_MARGIN;
        float arc_start = 0;
        float arc_end = max(2.0, 360.0 * total_play_audio / time_used);
        for (int i = arc_start + 1; i < arc_end; i += 2)
        {
          gfx->fillArc(cx, cy, r1, r2, arc_start - 90.0, i - 90.0, LEGEND_D_COLOR);
        }
        gfx->fillArc(cx, cy, r1, r2, arc_start - 90.0, arc_end - 90.0, LEGEND_D_COLOR);
        gfx->setTextColor(LEGEND_D_COLOR);
        gfx->printf("Play MP3:\n%0.1f %%\n", 100.0 * total_play_audio / time_used);

        arc_start = arc_end;
        arc_end += max(2.0, 360.0 * total_read_video / time_used);
        for (int i = arc_start + 1; i < arc_end; i += 2)
        {
          gfx->fillArc(cx, cy, r1, r2, arc_start - 90.0, i - 90.0, LEGEND_C_COLOR);
        }
        gfx->fillArc(cx, cy, r1, r2, arc_start - 90.0, arc_end - 90.0, LEGEND_C_COLOR);
        gfx->setTextColor(LEGEND_C_COLOR);
        gfx->printf("Read MJPEG:\n%0.1f %%\n", 100.0 * total_read_video / time_used);

        arc_start = arc_end;
        arc_end += max(2.0, 360.0 * total_decode_video / time_used);
        for (int i = arc_start + 1; i < arc_end; i += 2)
        {
          gfx->fillArc(cx, cy, r1, r2, arc_start - 90.0, i - 90.0, LEGEND_B_COLOR);
        }
        gfx->fillArc(cx, cy, r1, r2, arc_start - 90.0, arc_end - 90.0, LEGEND_B_COLOR);
        gfx->setTextColor(LEGEND_B_COLOR);
        gfx->printf("Decode video:\n%0.1f %%\n", 100.0 * total_decode_video / time_used);

        arc_start = arc_end;
        arc_end += max(2.0, 360.0 * total_show_video / time_used);
        for (int i = arc_start + 1; i < arc_end; i += 2)
        {
          gfx->fillArc(cx, cy, r2, 0, arc_start - 90.0, i - 90.0, LEGEND_A_COLOR);
        }
        gfx->fillArc(cx, cy, r2, 0, arc_start - 90.0, arc_end - 90.0, LEGEND_A_COLOR);
        gfx->setTextColor(LEGEND_A_COLOR);
        gfx->printf("Play video:\n%0.1f %%\n", 100.0 * total_show_video / time_used);
      }
    }
  }
#ifdef TFT_BL
  delay(60000);
  ledcDetachPin(TFT_BL);
#endif
  gfx->displayOff();
  esp_deep_sleep_start();
}

void loop()
{
}
