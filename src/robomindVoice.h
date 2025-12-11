// RoboDog AI Voice Integration with robomind - Using ESP8266Audio Library
// Connects ESP32 to robomind Flask server for speech-to-speech AI interaction
// Uses ESP32 INTERNAL_DAC (GPIO 25) for audio output
// HTTP client for communication with Python ML models
// Server returns WAV (PCM audio), played via ESP8266Audio library

#include <HTTPClient.h>
#include <WiFi.h>

// Minimal ESP8266Audio library for WAV playback
#include "AudioLib/AudioOutput.h"
#include "AudioLib/AudioOutputI2S.h"
#include "AudioLib/AudioFileSource.h"
#include "AudioLib/AudioGenerator.h"
#include "AudioLib/AudioGeneratorWAV.h"

// ==================== Configuration ====================

// Server configuration
#define ROBOMIND_SERVER_IP "192.168.1.248"  // UPDATE THIS with your server IP
#define ROBOMIND_SERVER_PORT 7777
#define ROBOMIND_ENDPOINT_PROCESS "/process"

// Audio settings
#define SAMPLE_RATE 16000       // 16kHz for Whisper compatibility (configured by WAV header)

// ESP32 INTERNAL DAC Configuration
// GPIO 25 = DAC channel 1 (used by buzzer - buzzer will not work during playback)
// No external I2S hardware needed!
#define AUDIO_OUTPUT_MODE 1     // INTERNAL_DAC mode
#define DMA_BUF_COUNT 8         // Standard DMA buffer count
#define USE_APLL 0              // Disable APLL (standard clock fine for 16kHz)

// Global state
bool robomindInitialized = false;
HTTPClient http;
WiFiClient client;

// ==================== Custom AudioFileSource for WiFiClient Stream ====================

class AudioFileSourceStream : public AudioFileSource {
  private:
    WiFiClient* stream;
    uint32_t totalSize;
    uint32_t currentPos;
    bool chunkedEncoding;
    uint32_t currentChunkRemaining;
    bool reachedEnd;

  public:
    AudioFileSourceStream(WiFiClient* s, uint32_t size = 0, bool chunked = true)
      : stream(s), totalSize(size), currentPos(0), chunkedEncoding(chunked),
        currentChunkRemaining(0), reachedEnd(false) {}

    virtual ~AudioFileSourceStream() override {}

    virtual uint32_t read(void *data, uint32_t len) override {
      if (!stream || reachedEnd) {
        return 0;
      }

      if (!chunkedEncoding) {
        // Simple non-chunked read
        uint32_t bytesRead = stream->readBytes((uint8_t*)data, len);
        currentPos += bytesRead;
        return bytesRead;
      }

      // Chunked encoding - decode while reading
      uint32_t bytesRead = 0;
      uint8_t* dest = (uint8_t*)data;

      while (bytesRead < len && !reachedEnd) {
        // Need to read a new chunk header?
        if (currentChunkRemaining == 0) {
          // Read chunk size line - wait for data if needed
          while (stream->available() == 0 && stream->connected()) {
            delay(1);  // Wait for data to arrive
          }

          if (stream->available() == 0) {
            // Stream ended
            reachedEnd = true;
            break;
          }

          String chunkLine = stream->readStringUntil('\n');
          chunkLine.trim();

          if (chunkLine.length() == 0) {
            reachedEnd = true;
            break;
          }

          // Parse hex chunk size
          currentChunkRemaining = strtoul(chunkLine.c_str(), NULL, 16);

          if (currentChunkRemaining == 0) {
            // Last chunk (size 0)
            reachedEnd = true;
            break;
          }
        }

        // Read from current chunk
        uint32_t toRead = min((uint32_t)(len - bytesRead), currentChunkRemaining);
        if (toRead > 0) {
          // Try to read the data - readBytes() will wait up to timeout
          uint32_t justRead = stream->readBytes(dest + bytesRead, toRead);

          if (justRead > 0) {
            bytesRead += justRead;
            currentChunkRemaining -= justRead;
            currentPos += justRead;

            // If chunk is exhausted, skip trailing \r\n
            if (currentChunkRemaining == 0) {
              stream->readStringUntil('\n');
            }
          } else {
            // No data received - check if connection is still alive
            if (!stream->connected() && stream->available() == 0) {
              reachedEnd = true;
              break;
            }
            // Otherwise keep trying - might be slow network
          }
        }
      }

      return bytesRead;
    }

