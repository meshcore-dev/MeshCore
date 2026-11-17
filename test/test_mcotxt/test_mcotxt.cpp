#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include <helpers/mcotxt/MCOtxtCodec.h>
#include <helpers/mcotxt/MCOtxtModels.h>
#include <helpers/mcotxt/MCOtxtParts.h>
#include <helpers/mcotxt/MCOtxtTransport.h>

namespace {

// Most-significant bit first, the stream's own order.
class StreamWriter {
 public:
  void bits(uint32_t value, int count) {
    for (int i = count - 1; i >= 0; i--) {
      if (_bits % 8 == 0) _bytes.push_back(0);
      _bytes.back() |= (uint8_t)(((value >> i) & 1u) << (7 - (_bits % 8)));
      _bits++;
    }
  }
  void headerField(uint32_t value) {
    if (value < 7) {
      bits(value, 3);
    } else {
      bits(7, 3);
      bits(value - 7, 8);
    }
  }
  // version 1, generation [generation], inline languages.
  void header(uint8_t language_a, uint8_t language_b = 7, uint32_t generation = 0) {
    headerField(1);
    headerField(generation);
    bits(language_a, 3);
    bits(language_b, 3);
  }
  void top4(int rank) {
    switch (rank) {
      case 0: bits(0, 2); break;
      case 1: bits(2, 3); break;
      case 2: bits(6, 4); break;
      default: bits(7, 4); break;
    }
  }
  void primary(uint8_t id) { bits(2, 2); bits(id, 5); }
  void punctuation(uint8_t id) { bits(6, 3); bits(id, 5); }
  void extension(uint8_t id) { bits(14, 4); bits(id, 5); }
  void shift() { bits(30, 5); }
  void toggleLanguage() { bits(62, 6); }
  void toggleCase() { bits(63, 6); bits(3, 3); }
  void utf8Run(const std::vector<uint8_t>& run) {
    bits(63, 6);
    bits(2, 3);
    bits((uint32_t)(run.size() - 1), 5);
    for (uint8_t b : run) bits(b, 8);
  }
  void byte(uint8_t value) { bits(value, 8); }

  const uint8_t* data() const { return _bytes.data(); }
  size_t size() const { return _bytes.size(); }
  uint32_t bitLength() const { return _bits; }

 private:
  std::vector<uint8_t> _bytes;
  uint32_t _bits = 0;
};

std::string decode(const StreamWriter& w, mcotxt::DecodeStatus expected = mcotxt::DecodeStatus::Ok,
                   mcotxt::DecodeResult* result_out = nullptr) {
  char out[512];
  size_t length = 0;
  const mcotxt::DecodeResult result =
      mcotxt::decodeStream(w.data(), w.size(), w.bitLength(), out, sizeof(out), length);
  EXPECT_EQ((int)result.status, (int)expected);
  if (result_out) *result_out = result;
  return std::string(out, length);
}

std::string utf8(uint32_t codepoint) {
  uint8_t tmp[4];
  const size_t n = mcotxt::utf8Encode(codepoint, tmp);
  return std::string((const char*)tmp, n);
}

}  // namespace

// ---------------------------------------------------------------- models

TEST(MCOtxtModels, TablesMatchTheManifest) {
  const mcotxt::Model* en = mcotxt::modelFor(0);
  ASSERT_NE(en, nullptr);
  EXPECT_EQ(en->primary_count, 32);
  EXPECT_EQ(en->extension_count, 5);
  EXPECT_EQ(en->uppercase_count, 26);
  EXPECT_STREQ(en->wire_hash, "55988b3bb2a000adf6e768a8541df5a25fec1628b4ec661a10b577e3af8b3770");
  const mcotxt::Model* ru = mcotxt::modelFor(1);
  ASSERT_NE(ru, nullptr);
  EXPECT_EQ(ru->extension_count, 12);
  EXPECT_STREQ(ru->wire_hash, "d123c4978a635bf1b26fe37261b7e96a0206b3ba15b2a80542e560f00d1eb193");
  for (uint8_t id = 0; id < mcotxt::kLanguageCount; id++) {
    const mcotxt::Model* model = mcotxt::modelFor(id);
    ASSERT_NE(model, nullptr) << (int)id;
    EXPECT_EQ(model->language_id, id);
    EXPECT_EQ(model->symbolAt(0), 0x20u) << "SPACE is always primary[0]";
    for (uint8_t rank = 0; rank < 4; rank++) {
      EXPECT_LT(model->start_top4[rank], model->symbolCount());
      EXPECT_LT(model->punct_start_top4[rank], model->symbolCount());
    }
  }
  EXPECT_EQ(mcotxt::modelFor(7), nullptr);
  EXPECT_EQ(mcotxt::modelFor(255), nullptr);
}

