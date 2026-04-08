#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

#if defined(ARDUINO) && !defined(NATIVE_PLATFORM)
  // For Arduino, we need to handle this carefully since unity.h uses extern "C"
  // We'll declare the functions needed but avoid including Arduino.h here
  
  #ifdef __cplusplus
  extern "C" {
  #endif
  
  extern void unity_putchar_(int c);
  
  #ifdef __cplusplus
  }
  #endif
  
  #define UNITY_OUTPUT_CHAR(c)  unity_putchar_(c)
  #define UNITY_OUTPUT_FLUSH()  do {} while(0)
#endif

#endif // UNITY_CONFIG_H