    virtual bool seek(int32_t pos, int dir) override {
      // Seeking not supported on stream
      return false;
    }

    virtual bool close() override {
      if (stream) {
        stream->stop();
      }
      return true;
    }

    virtual bool isOpen() override {
      return stream && (stream->connected() || stream->available());
    }

    virtual uint32_t getSize() override {
      return totalSize;
    }

    virtual uint32_t getPos() override {
      return currentPos;
    }
};

// ==================== Custom AudioFileSource for Memory Buffer ====================

class AudioFileSourceBuffer : public AudioFileSource {
  private:
    uint8_t* buffer;
    uint32_t bufferSize;
    uint32_t currentPos;
    bool ownsBuffer;

  public:
    AudioFileSourceBuffer(uint8_t* buf, uint32_t size, bool owns = false)
      : buffer(buf), bufferSize(size), currentPos(0), ownsBuffer(owns) {}

    virtual ~AudioFileSourceBuffer() override {
      if (ownsBuffer && buffer) {
        free(buffer);
      }
    }

    virtual uint32_t read(void *data, uint32_t len) override {
      if (!buffer || currentPos >= bufferSize) {
        return 0;
      }

      uint32_t available = bufferSize - currentPos;
      uint32_t toRead = (len < available) ? len : available;

      memcpy(data, buffer + currentPos, toRead);
      currentPos += toRead;

      return toRead;
    }

    virtual bool seek(int32_t pos, int dir) override {
      if (dir == SEEK_SET) {
        currentPos = pos;
      } else if (dir == SEEK_CUR) {
        currentPos += pos;
      } else if (dir == SEEK_END) {
        currentPos = bufferSize + pos;
      }
      return (currentPos <= bufferSize);
    }

    virtual bool close() override {
      return true;
    }

    virtual bool isOpen() override {
      return buffer != nullptr;
    }

    virtual uint32_t getSize() override {
      return bufferSize;
    }

    virtual uint32_t getPos() override {
      return currentPos;
    }
};

// ==================== Audio Playback Using ESP8266Audio ====================