TEST(MCOtxtModels, LookupsAgree) {
  const mcotxt::Model* en = mcotxt::modelFor(0);
  const int a = en->indexOf('a');
  ASSERT_GE(a, 0);
  EXPECT_EQ(en->indexOfUppercase('A'), a);
  EXPECT_EQ(en->uppercaseOf((uint8_t)a), 'A');
  EXPECT_EQ(en->uppercaseOf(0), 0u) << "SPACE has no uppercase form";
  EXPECT_EQ(en->indexOf(0x0439), -1) << "Cyrillic is not English";
  EXPECT_EQ(mcotxt::languageIdForCode("ru"), 1);
  EXPECT_EQ(mcotxt::languageIdForCode("BE"), 6);
  EXPECT_EQ(mcotxt::languageIdForCode("xx"), -1);
  EXPECT_EQ(mcotxt::languageIdForCode("eng"), -1);
  EXPECT_STREQ(mcotxt::languageCode(2), "fr");
  EXPECT_EQ(mcotxt::punctuationIdOf('.'), 1);
  EXPECT_EQ(mcotxt::punctuationIdOf(0x0A), 31);
  EXPECT_EQ(mcotxt::punctuationAt(8), 0x2014u);
  EXPECT_EQ(mcotxt::punctuationIdOf('1'), -1) << "digits are language symbols";
}

// --------------------------------------------------------------- decoder

TEST(MCOtxtDecoder, RawUtf8Mode) {
  StreamWriter w;
  w.headerField(1);
  w.headerField(0);
  w.bits(7, 3);
  w.bits(1, 3);
  w.bits(0, 4);
  EXPECT_EQ(w.bitLength(), 16u);
  for (char c : std::string("hi \xD0\xBF")) w.byte((uint8_t)c);
  mcotxt::DecodeResult result;
  EXPECT_EQ(decode(w, mcotxt::DecodeStatus::Ok, &result), "hi \xD0\xBF");
  EXPECT_TRUE(result.raw_utf8);
}

TEST(MCOtxtDecoder, RawUtf8RejectsPaddingAlignmentAndBadBytes) {
  {
    StreamWriter w;
    w.headerField(1); w.headerField(0); w.bits(7, 3); w.bits(1, 3); w.bits(1, 4);
    w.byte('a');
    decode(w, mcotxt::DecodeStatus::InvalidUtf8);
  }
  {
    StreamWriter w;
    w.headerField(1); w.headerField(0); w.bits(7, 3); w.bits(1, 3); w.bits(0, 4);
    w.byte('a'); w.bits(1, 3);
    decode(w, mcotxt::DecodeStatus::InvalidUtf8);
  }
  {
    StreamWriter w;
    w.headerField(1); w.headerField(0); w.bits(7, 3); w.bits(1, 3); w.bits(0, 4);
    w.byte(0xC0); w.byte(0x80);   // overlong NUL
    decode(w, mcotxt::DecodeStatus::InvalidUtf8);
  }
}

TEST(MCOtxtDecoder, RawUtf8IgnoresTheGeneration) {
  StreamWriter w;
  w.headerField(1); w.headerField(9); w.bits(7, 3); w.bits(1, 3); w.bits(0, 4);
  w.byte('x');
  EXPECT_EQ(decode(w), "x");
}

TEST(MCOtxtDecoder, HeaderRejections) {
  {
    StreamWriter w;
    w.header(0);
    w.bits(0, 0);
    // version 2
    StreamWriter v;
    v.headerField(2); v.headerField(0); v.bits(0, 3); v.bits(7, 3);
    decode(v, mcotxt::DecodeStatus::UnknownVersion);
  }
  {
    StreamWriter w;
    w.header(0, 7, 1);
    decode(w, mcotxt::DecodeStatus::UnsupportedGeneration);
  }
  {
    StreamWriter w;
    w.headerField(1); w.headerField(0); w.bits(7, 3); w.bits(2, 3);
    decode(w, mcotxt::DecodeStatus::UnsupportedHeader);
  }
  {
    StreamWriter w;
    w.header(0, 0);   // A and B must differ
    decode(w, mcotxt::DecodeStatus::UnknownLanguage);
  }
  {
    StreamWriter w;
    w.headerField(1); w.headerField(0); w.bits(7, 3);   // cut inside the header
    decode(w, mcotxt::DecodeStatus::UnexpectedEnd);
  }
}

