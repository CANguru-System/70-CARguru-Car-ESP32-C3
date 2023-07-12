
#include "Arduino.h"
#include "sound.h"
#include "LittleFS.h"
#include "soc/rtc_io_reg.h"

const gpio_num_t DacPin = GPIO_NUM_25;
const uint16_t sampleRate = 16000;
const uint32_t clockPeriod = 4000000;

uint8_t *WaveData = NULL;

// Interrupt timer for fixed sample rate playback (horn etc., playing in parallel with engine sound)
hw_timer_t *fixedTimer = NULL;
portMUX_TYPE fixedTimerMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t fixedTimerTicks;

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

void IRAM_ATTR onSoundTimer()
{
  static volatile uint32_t DataIdx_curr = AUDIO_DATA_START;
  if (noSound)
    return;
  currValue = WaveData[DataIdx_curr]; // shift to 16 bit
  if (currValue != lastValue)
    SET_PERI_REG_BITS(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, currValue, RTC_IO_PDAC1_DAC_S);
  lastValue = currValue;
  DataIdx_curr++;

  if (DataIdx_curr >= m_buffSize_curr) // if that was the last data byte...
  {
    DataIdx_curr = AUDIO_DATA_START;
    if (stop_wave_curr)
      curr_Wave->StopPlaying();
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
    timerAttachInterrupt(fixedTimer, &onSoundTimer, false); // edge (not level) triggered
    timerAlarmWrite(fixedTimer, fixedTimerTicks, true);    // autoreload true
    timerAlarmEnable(fixedTimer);                          // enable
  }
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
  log_i("Wave: %s", path);
  bool fsok = LittleFS.begin();
  if (fsok)
    log_i("LittleFS init: ok");
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
    log_i("Samplerate: %d Hz", rate);

    if (rdBytes != (m_buffSize + 1))
    {
      log_e("Failed to open file for reading");
    }
    log_i("Reading: %s", audioName);
    log_i("rdBytes: %d bytes", rdBytes);
    log_i("m_buffSize: %d bytes", m_buffSize);

    audiofile.close();
    log_i("Closing audio file");
  }
  LittleFS.end();
  vTaskDelay(2);
  dacWrite(DacPin, 0x00); // Set speaker to mid point to stop any clicks during sample playback
}

int Car_Audio_Wav_Class::PlayWav()
{
  if (WaveDataPtr)
  {
    log_i("Start PlayWav");
    vTaskDelay(2);
    m_buffSize_curr = m_buffSize;
    stop_wave_curr = stop_wave;
    curr_Wave = this;
    lastValue = 0x7F;
    dacWrite(DacPin, lastValue); // Set speaker to mid point to stop any clicks during sample playback
    noSound = false;
    WaveData = WaveDataPtr;
  }

  return 1;
}

void Car_Audio_Wav_Class::StopPlaying()
{
  noSound = true;
  dacWrite(DacPin, 0x00); // Set speaker to mid point to stop any clicks during sample playback
}