void playAudioFromBuffer(uint8_t* buffer, uint32_t size) {
  if (!buffer || size == 0) {
    PTLF("ERROR: Invalid buffer");
    return;
  }

  PTLF("Playing WAV audio from buffer...");
  PTHL("Buffer size: ", size);

  // Debug: Print first 64 bytes in detail
  Serial.println("=== First 64 bytes of buffer ===");
  uint32_t dumpSize = min((uint32_t)64, size);
  for (uint32_t i = 0; i < dumpSize; i += 16) {
    Serial.printf("%04X: ", i);
    // Hex dump
    for (uint32_t j = 0; j < 16 && (i + j) < dumpSize; j++) {
      Serial.printf("%02X ", buffer[i + j]);
    }
    // Padding
    for (uint32_t j = dumpSize - i; j < 16 && i + j >= dumpSize; j++) {
      Serial.print("   ");
    }
    Serial.print(" | ");
    // ASCII dump
    for (uint32_t j = 0; j < 16 && (i + j) < dumpSize; j++) {
      char c = buffer[i + j];
      if (c >= 32 && c < 127) {
        Serial.print(c);
      } else {
        Serial.print('.');
      }
    }
    Serial.println();
  }
  Serial.println("=== End of buffer dump ===");

  // Check if it looks like chunked encoding
  bool looksLikeChunked = true;
  for (int i = 0; i < 4 && i < size; i++) {
    if (buffer[i] < '0' || buffer[i] > 'f') {
      looksLikeChunked = false;
      break;
    }
  }
  if (looksLikeChunked) {
    PTLF("WARNING: Buffer starts with hex digits - might be chunked encoding!");
  }

  // Create audio components
  AudioFileSourceBuffer* file = new AudioFileSourceBuffer(buffer, size, false);
  AudioOutputI2S* out = new AudioOutputI2S(0, AUDIO_OUTPUT_MODE, DMA_BUF_COUNT, USE_APLL);
  AudioGeneratorWAV* wav = new AudioGeneratorWAV();

  // Begin playback
  if (wav->begin(file, out)) {
    PTLF("WAV playback started");

    // Loop until playback completes
    PTLF("WAV playback in progress...");
    while (wav->isRunning()) {
      PTLF("WAV playback loop...");
      if (!wav->loop()) {
        PTLF("WAV playback loop ended");
        wav->stop();
        break;
      }
      yield();  // Allow other tasks to run
    }

    PTLF("WAV playback finished");
  } else {
    PTLF("ERROR: Failed to begin WAV playback");
  }

  // Cleanup
  delete wav;
  delete out;
  delete file;
}

// ==================== Memory Diagnostics ====================

void printMemoryInfo() {
  Serial.println("\n========== ESP32 Memory Information ==========");

  // Chip information
  Serial.printf("Chip model: %s\n", ESP.getChipModel());
  Serial.printf("Chip revision: %d\n", ESP.getChipRevision());
  Serial.printf("CPU frequency: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash size: %d bytes (%d KB)\n", ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1024);

  // Heap (internal SRAM)
  Serial.println("\n--- Internal SRAM (Heap) ---");
  Serial.printf("Total heap size: ~320 KB (ESP32 standard)\n");
  Serial.printf("Free heap: %d bytes (%d KB)\n", ESP.getFreeHeap(), ESP.getFreeHeap() / 1024);
  Serial.printf("Largest free block: %d bytes (%d KB)\n", ESP.getMaxAllocHeap(), ESP.getMaxAllocHeap() / 1024);
  Serial.printf("Min free heap (ever): %d bytes (%d KB)\n", ESP.getMinFreeHeap(), ESP.getMinFreeHeap() / 1024);
  Serial.printf("Heap fragmentation: %d%%\n",
                100 - (ESP.getMaxAllocHeap() * 100 / ESP.getFreeHeap()));

  // PSRAM (external SPI RAM) - if available
  Serial.println("\n--- PSRAM (External SPI RAM) ---");
  #ifdef CONFIG_SPIRAM_SUPPORT
    if (psramFound()) {
      Serial.printf("PSRAM: FOUND\n");
      Serial.printf("PSRAM size: %d bytes (%d MB)\n", ESP.getPsramSize(), ESP.getPsramSize() / (1024 * 1024));
      Serial.printf("Free PSRAM: %d bytes (%d KB)\n", ESP.getFreePsram(), ESP.getFreePsram() / 1024);
      Serial.printf("Min free PSRAM: %d bytes (%d KB)\n", ESP.getMinFreePsram(), ESP.getMinFreePsram() / 1024);
    } else {
      Serial.println("PSRAM: NOT FOUND (not installed on this chip)");
    }
  #else
    Serial.println("PSRAM: NOT ENABLED (CONFIG_SPIRAM_SUPPORT not defined)");
    Serial.println("To enable: Tools > PSRAM > Enabled in Arduino IDE");
  #endif

  // Sketch information
  Serial.println("\n--- Sketch/Program ---");
  Serial.printf("Sketch size: %d bytes (%d KB)\n", ESP.getSketchSize(), ESP.getSketchSize() / 1024);
  Serial.printf("Free sketch space: %d bytes (%d KB)\n", ESP.getFreeSketchSpace(), ESP.getFreeSketchSpace() / 1024);

  Serial.println("==============================================\n");
}