TEST(MCOtxtDecoder, RejectsBitLengthLargerThanTheBufferWithoutOverflow) {
  const uint8_t data[] = {0};
  char out[8];
  size_t length = 0;
  const mcotxt::DecodeResult result =
      mcotxt::decodeStream(data, sizeof(data), UINT32_MAX, out, sizeof(out), length);
  EXPECT_EQ((int)result.status, (int)mcotxt::DecodeStatus::Malformed);
  EXPECT_EQ(length, 0u);
}

TEST(MCOtxtDecoder, ExtendedPairHeader) {
  StreamWriter w;
  w.headerField(1); w.headerField(0); w.bits(7, 3); w.bits(0, 3);
  w.bits(1, 8); w.bits(255, 8);
  w.primary(1);
  mcotxt::DecodeResult result;
  EXPECT_EQ(decode(w, mcotxt::DecodeStatus::Ok, &result), utf8(mcotxt::modelFor(1)->symbolAt(1)));
  EXPECT_EQ(result.language_a, 1);
  EXPECT_EQ(result.language_b, mcotxt::kLanguageNone);

  StreamWriter bad;
  bad.headerField(1); bad.headerField(0); bad.bits(7, 3); bad.bits(0, 3);
  bad.bits(255, 8); bad.bits(0, 8);
  decode(bad, mcotxt::DecodeStatus::UnknownLanguage);
}

TEST(MCOtxtDecoder, Top4FollowsTheContextRows) {
  for (uint8_t id = 0; id < mcotxt::kLanguageCount; id++) {
    const mcotxt::Model* model = mcotxt::modelFor(id);
    for (int rank = 0; rank < 4; rank++) {
      StreamWriter w;
      w.header(id);
      w.top4(rank);
      w.top4(0);
      const uint8_t first = model->start_top4[rank];
      const uint8_t second = model->rowFor(first)[0];
      EXPECT_EQ(decode(w), utf8(model->symbolAt(first)) + utf8(model->symbolAt(second)))
          << "language " << (int)id << " rank " << rank;
    }
  }
}

TEST(MCOtxtDecoder, LiteralsAndPunctuationContexts) {
  const mcotxt::Model* en = mcotxt::modelFor(0);
  StreamWriter w;
  w.header(0);
  w.primary(3);
  w.extension(0);
  w.punctuation(1);   // '.', then AFTER_PUNCT
  w.top4(0);
  w.punctuation(0);   // SPACE keeps the SYMBOL context
  w.top4(1);
  w.punctuation(31);  // LF gives START
  w.top4(2);
  const uint8_t after_punct = en->punct_start_top4[0];
  const uint8_t after_space = en->rowFor(after_punct)[1];
  const uint8_t after_lf = en->start_top4[2];
  EXPECT_EQ(decode(w), utf8(en->symbolAt(3)) + utf8(en->symbolAt(en->primary_count)) + "." +
                           utf8(en->symbolAt(after_punct)) + " " + utf8(en->symbolAt(after_space)) +
                           "\n" + utf8(en->symbolAt(after_lf)));

  // PRIMARY has a five-bit ID and every v1 model has all 32 entries, so an
  // out-of-range PRIMARY cannot be represented on the wire. EXTENSION IDs
  // still have unused values and exercise the decoder's range check.
  StreamWriter bad_extension;
  bad_extension.header(0);
  bad_extension.extension(5);   // EN has five extension symbols
  decode(bad_extension, mcotxt::DecodeStatus::InvalidToken);
}

