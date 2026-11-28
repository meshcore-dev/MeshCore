#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
inline unsigned long test_millis = 0;
inline unsigned long millis() { return test_millis; }