// ==================== Initialization ====================

void robomindVoiceSetup() {
  PTLF("Initializing robomind AI voice system...");
  PTLF("Using ESP32 INTERNAL_DAC on GPIO 25");
  PTLF("WARNING: Buzzer on GPIO 25 will not work during playback");

  // Print memory diagnostics
  printMemoryInfo();

  robomindInitialized = true;
  PTLF("robomind voice system initialized (speaker-only mode)");
  PTLF("Use playRandomRobomindVoice() to play AI responses");
}

void robomindVoiceLoop() {
  // No-op in speaker-only mode
  // Use playRandomRobomindVoice() to trigger AI responses
}

// ==================== Optional: Health Check ====================

bool checkRobomindHealth() {
  extern bool webServerConnected;  // Defined in webServer.h

  if (!WiFi.isConnected() || !webServerConnected) {
    PTLF("WiFi not connected");
    return false;
  }

  String url = String("http://") + ROBOMIND_SERVER_IP + ":" +
               String(ROBOMIND_SERVER_PORT) + "/health";

  HTTPClient healthHttp;
  healthHttp.begin(url);
  healthHttp.setTimeout(5000);

  int httpResponseCode = healthHttp.GET();
  healthHttp.end();

  if (httpResponseCode == 200) {
    PTLF("robomind server is healthy");
    return true;
  } else {
    PTHL("robomind health check failed: ", httpResponseCode);
    return false;
  }
}

// ==================== Optional: Random POST Request ====================
bool playRandomRobomindVoice() {
  extern bool webServerConnected;  // Defined in webServer.h

  if (!WiFi.isConnected() || !webServerConnected) {
    PTLF("WiFi not connected");
    return false;
  }

  String url = String("http://") + ROBOMIND_SERVER_IP + ":" +
               String(ROBOMIND_SERVER_PORT) + "/random";

  PTLF("Requesting random AI response...");

  // Use global http object
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(30000);  // 30 second timeout for generation

  // Collect response headers
  const char* headerKeys[] = {"Content-Type"};
  http.collectHeaders(headerKeys, 1);

  // Send POST request (endpoint expects POST)
  int httpResponseCode = http.POST("");

  if (httpResponseCode == 200) {
    PTLF("Received random response, streaming audio...");

    // Get stream for reading response body
    WiFiClient* stream = http.getStreamPtr();

    if (!stream) {
      PTLF("ERROR: Failed to get stream pointer");
      http.end();
      return false;
    }

    // Create streaming audio source with chunked decoding
    AudioFileSourceStream* file = new AudioFileSourceStream(stream, 0, true);
    AudioOutputI2S* out = new AudioOutputI2S(0, AUDIO_OUTPUT_MODE, DMA_BUF_COUNT, USE_APLL);
    AudioGeneratorWAV* wav = new AudioGeneratorWAV();

    // Increase internal buffer to smooth out network jitter
    // Default is 128 bytes, increase to 2KB for better buffering
    wav->SetBufferSize(2048);

    // Begin playback - will stream while playing
    PTLF("Starting streaming WAV playback...");
    if (wav->begin(file, out)) {
      PTLF("WAV playback started, streaming from HTTP");

      // Loop until playback completes
      while (wav->isRunning()) {
        if (!wav->loop()) {
          wav->stop();
          break;
        }
        yield();  // Allow other tasks to run
      }

      PTLF("WAV playback finished");
    } else {
      PTLF("ERROR: Failed to begin WAV playback");
    }

    // Cleanup
    delete wav;
    delete out;
    delete file;

    http.end();

    PTLF("Random response playback complete");
    return true;
  } else {
    PTHL("HTTP Error: ", httpResponseCode);
    http.end();
    return false;
  }
}