TEST(MCOtxtDecoder, ShiftAndCapsMode) {
  const mcotxt::Model* en = mcotxt::modelFor(0);
  const uint8_t a = (uint8_t)en->indexOf('a');
  const uint8_t one = (uint8_t)en->indexOf('1');
  {
    StreamWriter w;
    w.header(0);
    w.shift(); w.primary(a); w.primary(a);
    w.toggleCase(); w.primary(a); w.shift(); w.primary(a); w.primary(a);
    w.toggleCase(); w.primary(a);
    EXPECT_EQ(decode(w), "AaAaAa");
  }
  {
    StreamWriter w;
    w.header(0);
    w.toggleCase(); w.primary(one);   // digits ignore caps mode
    EXPECT_EQ(decode(w), "1");
  }
  {
    StreamWriter w;
    w.header(0);
    w.shift(); w.primary(one);
    decode(w, mcotxt::DecodeStatus::InvalidShift);
  }
  {
    StreamWriter w;
    w.header(0);
    w.shift(); w.shift(); w.primary(a);
    decode(w, mcotxt::DecodeStatus::InvalidShift);
  }
  {
    StreamWriter w;
    w.header(0);
    w.shift(); w.punctuation(1);
    decode(w, mcotxt::DecodeStatus::InvalidShift);
  }
  {
    StreamWriter w;
    w.header(0);
    w.primary(a); w.shift();
    decode(w, mcotxt::DecodeStatus::InvalidShift);
  }
}

TEST(MCOtxtDecoder, ToggleLanguage) {
  const mcotxt::Model* ru = mcotxt::modelFor(1);
  StreamWriter w;
  w.header(0, 1);
  w.toggleLanguage(); w.primary(1);
  w.toggleLanguage(); w.primary(3);
  EXPECT_EQ(decode(w), utf8(ru->symbolAt(1)) + "a");

  StreamWriter without_b;
  without_b.header(0);
  without_b.toggleLanguage();
  decode(without_b, mcotxt::DecodeStatus::InvalidToken);
}

TEST(MCOtxtDecoder, SwitchOtherLanguageAndResetContext) {
  const mcotxt::Model* fr = mcotxt::modelFor(2);
  StreamWriter w;
  w.header(0);
  w.bits(63, 6); w.bits(0, 3); w.bits(2, 8);   // SWITCH_OTHER_LANGUAGE fr
  w.top4(0);
  w.bits(63, 6); w.bits(1, 3);                 // RESET_CONTEXT
  w.top4(1);
  EXPECT_EQ(decode(w), utf8(fr->symbolAt(fr->start_top4[0])) + utf8(fr->symbolAt(fr->start_top4[1])));

  StreamWriter unknown;
  unknown.header(0);
  unknown.bits(63, 6); unknown.bits(0, 3); unknown.bits(255, 8);
  decode(unknown, mcotxt::DecodeStatus::InvalidToken);

  StreamWriter reserved;
  reserved.header(0);
  reserved.bits(63, 6); reserved.bits(5, 3);
  decode(reserved, mcotxt::DecodeStatus::InvalidToken);
}

TEST(MCOtxtDecoder, Utf8Run) {
  StreamWriter w;
  w.header(0);
  w.utf8Run({0xF0, 0x9F, 0x98, 0x80});
  w.top4(0);
  const mcotxt::Model* en = mcotxt::modelFor(0);
  EXPECT_EQ(decode(w), std::string("\xF0\x9F\x98\x80") + utf8(en->symbolAt(en->start_top4[0])));

  StreamWriter bad;
  bad.header(0);
  bad.utf8Run({0xF0, 0x9F, 0x98});   // cut sequence
  decode(bad, mcotxt::DecodeStatus::InvalidUtf8);

  StreamWriter cut;
  cut.header(0);
  cut.bits(63, 6); cut.bits(2, 3); cut.bits(3, 5); cut.byte('a');
  decode(cut, mcotxt::DecodeStatus::UnexpectedEnd);
}

TEST(MCOtxtDecoder, PaddingIsNotSelfTerminating) {
  StreamWriter w;
  w.header(0);
  w.punctuation(1);   // 20 bits total; four zero padding bits are two TOP4 tokens
  char out[64];
  size_t length;
  // Reading the padded byte count instead of the bit count turns the padding
  // into TOP4 tokens: the caller must pass the exact bit length.
  mcotxt::decodeStream(w.data(), w.size(), (uint32_t)w.size() * 8, out, sizeof(out), length);
  EXPECT_GT(length, 1u);
  mcotxt::decodeStream(w.data(), w.size(), w.bitLength(), out, sizeof(out), length);
  EXPECT_EQ(std::string(out, length), ".");
}

