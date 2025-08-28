#pragma once

#ifndef FIRMWARE_VER_CODE
    #define FIRMWARE_VER_CODE 7
#endif

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "Not Set"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1.7.4?"
#endif

#ifndef ADVERT_LAT
  #define  ADVERT_LAT  0.0
#endif

#ifndef ADVERT_LON
  #define  ADVERT_LON  0.0
#endif

#ifndef ADMIN_PASSWORD
  #define  ADMIN_PASSWORD  "password"
#endif

#define PACKET_LOG_FILE  "/packet_log"


//================ Radio Settings ================

//Australia
#if REGION_AS
    #undef LORA_FREQ
    #define LORA_FREQ   915.800

    #undef LORA_SF
    #define LORA_SF     10
#endif

//EU 433Mhz
#if REGION_EU_433
    #undef LORA_FREQ
    #define LORA_FREQ   433.650
#endif

//New Zealand
#if REGION_NZ
    #undef LORA_FREQ
    #define LORA_FREQ   917.375
#endif

//Portugal
#if REGION_PO
    #undef LORA_FREQ
    #define LORA_FREQ   433.375

    #undef LORA_SF
    #define LORA_SF     9

    #undef LORA_BW 
    #define LORA_BW     62.5

    #undef LORA_CR
    #define LORA_CR      6
#endif

//Switzerland
#if REGION_SZ
    //Same as EU / UK
#endif

//USA
#if REGION_US
    #undef LORA_FREQ
    #define LORA_FREQ   910.525
#endif

#if MED_RANGE
  #undef LORA_SF
  #define LORA_SF     10
#endif

#if NARROW
  #undef LORA_BW 
  #define LORA_BW     62.5
  #undef LORA_SF
  #define LORA_SF     7
#endif


//EU - UK Long Range
#ifndef LORA_FREQ
  #define LORA_FREQ   869.525
#endif

#ifndef LORA_BW
  #define LORA_BW     250
#endif

#ifndef LORA_SF
  #define LORA_SF     11
#endif

#ifndef LORA_CR
  #define LORA_CR      5
#endif

#ifndef LORA_PREAMBLE
  #define LORA_PREAMBLE 16
#endif

#ifndef LORA_TX_POWER
  #define LORA_TX_POWER  20
#endif

#ifndef MAX_LORA_TX_POWER
    #define MAX_LORA_TX_POWER LORA_TX_POWER
#endif