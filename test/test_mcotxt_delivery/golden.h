#pragma once

// Fixed MCOtxtFrame output from the independent Dart encoder in MCOa.
// Model generation 0; language pair RU/EN. Do not regenerate from the C++ decoder.
struct Golden { const char* text; const char* frame_hex; };

static const Golden kEnglish = {"Hello world!", "52208fbc1144d320a8b0b0c0"};
static const Golden kRussian = {"Привет мир!", "48208f09e724d0a4c4c3"};
static const Golden kReply = {"о, а вот тебе ответ о, а вот тебе ответ о, а вот тебе ответ о, а вот тебе ответ о, а вот тебе ответ о, а вот тебе ответ о, а вот тебе ответ о, а вот тебе ответ о, а вот тебе ответ", "d706208a784511dd34b8d31ca6a7258061144774d2e34c729a9c960184511dd34b8d31ca6a7258061144774d2e34c729a9c960184511dd34b8d31ca6a7258061144774d2e34c729a9c960184511dd34b8d31ca6a7258061144774d2e34c729a9c960184511dd34b8d31ca6a724"};
static const Golden kLong = {"аааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааааа", "e413208a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d1a3468d10"};
static const Golden kUnicode = {"😀 ёжик идёт домой — привет!", "c201208fd0fc27e6201b8761e2b499bc34d35426ac64093ce4b0c0"};