TEST(MCOtxtDecoder, OutputCapacityIsHonoured) {
  StreamWriter w;
  w.header(0);
  for (int i = 0; i < 10; i++) w.primary(3);
  char out[6];
  size_t length;
  const mcotxt::DecodeResult result =
      mcotxt::decodeStream(w.data(), w.size(), w.bitLength(), out, sizeof(out), length);
  EXPECT_EQ((int)result.status, (int)mcotxt::DecodeStatus::OutputTooSmall);
  // The start that fit stays in the buffer, terminated.
  const mcotxt::Model* en = mcotxt::modelFor(0);
  ASSERT_NE(en, nullptr);
  std::string expected;
  while (expected.size() + utf8(en->symbolAt(3)).size() + 1 <= sizeof(out)) {
    expected += utf8(en->symbolAt(3));
  }
  EXPECT_EQ(std::string(out), expected);
  EXPECT_EQ(length, expected.size());

  // RAW_UTF8 cuts on a character boundary too.
  StreamWriter raw;
  raw.headerField(1);
  raw.headerField(0);
  raw.bits(7, 3);
  raw.bits(1, 3);
  raw.bits(0, 4);
  for (char c : std::string("h\xC3\xA9llo")) raw.byte((uint8_t)c);
  char small[4];
  const mcotxt::DecodeResult cut =
      mcotxt::decodeStream(raw.data(), raw.size(), raw.bitLength(), small, sizeof(small), length);
  EXPECT_EQ((int)cut.status, (int)mcotxt::DecodeStatus::OutputTooSmall);
  EXPECT_TRUE(cut.raw_utf8);
  EXPECT_EQ(std::string(small), "h\xC3\xA9");
  EXPECT_EQ(length, 3u);

  // And a UTF8_RUN keeps the characters that fit.
  StreamWriter run;
  run.header(0);
  run.utf8Run({'x', 'y', 'z'});
  char two[3];
  const mcotxt::DecodeResult part =
      mcotxt::decodeStream(run.data(), run.size(), run.bitLength(), two, sizeof(two), length);
  EXPECT_EQ((int)part.status, (int)mcotxt::DecodeStatus::OutputTooSmall);
  EXPECT_EQ(std::string(two), "xy");
}

// ----------------------------------------------------------------- parts

namespace {

std::string partText(const char* prefix, const std::string& text, const mcotxt::TextPart* parts,
                     size_t index, size_t count) {
  char out[256];
  const size_t length = mcotxt::formatPart(prefix, strlen(prefix), text.data(), parts[index],
                                           index, count, out, sizeof(out));
  return std::string(out, length);
}

}  // namespace

TEST(MCOtxtParts, OneMessageWhenItFits) {
  const std::string text = "hello world";
  mcotxt::TextPart parts[mcotxt::kMaxParts];
  ASSERT_EQ(mcotxt::splitForApp(text.data(), text.size(), 40, 5, parts, mcotxt::kMaxParts), 1u);
  EXPECT_EQ(partText("Bob: ", text, parts, 0, 1), "Bob: hello world");
  // Exactly the budget still goes as one; one byte more does not.
  const std::string full(35, 'x');
  EXPECT_EQ(mcotxt::splitForApp(full.data(), full.size(), 40, 5, parts, mcotxt::kMaxParts), 1u);
  const std::string over(36, 'x');
  EXPECT_GT(mcotxt::splitForApp(over.data(), over.size(), 40, 5, parts, mcotxt::kMaxParts), 1u);
  EXPECT_EQ(mcotxt::splitForApp(text.data(), 0, 40, 5, parts, mcotxt::kMaxParts), 0u);
}

TEST(MCOtxtParts, BreaksAtSpacesAndNumbersTheParts) {
  std::string text;
  for (int i = 1; i <= 30; i++) {
    if (!text.empty()) text += ' ';
    char word[12];
    snprintf(word, sizeof(word), "word%02d", i);
    text += word;
  }
  mcotxt::TextPart parts[mcotxt::kMaxParts];
  const size_t count = mcotxt::splitForApp(text.data(), text.size(), 40, 5, parts, mcotxt::kMaxParts);
  ASSERT_GT(count, 1u);
  std::string joined;
  for (size_t i = 0; i < count; i++) {
    const std::string part = partText("Bob: ", text, parts, i, count);
    EXPECT_LE(part.size(), 40u) << part;
    const std::string head = i == 0 ? "Bob: " : "Bob: ...";
    EXPECT_EQ(part.compare(0, head.size(), head), 0) << part;
    char mark[16];
    snprintf(mark, sizeof(mark), "%s [%u/%u]", i + 1 < count ? "..." : "",
             (unsigned)(i + 1), (unsigned)count);
    ASSERT_GT(part.size(), strlen(mark));
    EXPECT_EQ(part.compare(part.size() - strlen(mark), strlen(mark), mark), 0) << part;
    const std::string chunk = text.substr(parts[i].offset, parts[i].length);
    EXPECT_EQ(part, head + chunk + mark);
    EXPECT_NE(chunk.front(), ' ');
    EXPECT_NE(chunk.back(), ' ');
    if (!joined.empty()) joined += ' ';
    joined += chunk;
  }
  EXPECT_EQ(joined, text) << "chunks only break at spaces, so the words come back whole";
}

