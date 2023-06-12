/*
  Game_Audio Library

  This library is based heavily on the XT_DAC_Audio libray
  http://www.xtronical.com/basics/audio/digitised-speech-sound-esp32-playing-wavs/

  This library is designed for 8-bit quality audio that an old school video
  game might use. This leaves plenty of cpu resources for the video game logic.

  The interrupt is sync'd with the sample rate. This removes the need to do any
  complex math in the interrupt. Using the FPU in the interrupt appears to have been
  causing some instability and crashes when I loaded up the CPU with game logic.

  (c) B. Dring 2018, Licensed under GNU GPL 3.0 and later, under this license absolutely no warranty given

*/

#include "sound.h"
#include <LittleFS.h>
//* #include "soc/rtc_io_reg.h"

const gpio_num_t DacPin = GPIO_NUM_20;
const uint16_t sampleRate = 16000;
const uint32_t clockPeriod = 4000000;

uint8_t *WaveData = NULL;

// Interrupt timer for fixed sample rate playback (horn etc., playing in parallel with engine sound)
hw_timer_t *fixedTimer = NULL;
portMUX_TYPE fixedTimerMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t fixedTimerTicks;

Car_Audio_Wav_Class *curr_Wave;

// because of the difficulty in passing parameters to interrupt handlers we have a global
// object of this type that points to the object the user creates.

// this interrupt is called at the SampleRate of the audio file.

volatile uint32_t DataIdx_curr;
volatile size_t m_buffSize_curr;
volatile bool stop_wave_curr;
volatile bool noSound;
volatile uint8_t lastValue;
volatile uint8_t currValue;

void IRAM_ATTR onSoundTimer()
{
  if (noSound)
    return;
  currValue = WaveData[DataIdx_curr];
  if (currValue != lastValue)
//*    SET_PERI_REG_BITS(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, currValue, RTC_IO_PDAC1_DAC_S);
  //    dacWrite(DacPin, WavData[DataIdx_curr]); // write out the data
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

Car_Audio_Wav_Class::Car_Audio_Wav_Class()
{
  if (fixedTimer == NULL)
  {
    fixedTimerTicks = clockPeriod / sampleRate;
    noSound = true;
    // Interrupt timer for sample rate
    fixedTimer = timerBegin(0, 20, true);                  // timer 0, MWDT clock period = 12.5 ns * TIMGn_Tx_WDT_CLK_PRESCALE -> 12.5 ns * 20 -> 250 ns = 0.25 us, countUp
    timerAttachInterrupt(fixedTimer, &onSoundTimer, true); // edge (not level) triggered
    timerAlarmWrite(fixedTimer, fixedTimerTicks, true);    // autoreload true
    timerAlarmEnable(fixedTimer);                          // enable
  }
}

void Car_Audio_Wav_Class::Load_Wave(const char *path, bool play_once)
{
  if (strlen(path) > 255)
    Serial.println("Filename: " + String(path) + " too long");

  memcpy(audioName, path, strlen(path) + 1);
  if (audioName[0] != '/')
  {
    for (int i = 255; i > 0; i--)
      audioName[i] = audioName[i - 1];
    audioName[0] = '/';
  }
  stop_wave = play_once;
  Serial.println("Wave: " + String(path));
  bool fsok = LittleFS.begin();
  Serial.printf_P(PSTR("LittleFS init: %S\r\n"), fsok ? PSTR("ok") : PSTR("fail!"));
  if (LittleFS.exists(audioName))
    audiofile = LittleFS.open(audioName);
  else
    Serial.println("file not found!");

  if (audiofile)
  {
    // size of wave-file
    m_buffSize = audiofile.size();
    // allocate space
    WaveDataPtr = (uint8_t *)malloc(m_buffSize);
    if (WaveDataPtr == NULL)
      Serial.println("Fehler: Kein Speicher!");
    // read the file
    size_t rdBytes = audiofile.read(WaveDataPtr, m_buffSize);
    // just for clarification: size in the file
    m_buffSize = (WaveDataPtr[WAV_FILESIZE_H] * 65536) + (WaveDataPtr[WAV_FILESIZE_M] * 256) + WaveDataPtr[WAV_FILESIZE_L] + AUDIO_DATA_START;
    if (rdBytes != (m_buffSize + 1))
    {
      Serial.println("Failed to open file for reading");
      Serial.println("Reading: " + String(audioName));
      Serial.println("rdBytes: " + String(rdBytes) + " bytes");
      Serial.println("m_buffSize: " + String(m_buffSize) + " bytes");
    }
    audiofile.close();
  }
  LittleFS.end();
  vTaskDelay(2);
}

int Car_Audio_Wav_Class::PlayWav()
{
  if (WaveDataPtr)
  {
    vTaskDelay(2);
    m_buffSize_curr = m_buffSize;
    stop_wave_curr = stop_wave;
    curr_Wave = this;
    DataIdx_curr = AUDIO_DATA_START;
    lastValue = 0x7F;
//*    dacWrite(DacPin, lastValue); // Set speaker to mid point to stop any clicks during sample playback
    noSound = false;
    WaveData = WaveDataPtr;
  }

  return 1;
}

void Car_Audio_Wav_Class::StopPlaying()
{
  noSound = true;
//*  dacWrite(DacPin, 0x00); // Set speaker to mid point to stop any clicks during sample playback
  Serial.println("Closing audio file");
}
