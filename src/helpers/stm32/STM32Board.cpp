#if defined(STM32_PLATFORM)
#include "STM32Board.h"

#if defined(STM32WLxx)
#include <SubGhz.h>

// Low-power sleep for the STM32WL. The core enters a Stop mode while the on-chip
// radio keeps receiving, so either an incoming frame (through the radio's IRQ on
// EXTI line 44) or a periodic RTC timer wakes it again. Stop mode halts SysTick,
// so on wake millis() is advanced by the elapsed time read back from the RTC.

extern "C" __IO uint32_t uwTick;            // the tick counter behind millis()
extern "C" void SystemClock_Config(void);   // provided by the board variant

static RTC_HandleTypeDef s_hrtc;
static bool s_rtc_ready = false;

// Acknowledge the wake-up timer interrupt; the weak default handler would trap.
extern "C" void RTC_WKUP_IRQHandler(void) {
  HAL_RTCEx_WakeUpTimerIRQHandler(&s_hrtc);
}

static bool stm32wl_rtc_begin() {
  if (s_rtc_ready) return true;

  HAL_PWR_EnableBkUpAccess();

  __HAL_RCC_LSI_ENABLE();
  while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET) { }

  __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSI);
  __HAL_RCC_RTCAPB_CLK_ENABLE();
  __HAL_RCC_RTC_ENABLE();

  s_hrtc.Instance = RTC;
  s_hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  s_hrtc.Init.AsynchPrediv = 127;   // the LSI runs at ~32 kHz, so /128 gives 250 Hz
  s_hrtc.Init.SynchPrediv = 249;    // and /250 yields a 1 Hz ck_spre
  s_hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  s_hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  s_hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  s_hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  if (HAL_RTC_Init(&s_hrtc) != HAL_OK) return false;

  HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 15, 0);
  HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);

  s_rtc_ready = true;
  return true;
}

static void stm32wl_rtc_zero() {
  RTC_TimeTypeDef t = {};
  HAL_RTC_SetTime(&s_hrtc, &t, RTC_FORMAT_BIN);
}

static uint32_t stm32wl_rtc_elapsed_ms() {
  RTC_TimeTypeDef t;
  RTC_DateTypeDef d;
  HAL_RTC_GetTime(&s_hrtc, &t, RTC_FORMAT_BIN);
  // Reading the date is mandatory after the time: it releases the shadow registers
  // that the time read locks.
  HAL_RTC_GetDate(&s_hrtc, &d, RTC_FORMAT_BIN);

  uint32_t ms = ((t.Hours * 60UL + t.Minutes) * 60UL + t.Seconds) * 1000UL;
  if (t.SecondFraction) {
    ms += ((t.SecondFraction - t.SubSeconds) * 1000UL) / (t.SecondFraction + 1);
  }
  return ms;
}

void STM32Board::sleep(uint32_t secs) {
  // A frame that arrived since the last loop() should be handled, not slept through.
  if (SubGhz.isInterruptPending()) return;

  bool timed = (secs > 0) && stm32wl_rtc_begin();

  // The radio's interrupt is wired to EXTI line 44; unmasking it lets an incoming
  // frame wake the core from Stop.
  LL_EXTI_EnableIT_32_63(LL_EXTI_LINE_44);

  if (timed) {
    stm32wl_rtc_zero();
    HAL_RTCEx_SetWakeUpTimer_IT(&s_hrtc, secs - 1, RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0);
  }

  // Keep interrupts masked across the transition so that the waking ISR runs only
  // after SystemClock_Config() has restored the clock, rather than at the slower
  // MSI range that the core wakes into.
  __disable_irq();

  // Re-check now that interrupts are masked. Had a frame arrived during the setup
  // above, its ISR would already have run and disabled the radio interrupt in the
  // NVIC, and a disabled but pending interrupt cannot wake WFI. The request is still
  // asserted on EXTI line 44, so bail out rather than sleep through it.
  if (SubGhz.isInterruptPending()) {
    if (timed) HAL_RTCEx_DeactivateWakeUpTimer(&s_hrtc);
    __enable_irq();
    return;
  }

  HAL_SuspendTick();

  // For a node that stays in continuous RX, Stop0 is the lower-power choice even
  // though it is the shallower sleep. Stop1, Stop2 and Standby force the SMPS into
  // bypass, which drops the radio back onto the LDO, whereas Stop0 keeps the SMPS
  // running and the radio on DC-DC. The radio it saves outweighs the slightly
  // higher core current, so idle-RX firmware should build with STM32WL_SLEEP_STOP0.
#if defined(STM32WL_SLEEP_STOP0)
  HAL_PWREx_EnterSTOP0Mode(PWR_STOPENTRY_WFI);
#else
  HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
#endif

  // A Stop mode always exits on the MSI clock, so restore the configured clock tree.
  SystemClock_Config();
  HAL_ResumeTick();

  if (timed) {
    HAL_RTCEx_DeactivateWakeUpTimer(&s_hrtc);
    uwTick += stm32wl_rtc_elapsed_ms();
  }

  __enable_irq();
}

// Enable the SMPS step-down so that the core and on-chip radio draw from the DC-DC
// regulator rather than the LDO, which roughly halves the radio's RX current. The
// board must have the SMPS coil on VLXSMPS populated. Stop1, Stop2 and Standby force
// the converter back to bypass, so the saving only applies in Run, LP-Run and Stop0.
void STM32Board::lowPowerInit() {
#if defined(STM32WL_ENABLE_SMPS)
  SET_BIT(PWR->CR5, PWR_CR5_SMPSEN);
#endif
}

#else  // other STM32 parts have no on-chip radio to wake on, so there is nothing to do

void STM32Board::sleep(uint32_t secs) {
  (void)secs;
}

void STM32Board::lowPowerInit() { }

#endif  // STM32WLxx
#endif  // STM32_PLATFORM