TEST(MCOtxtParts, FirstAndMiddlePartsEndWithEllipsesButLastDoesNot) {
  const std::string text = "abcdefghijklmnopqrstuvwx";
  mcotxt::TextPart parts[mcotxt::kMaxParts];
  const size_t count = mcotxt::splitForApp(text.data(), text.size(), 24, 5, parts, mcotxt::kMaxParts);
  ASSERT_EQ(count, 3u);
  EXPECT_EQ(partText("Bob: ", text, parts, 0, count), "Bob: abcdefghij... [1/3]");
  EXPECT_EQ(partText("Bob: ", text, parts, 1, count), "Bob: ...klmnopq... [2/3]");
  EXPECT_EQ(partText("Bob: ", text, parts, 2, count), "Bob: ...rstuvwx [3/3]");
}

TEST(MCOtxtParts, LastPartUsesFullBudgetWithoutReservingTrailingEllipses) {
  const std::string text = "abcdefghijklmnopqrst";
  mcotxt::TextPart parts[mcotxt::kMaxParts];
  const size_t count = mcotxt::splitForApp(text.data(), text.size(), 24, 5, parts, mcotxt::kMaxParts);
  ASSERT_EQ(count, 2u);
  EXPECT_EQ(partText("Bob: ", text, parts, 0, count), "Bob: abcdefghij... [1/2]");
  EXPECT_EQ(partText("Bob: ", text, parts, 1, count), "Bob: ...klmnopqrst [2/2]");
  EXPECT_EQ(partText("Bob: ", text, parts, 0, count).size(), 24u);
  EXPECT_EQ(partText("Bob: ", text, parts, 1, count).size(), 24u);
}

TEST(MCOtxtParts, CutsOnCharacterBoundariesWithoutSpaces) {
  std::string text;
  for (int i = 0; i < 60; i++) text += "\xD0\xB0";   // 120 bytes, no breaks
  mcotxt::TextPart parts[mcotxt::kMaxParts];
  const size_t count = mcotxt::splitForApp(text.data(), text.size(), 32, 0, parts, mcotxt::kMaxParts);
  ASSERT_GT(count, 1u);
  std::string joined;
  for (size_t i = 0; i < count; i++) {
    EXPECT_EQ(parts[i].length % 2, 0u) << "a two-byte letter is never split";
    EXPECT_LE(partText("", text, parts, i, count).size(), 32u);
    joined += text.substr(parts[i].offset, parts[i].length);
  }
  EXPECT_EQ(joined, text);
}

TEST(MCOtxtParts, MarksGrowWithTheCountAndTooManyPartsFail) {
  const std::string text(400, 'a');   // no breaks: every part fills its budget
  mcotxt::TextPart parts[mcotxt::kMaxParts];
  const size_t count = mcotxt::splitForApp(text.data(), text.size(), 40, 0, parts, mcotxt::kMaxParts);
  ASSERT_GE(count, 10u) << "two-digit marks were planned for";
  std::string joined;
  for (size_t i = 0; i < count; i++) {
    EXPECT_LE(partText("", text, parts, i, count).size(), 40u);
    joined += text.substr(parts[i].offset, parts[i].length);
  }
  EXPECT_EQ(joined, text);
  const std::string last = partText("", text, parts, count - 1, count);
  const std::string mark = " [" + std::to_string(count) + "/" + std::to_string(count) + "]";
  EXPECT_EQ(last.substr(last.size() - mark.size()), mark);
  EXPECT_EQ(mcotxt::splitForApp(text.data(), text.size(), 40, 0, parts, 4), 0u);
  // A prefix that leaves no room fails too.
  EXPECT_EQ(mcotxt::splitForApp(text.data(), text.size(), 40, 40, parts, mcotxt::kMaxParts), 0u);
}

