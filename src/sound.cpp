
#include "Arduino.h"
#include "SPI.h"
#include "sound.h"
#include <LittleFS.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

const gpio_num_t MCP4921_CS_PIN = GPIO_NUM_20;

const uint16_t sampleRate = 16000;
const uint32_t clockPeriod = 4000000;
const uint64_t timerRate = 62; // = 1/(80000000/2*sampleRate/clockPeriod)*1000000;

uint8_t *WaveData = NULL;
esp_timer_handle_t periodic_timer;

// Interrupt timer for fixed sample rate playback (horn etc., playing in parallel with engine sound)

Car_Audio_Wav_Class *curr_Wave;

// because of the difficulty in passing parameters to interrupt handlers we have a global
// object of this type that points to the object the user creates.

// this interrupt is called at the SampleRate of the audio file.

uint32_t m_buffSize_curr;
bool stop_wave_curr;

bool noSound;
uint16_t lastValue;
uint16_t currValue;

void mcp4921(uint16_t data)
{
  data <<= 4;
  digitalWrite(MCP4921_CS_PIN, LOW); // initiate data transfer with 4921
  SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));

  SPI.transfer((0b00110000 & 0xf0) | ((data >> 8) & 0x0f)); // send 1st byte
  SPI.transfer((uint8_t)(data & 0xff));                     // send 2nd byte

  SPI.endTransaction();
  digitalWrite(MCP4921_CS_PIN, HIGH); // finish data transfer
}

static void onSoundTimer(void *arg)
{
  static volatile uint32_t DataIdx_curr = AUDIO_DATA_START;
  if (noSound)
    return;

  currValue = WaveData[DataIdx_curr]; // shift to 16 bit
  if (currValue != lastValue)
    mcp4921(currValue);
  lastValue = currValue;
  DataIdx_curr++;

  if (DataIdx_curr >= m_buffSize_curr) // if that was the last data byte...
  {
    if (stop_wave_curr)
      curr_Wave->StopPlaying();
    else
      DataIdx_curr = AUDIO_DATA_START;
  }
}

void Attach_Sound_Once()
{
  pinMode(MCP4921_CS_PIN, OUTPUT);
  digitalWrite(MCP4921_CS_PIN, HIGH);

  SPI.begin();
}

Car_Audio_Wav_Class::Car_Audio_Wav_Class()
{
}

void Car_Audio_Wav_Class::Load_Wave(const char *path, bool play_once)
{
  if (strlen(path) > 255)
    log_e("Filename: %s too long", path);

  memcpy(audioName, path, strlen(path) + 1);
  if (audioName[0] != '/')
  {
    for (int i = 255; i > 0; i--)
      audioName[i] = audioName[i - 1];
    audioName[0] = '/';
  }
  stop_wave = play_once;
  log_e("Wave: %s", path);
  bool fsok = LittleFS.begin();
  if (fsok)
    log_e("LittleFS init: ok");
  else
    log_e("LittleFS init: fail!");
  if (LittleFS.exists(audioName))
    audiofile = LittleFS.open(audioName);
  else
    log_e("file not found!");

  if (audiofile)
  {
    // size of wave-file
    m_buffSize = audiofile.size();
    // allocate space
    WaveDataPtr = (uint8_t *)malloc(m_buffSize);
    if (WaveDataPtr == NULL)
      log_e("Fehler: Kein Speicher!");
    // read the file
    size_t rdBytes = audiofile.read(WaveDataPtr, m_buffSize);
    // just for clarification: size in the file
    m_buffSize = (WaveDataPtr[WAV_FILESIZE_H] * 65536) + (WaveDataPtr[WAV_FILESIZE_M] * 256) + WaveDataPtr[WAV_FILESIZE_L] + AUDIO_DATA_START;
    uint16_t rate = (WaveDataPtr[WAV_SAMPLERATE_H] * 256) + WaveDataPtr[WAV_SAMPLERATE_L];
    log_e("Samplerate: %d Hz", rate);

    if (rdBytes != (m_buffSize + 1))
    {
      log_e("Failed to open file for reading");
    }
    log_e("Reading: %s", audioName);
    log_e("rdBytes: %d bytes", rdBytes);
    log_e("m_buffSize: %d bytes", m_buffSize);

    audiofile.close();
    log_e("Closing audio file");
  }
  LittleFS.end();
  vTaskDelay(2);
  mcp4921(0x00);
}

int Car_Audio_Wav_Class::PlayWav()
{
  if (WaveDataPtr)
  {
    vTaskDelay(2);
    m_buffSize_curr = m_buffSize;
    stop_wave_curr = stop_wave;
    curr_Wave = this;
    lastValue = 0x7F;
    mcp4921(lastValue);
    noSound = false;
    WaveData = WaveDataPtr;
    /* Create a timer:
     * a periodic timer which will run every 0.5s, and print a message timer with period of 1s.
     */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &onSoundTimer,
        /* name is optional, but may help identify the timer when debugging */
        .name = "onSoundTimer"};

    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    /* The timer has been created but is not running yet */
    log_e("Created timer");

    /* Start the timers */
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, timerRate));
    log_e("Started timer");
  }

  return 1;
}

void Car_Audio_Wav_Class::StopPlaying()
{
  noSound = true;
  mcp4921(0x00); // lookuptable

  /* Clean up and finish the example */
  ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
  log_e("Stopped timer");
  ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));
  log_e("Deleted timer");
}