TEST(MCOtxtParts, SplitPartsAlwaysFormatInsideFrameTextBudget) {
  const std::string prefix = "LongestVisibleSenderName: ";
  std::string text;
  for (int i = 0; i < 80; i++) {
    if (!text.empty()) text += ' ';
    text += "message";
  }
  constexpr size_t kFrameTextBudget = 167;   // MAX_FRAME_SIZE - CHANNEL_MSG_RECV_V3 header
  mcotxt::TextPart parts[mcotxt::kMaxParts];
  const size_t count = mcotxt::splitForApp(text.data(), text.size(), kFrameTextBudget,
                                           prefix.size(), parts, mcotxt::kMaxParts);
  ASSERT_GT(count, 1u);
  char out[256];
  std::string joined;
  for (size_t i = 0; i < count; i++) {
    const size_t length = mcotxt::formatPart(prefix.c_str(), prefix.size(), text.data(),
                                             parts[i], i, count, out, sizeof(out));
    ASSERT_GT(length, 0u);
    EXPECT_LE(length, kFrameTextBudget);
    const std::string chunk = text.substr(parts[i].offset, parts[i].length);
    if (!joined.empty()) joined += ' ';
    joined += chunk;
  }
  EXPECT_EQ(joined, text);
}

// ------------------------------------------------------------- transport

TEST(MCOtxtTransport, TextPayloadRecognition) {
  EXPECT_TRUE(mcotxt::isTextPayload("mct:abc"));
  EXPECT_TRUE(mcotxt::isTextPayload("  mct:abc"));
  EXPECT_FALSE(mcotxt::isTextPayload("mct:"));
  EXPECT_FALSE(mcotxt::isTextPayload("mcmp3:abc"));
  EXPECT_FALSE(mcotxt::isTextPayload("hello"));
  EXPECT_FALSE(mcotxt::isTextPayload(nullptr));
}

TEST(MCOtxtTransport, BinaryEnvelopeDecodes) {
  // senderNameLength 0 | 0x31 | flags 0x02 | timestamp | name (UTF-8 string) |
  // text (UTF-8 string).
  std::vector<uint8_t> envelope = {0x00, 0x31, 0x02, 0x78, 0x56, 0x34, 0x12,
                                   0x01, 0x03, 'B', 'o', 'b',
                                   0x01, 0x02, 'h', 'i'};
  char out[64];
  mcotxt::DecodedMessage message;
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, envelope.data(), envelope.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::Ok);
  EXPECT_STREQ(out, "hi");
  EXPECT_TRUE(message.has_sender);
  EXPECT_STREQ(message.sender, "Bob");
  EXPECT_TRUE(message.has_timestamp);
  EXPECT_EQ(message.timestamp, 0x12345678u);
  EXPECT_FALSE(message.has_reply);

  // The envelope name is the fallback when the container carries none.
  std::vector<uint8_t> named = {0x03, 'A', 'n', 'n', 0x31, 0x04, 0x01, 0x02, 'h', 'i'};
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, named.data(), named.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::Ok);
  EXPECT_STREQ(message.sender, "Ann");
  EXPECT_FALSE(message.has_timestamp);

  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, envelope.data(), envelope.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::Ok);
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0xAE1C, envelope.data(), envelope.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::NotMCOtxt);
  std::vector<uint8_t> image = {0x00, 0x13, 0x00};
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, image.data(), image.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::NotMCOtxt);
  std::vector<uint8_t> newer = {0x00, 0x32, 0x04, 0x01, 0x02, 'h', 'i'};
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, newer.data(), newer.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::UnsupportedVersion);
  EXPECT_EQ(message.declared_version, 2);
  std::vector<uint8_t> trailing = {0x00, 0x31, 0x04, 0x01, 0x02, 'h', 'i', 0x00};
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, trailing.data(), trailing.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::Malformed);
  std::vector<uint8_t> flags = {0x00, 0x31, 0x84, 0x01, 0x02, 'h', 'i'};
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, flags.data(), flags.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::Malformed);
}

TEST(MCOtxtTransport, BinaryEnvelopeRecognition) {
  std::vector<uint8_t> ok = {0x00, 0x31, 0x04, 0x01, 0x02, 'h', 'i'};
  EXPECT_TRUE(mcotxt::isBinaryEnvelope(0x0120, ok.data(), ok.size()));
  std::vector<uint8_t> named = {0x03, 'B', 'o', 'b', 0x32};   // any revision counts
  EXPECT_TRUE(mcotxt::isBinaryEnvelope(0x0120, named.data(), named.size()));
  std::vector<uint8_t> other = {0x00, 0x41};   // another subtype
  EXPECT_FALSE(mcotxt::isBinaryEnvelope(0x0120, other.data(), other.size()));
  EXPECT_FALSE(mcotxt::isBinaryEnvelope(0x0121, ok.data(), ok.size()));
  std::vector<uint8_t> cut = {0x05, 'B'};   // name longer than the payload
  EXPECT_FALSE(mcotxt::isBinaryEnvelope(0x0120, cut.data(), cut.size()));
  const std::vector<uint8_t> huge_name = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x31};
  EXPECT_FALSE(mcotxt::isBinaryEnvelope(0x0120, huge_name.data(), huge_name.size()));
  char out[8];
  mcotxt::DecodedMessage message;
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, huge_name.data(), huge_name.size(),
                                               out, sizeof(out), message),
            (int)mcotxt::MessageStatus::NotMCOtxt);
  EXPECT_FALSE(mcotxt::isBinaryEnvelope(0x0120, nullptr, 0));
}

TEST(MCOtxtTransport, RejectsOverflowingMCOtxtBitLength) {
  // outer name 0 | subtype v1 | inherited timestamp | MCOtxt mode |
  // bit_length UINT32_MAX. No stream bytes follow.
  const std::vector<uint8_t> envelope = {
      0x00, 0x31, 0x04, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
  char out[8];
  mcotxt::DecodedMessage message;
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, envelope.data(), envelope.size(),
                                               out, sizeof(out), message),
            (int)mcotxt::MessageStatus::Malformed);
}

TEST(MCOtxtTransport, TooLongKeepsTheStart) {
  // A UTF-8 mode text into a buffer for three bytes.
  std::vector<uint8_t> plain = {0x00, 0x31, 0x04, 0x01, 0x05, 'h', 'e', 'l', 'l', 'o'};
  char out[4];
  mcotxt::DecodedMessage message;
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, plain.data(), plain.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::TooLong);
  EXPECT_STREQ(out, "hel");
  // A failed text decode does not claim a successfully parsed envelope sender.
  std::vector<uint8_t> named = {0x03, 'B', 'o', 'b', 0x31, 0x04, 0x01, 0x05,
                                'h', 'e', 'l', 'l', 'o'};
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, named.data(), named.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::TooLong);
  EXPECT_FALSE(message.has_sender);
}

TEST(MCOtxtTransport, ReplyAnchorNeedsBothHalves) {
  std::vector<uint8_t> reply = {0x00, 0x31, 0x05, 0x01, 0x03, 'B', 'o', 'b',
                                0x10, 0x00, 0x00, 0x00, 0x01, 0x02, 'o', 'k'};
  char out[64];
  mcotxt::DecodedMessage message;
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, reply.data(), reply.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::Ok);
  EXPECT_TRUE(message.has_reply);
  EXPECT_STREQ(message.reply_author, "Bob");
  EXPECT_EQ(message.reply_timestamp, 0x10u);
  EXPECT_STREQ(out, "ok");

  bool truncated = false;
  EXPECT_TRUE(mcotxt::ensureReplyMentionPrefix(message, out, sizeof(out), truncated));
  EXPECT_FALSE(truncated);
  EXPECT_STREQ(out, "@[Bob] ok");
  EXPECT_TRUE(mcotxt::ensureReplyMentionPrefix(message, out, sizeof(out), truncated));
  EXPECT_FALSE(truncated);
  EXPECT_STREQ(out, "@[Bob] ok");

  std::vector<uint8_t> cut = {0x00, 0x31, 0x05, 0x01, 0x03, 'B', 'o', 'b', 0x10, 0x00};
  EXPECT_EQ((int)mcotxt::decodeBinaryEnvelope(0x0120, cut.data(), cut.size(), out,
                                                  sizeof(out), message),
            (int)mcotxt::MessageStatus::Malformed);
}

TEST(MCOtxtTransport, TextPayloadRejectsGarbage) {
  char out[64];
  mcotxt::DecodedMessage message;
  EXPECT_EQ((int)mcotxt::decodeTextPayload("mct:!!!\x01", out, sizeof(out), message),
            (int)mcotxt::MessageStatus::Malformed);
  EXPECT_EQ((int)mcotxt::decodeTextPayload("plain text", out, sizeof(out), message),
            (int)mcotxt::MessageStatus::NotMCOtxt);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
