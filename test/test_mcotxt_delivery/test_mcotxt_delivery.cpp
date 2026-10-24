#include "fixtures.h"

TEST(MCOtxtInterop, DartFramesDecodeExactly) {
  for (const Golden* golden : {&kEnglish, &kRussian, &kReply, &kLong, &kUnicode}) {
    Bytes envelope = {0};
    const Bytes body = container(*golden);
    envelope.insert(envelope.end(), body.begin(), body.end());
    char output[2048];
    mcotxt::DecodedMessage metadata;
    EXPECT_EQ(mcotxt::decodeBinaryEnvelope(0x0120, envelope.data(), envelope.size(), output,
                                           sizeof(output), metadata), mcotxt::MessageStatus::Ok);
    EXPECT_STREQ(output, golden->text);
  }
}

TEST(MCOtxtDelivery, CapabilityIsAnExactCaseSensitiveItemNotAnAppName) {
  const Bytes raw = channel("Bob: " + textToken(container(kRussian)));
  for (const std::string info : {"cap=mctxt", "Any;cap=mctxt", "Other;cap=mctxt,other",
                                 "Other;cap=other,mctxt,another", "Other;cap=other,mctxt",
                                 "a;cap=other;cap=mctxt", "a;cap=;cap=mctxt",
                                 "a;cap=,other,,mctxt,;version=1"}) {
    SCOPED_TRACE(info);
    MCOtxtDelivery d; start(d, info);
    EXPECT_EQ(drain(d, raw), std::vector<Bytes>{raw});
  }
  for (const std::string info : {"", "MeshCoreOpen", "cap=", "a;cap=;version=1", "a;cap=,,,",
                                 "a;cap=other,another", "a;cap=mctxt2", "a;cap=MCTXT",
                                 "a;mctxt", "a;cap=xmctxt", "a;othercap=mctxt"}) {
    SCOPED_TRACE(info);
    MCOtxtDelivery d; start(d, info);
    auto frames = drain(d, raw);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(bodyText(frames[0]), "Bob: Привет мир!");
  }
}

TEST(MCOtxtDelivery, CapabilityParsingStopsAtFieldLengthAndNul) {
  const Bytes raw = channel("Bob: " + textToken(container(kRussian)));
  const std::string info = "Any;cap=other,mctxt";
  MCOtxtDelivery d;
  // The buffer contains mctxt, but it is outside the supplied APP_START length.
  d.appStart((const uint8_t*)info.data(), info.size() - 1);
  auto frames = drain(d, raw);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(bodyText(frames[0]), "Bob: Привет мир!");

  const uint8_t terminated[] = "Any;cap=\0mctxt";
  d.appStart(terminated, sizeof(terminated) - 1);
  frames = drain(d, raw);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(bodyText(frames[0]), "Bob: Привет мир!");

  d.appStart(nullptr, 0);
  frames = drain(d, raw);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(bodyText(frames[0]), "Bob: Привет мир!");
}

TEST(MCOtxtDelivery, PlainMessagesAndCliAreUntouched) {
  MCOtxtDelivery d;
  for (const Bytes raw : {channel("Bob: ordinary text"), direct(textToken(container(kRussian)), 1)}) {
    EXPECT_EQ(drain(d, raw), std::vector<Bytes>{raw});
    d.resetMessage();
  }
}

TEST(MCOtxtDelivery, ReplyMentionOnlyInFirstNumberedPartAndOriginalTimestampOnEveryPart) {
  const Bytes raw = channel("Bob: " + textToken(container(kReply, "", "Ann")));
  MCOtxtDelivery d;
  auto frames = drain(d, raw);
  ASSERT_GT(frames.size(), 1u);
  EXPECT_EQ(bodyText(frames[0]).find("Bob: @[Ann] "), 0u);
  for (size_t i = 0; i < frames.size(); ++i) {
    EXPECT_EQ(timestamp(frames[i]), 0x11223344u);
    EXPECT_EQ(frames[i][4], 2); EXPECT_EQ(frames[i][5], 0x82);
    if (i > 0) EXPECT_EQ(bodyText(frames[i]).find("@[Ann]"), std::string::npos);
  }
  EXPECT_EQ(withoutSpaces(reassemble(frames, "Bob: ")), withoutSpaces(std::string("@[Ann] ") + kReply.text));
}

TEST(MCOtxtDelivery, EmbeddedContainersPreserveProsePunctuationAndMultipleTokens) {
  const std::string token = textToken(container(kRussian));
  MCOtxtDelivery d;
  const auto frames = drain(d, channel("Bob: цитата («" + token + "»), then " + textToken(container(kEnglish)) + "."));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(bodyText(frames[0]), "Bob: цитата («Привет мир!»), then Hello world!.");
}

TEST(MCOtxtDelivery, InlinePunctuationCannotChangeTheLastDecodedCharacter) {
  // Regression: the trailing '.' used to be accepted as a noncanonical Base91
  // tail, changing the final compressed '!' to SPACE and swallowing the dot.
  const std::string input = "quote: mct:3O2W>2lB8FIA/BPZ{Y)kxL>uwPw.";
  const std::string expected = "@[N] quote: Hello world!.";
  MCOtxtDelivery delivery;
  const auto frames = drain(delivery, channel("Bob: " + input));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(bodyText(frames[0]), "Bob: " + expected);
  EXPECT_EQ(std::string(mcotxt::displayText(("Bob: " + input).c_str(), true)), "Bob: " + expected);
}

TEST(MCOtxtDelivery, AdjacentBase91PunctuationIsPreservedAcrossTokenEndings) {
  for (const Golden* golden : {&kEnglish, &kRussian, &kUnicode}) {
    for (size_t n = 0; n <= 31; ++n) {
      const std::string name(n, 'N');
      for (bool reply : {false, true}) {
        const std::string token = textToken(reply ? container(*golden, "", name)
                                                  : container(*golden, name));
        for (const std::string punctuation : {".", ",", ")", "]", "!", "?", ";", ":", "\"", ")...", "]!"}) {
          SCOPED_TRACE(token + punctuation);
          const std::string input = "quote: (" + token + punctuation;
          const std::string expected = (reply && !name.empty() ? "@[" + name + "] " : "") +
                                      "quote: (" + golden->text + punctuation;
          char output[mcotxt::kScratchBytes];
          ASSERT_TRUE(mcotxt::decodeText(input.c_str(), output, sizeof(output)));
          ASSERT_EQ(std::string(output), expected);
        }
      }
    }
  }
}

TEST(MCOtxtDelivery, NoncanonicalBase91AndNonzeroStreamPaddingAreRejected) {
  char output[mcotxt::kScratchBytes];
  mcotxt::DecodedMessage metadata;
  const std::string token = textToken(container(kEnglish, "NNNNN"));
  EXPECT_EQ(mcotxt::decodeTextPayload((token + ".").c_str(), output, sizeof(output), metadata),
            mcotxt::MessageStatus::Malformed);
  // The independent English stream has 82 significant bits; these low bits
  // are padding even though Base91 can canonically encode them as nonzero.
  Bytes body = container(kEnglish);
  body.back() |= 1;
  EXPECT_EQ(mcotxt::decodeTextPayload(textToken(body).c_str(), output, sizeof(output), metadata),
            mcotxt::MessageStatus::Malformed);
  MCOtxtDelivery delivery;
  const Bytes raw = binary(body);
  EXPECT_EQ(drain(delivery, raw), std::vector<Bytes>{raw});
}

TEST(MCOtxtNames, Full31ByteNamesAndReplyRemainUnchangedAcrossParts) {
  const std::string name(31, 'N');
  const std::string prefix = name + ": ";
  MCOtxtDelivery delivery;
  auto frames = drain(delivery, channel(prefix + textToken(container(kEnglish, "", name))));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(bodyText(frames[0]), prefix + "@[" + name + "] Hello world!");
  delivery.resetMessage();
  frames = drain(delivery, binary(container(kReply, name)));
  ASSERT_GT(frames.size(), 2u);
  frames.erase(frames.begin());
  EXPECT_EQ(withoutSpaces(reassemble(frames, prefix)), withoutSpaces(kReply.text));
}

TEST(MCOtxtNames, InvalidUtf8BeyondTheClippedContainerNameIsStillRejected) {
  const Bytes raw = binary(container(kEnglish, std::string(32, 'N') + "\xc0\x80"));
  MCOtxtDelivery delivery;
  EXPECT_EQ(drain(delivery, raw), std::vector<Bytes>{raw});
}

TEST(MCOtxtDelivery, EmbeddedReplyIsMovedToBeginningBeforeSplitting) {
  MCOtxtDelivery d;
  const auto frames = drain(d, channel("Bob: цитата: " + textToken(container(kRussian, "", "Ann"))));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(bodyText(frames[0]), "Bob: @[Ann] цитата: Привет мир!");
}

TEST(MCOtxtDelivery, TokenMustStartAtAWordBoundary) {
  const std::string token = textToken(container(kRussian));
  for (const std::string prefix : {"word", "слово", "_", "9"}) {
    MCOtxtDelivery d;
    const Bytes raw = channel("Bob: " + prefix + token);
    EXPECT_EQ(drain(d, raw), std::vector<Bytes>{raw});
  }
}

TEST(MCOtxtDelivery, RawBinaryFirstThenDecodedTextWithContainerSenderAndReply) {
  const Bytes raw = binary(container(kReply, "Bob", "Ann", false));
  MCOtxtDelivery d;
  auto frames = drain(d, raw);
  ASSERT_GT(frames.size(), 2u);
  EXPECT_EQ(frames[0], raw);
  frames.erase(frames.begin());
  for (const Bytes& frame : frames) EXPECT_EQ(timestamp(frame), 0x12345678u);
  EXPECT_EQ(withoutSpaces(reassemble(frames, "Bob: ")), withoutSpaces(std::string("@[Ann] ") + kReply.text));
}

TEST(MCOtxtDelivery, CapableClientGetsOnlyOriginalBinary) {
  MCOtxtDelivery d; start(d, "ArbitraryClient;cap=mctxt");
  const Bytes raw = binary(container(kRussian, "Bob"));
  EXPECT_EQ(drain(d, raw), std::vector<Bytes>{raw});
}

TEST(MCOtxtDelivery, OuterBinaryNameFallbackAndStableInheritedTimestamp) {
  MCOtxtDelivery d;
  auto frames = drain(d, binary(container(kReply), "Bob"));
  ASSERT_GT(frames.size(), 2u);
  EXPECT_EQ(frames[1][0], 17);
  for (size_t i = 1; i < frames.size(); ++i) EXPECT_EQ(timestamp(frames[i]), 100u);
  frames.erase(frames.begin());
  EXPECT_EQ(withoutSpaces(reassemble(frames, "Bob: ")), withoutSpaces(kReply.text));
}

TEST(MCOtxtDelivery, GeneratedFramesRespectLegacyAndV3LayoutsAndSignedRoomPrefix) {
  for (bool source_v3 : {false, true}) for (uint8_t target : Bytes{0, 4}) {
    for (uint8_t type : Bytes{0, 2}) {
      MCOtxtDelivery d;
      const auto frames = drain(d, direct(textToken(container(kRussian)), type, source_v3), target);
      ASSERT_EQ(frames.size(), 1u);
      EXPECT_EQ(frames[0][0], target >= 3 ? 16 : 7);
      EXPECT_EQ(bodyText(frames[0]), "Привет мир!");
      if (type == 2) EXPECT_EQ(frames[0][headerSize(frames[0]) - 4], 0xAA);
    }
    MCOtxtDelivery d;
    auto frames = drain(d, binary(container(kRussian, "Bob")), target);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1][0], target >= 3 ? 17 : 8);
    EXPECT_EQ(bodyText(frames[1]), "Bob: Привет мир!");
  }
}

TEST(MCOtxtDelivery, NewAppReevaluatesRetainedRawHeadWithoutLeakingOldCursorOrCapability) {
  const Bytes raw = channel("Bob: " + textToken(container(kReply)));
  MCOtxtDelivery d;
  bool consumed;
  const Bytes first = next(d, raw, consumed);
  EXPECT_FALSE(consumed);
  start(d, "decoder;cap=mctxt");
  EXPECT_EQ(next(d, raw, consumed), raw); EXPECT_TRUE(consumed);
  start(d, "legacy");
  EXPECT_EQ(next(d, raw, consumed), first); EXPECT_FALSE(consumed);
  d.resetMessage();  // the real queue calls this when overflow evicts its head
  EXPECT_EQ(bodyText(drain(d, channel("Bob: " + textToken(container(kRussian))))[0]), "Bob: Привет мир!");
}

TEST(MCOtxtDelivery, UnknownMalformedAndOtherBinaryRemainAvailableRaw) {
  for (Bytes body : {Bytes{0x32, 4, 1, 2, 'h', 'i'}, Bytes{0x31, 0xFF}, Bytes{0x13, 0}}) {
    MCOtxtDelivery d;
    const Bytes raw = binary(body);
    EXPECT_EQ(drain(d, raw), std::vector<Bytes>{raw});
  }
  MCOtxtDelivery d;
  const Bytes raw = channel("Bob: mct:!!!");
  EXPECT_EQ(drain(d, raw), std::vector<Bytes>{raw});
}

TEST(MCOtxtDelivery, InsufficientDecodeWorkspaceFallsBackToRawWithoutClipping) {
  char out[32];
  EXPECT_FALSE(mcotxt::decodeText(textToken(container(kReply)).c_str(), out, sizeof(out)));
}

TEST(MCOtxtDelivery, ConflictingEmbeddedRepliesStayRawRatherThanLosingAnAnchor) {
  MCOtxtDelivery d;
  const Bytes raw = channel("Bob: " + textToken(container(kRussian, "", "Ann")) +
                           " " + textToken(container(kEnglish, "", "Eve")));
  EXPECT_EQ(drain(d, raw), std::vector<Bytes>{raw});
}

TEST(MCOtxtDelivery, EmbeddedNulInDecodedTextNeverSilentlyTruncatesTheMessage) {
  // UTF-8 mode and RAW_UTF8 stream mode can represent NUL, but a companion
  // C-string cannot. Both must fall back to their original source bytes.
  for (const Bytes& body : {Bytes{0x31, 4, 1, 3, 'a', 0, 'b'},
                           Bytes{0x31, 4, 0, 40, 0x23, 0x90, 'a', 0, 'b'}}) {
    MCOtxtDelivery d;
    const Bytes raw = channel("Bob: " + textToken(body));
    EXPECT_EQ(drain(d, raw), std::vector<Bytes>{raw});
    d.resetMessage();
    const Bytes binary_raw = binary(body);
    EXPECT_EQ(drain(d, binary_raw), std::vector<Bytes>{binary_raw});
  }
}

TEST(MCOtxtDelivery, EmptyDecodedTextAndLateCapabilityDoNotNeedSpecialQueueRecords) {
  const Bytes empty = {0x31, 4, 1, 0};
  MCOtxtDelivery d;
  const auto frames = drain(d, direct(textToken(empty)));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_TRUE(bodyText(frames[0]).empty());
  start(d, std::string(80, 'x') + ";cap=mctxt");
  const Bytes raw = channel("Bob: " + textToken(container(kRussian)));
  EXPECT_EQ(drain(d, raw), std::vector<Bytes>{raw});
}

TEST(MCOtxtDelivery, EveryTruncationAndBitMutationOfAValidContainerIsBounded) {
  const Bytes good = container(kReply, "Bob", "Ann");
  for (size_t cut = 0; cut <= good.size(); ++cut) {
    MCOtxtDelivery d;
    drain(d, binary(Bytes(good.begin(), good.begin() + cut)));
  }
  for (size_t pos = 0; pos < good.size(); ++pos) for (uint8_t bit = 0; bit < 8; ++bit) {
    Bytes body = good;
    body[pos] ^= (uint8_t)(1u << bit);
    MCOtxtDelivery d;
    drain(d, binary(body));
  }
}

TEST(MCOtxtDelivery, BoundedRandomFramesAndTruncatedContainersNeverOverrunOutput) {
  std::mt19937 random(0x4D4354);
  MCOtxtDelivery d;
  for (size_t trial = 0; trial < 4000; ++trial) {
    Bytes raw(random() % (MCOtxtDelivery::kFrameSize + 1));
    for (auto& byte : raw) byte = (uint8_t)random();
    if (raw.size() >= 9 && trial % 2 == 0) { raw[0] = 27; raw[6] = 0x20; raw[7] = 1; raw[8] = (uint8_t)(raw.size() - 9); }
    bool consumed;
    next(d, raw, consumed, (uint8_t)(trial % 5));
    d.resetMessage();
  }
}

TEST(MCOtxtNames, SenderAndReplyAreClippedToFirmwareNameLimit) {
  for (const std::string sender : {std::string(96, 'A'), std::string("Боб")}) {
    const std::string author(40, 'R');
    const std::string expected_sender = sender == "Боб" ? sender : std::string(31, 'A');
    const std::string expected = expected_sender + ": @[" + std::string(31, 'R') + "] Hello world!";
    const Bytes raw = binary(container(kEnglish, sender, author));
    MCOtxtDelivery delivery;
    const auto frames = drain(delivery, raw);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(bodyText(frames[1]), expected);
    EXPECT_STREQ(mcotxt::displayBinary(0x0120, raw.data() + 9, raw[8]),
                 expected.c_str());
  }
}

TEST(MCOtxtNames, Utf8ClippingKeepsWholeCharactersAndTheSameInlineReply) {
  std::string author;
  for (int i = 0; i < 17; ++i) author += "я";
  const std::string clipped = author.substr(0, 30);  // the next two-byte character cannot fit
  const std::string token = textToken(container(kEnglish, "", author));
  const std::string input = "Bob: quote: " + token + " then " + token + ".";
  MCOtxtDelivery delivery;
  const auto frames = drain(delivery, channel(input));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(bodyText(frames[0]), "Bob: @[" + clipped + "] quote: Hello world! then Hello world!.");
}

TEST(MCOtxtNames, ReplyMetadataDoesNotConsumeTextWorkspace) {
  const std::string input = "quote: " + textToken(container(kEnglish, "", "Ann"));
  char output[40];
  ASSERT_TRUE(mcotxt::decodeText(input.c_str(), output, sizeof(output)));
  EXPECT_STREQ(output, "@[Ann] quote: Hello world!");
}

TEST(MCOtxtNames, DistinctAuthorsWithinTheFirmwareLimitStayDistinct) {
  const std::string common(30, 'A');
  const std::string input = "Bob: " + textToken(container(kEnglish, "", common + "X")) +
                           " " + textToken(container(kEnglish, "", common + "Y"));
  const Bytes raw = channel(input);
  MCOtxtDelivery delivery;
  EXPECT_EQ(drain(delivery, raw), std::vector<Bytes>{raw});
}

TEST(MCOtxtNames, EnvelopeSenderIsClippedAndItsWholeUtf8FieldIsValidated) {
  const std::string sender(96, 'A');
  const Bytes raw = binary(container(kEnglish), sender);
  MCOtxtDelivery delivery;
  const auto frames = drain(delivery, raw);
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(bodyText(frames[1]), std::string(31, 'A') + ": Hello world!");
  const Bytes invalid = binary(container(kEnglish), std::string(32, 'A') + "\xc0\x80");
  delivery.resetMessage();
  EXPECT_EQ(drain(delivery, invalid), std::vector<Bytes>{invalid});

  const std::string unicode_name = "яяяяяяяяяяяяяяяяя";  // 34 bytes
  const std::string expected_name = "яяяяяяяяяяяяяяя";   // 30 complete bytes
  for (bool inner : {false, true}) {
    delivery.resetMessage();
    const Bytes unicode_raw = inner ? binary(container(kEnglish, unicode_name))
                                    : binary(container(kEnglish), unicode_name);
    const auto unicode_frames = drain(delivery, unicode_raw);
    ASSERT_EQ(unicode_frames.size(), 2u);
    EXPECT_EQ(bodyText(unicode_frames[1]), expected_name + ": Hello world!");
  }
}

TEST(MCOtxtNames, CompressedSenderIsClippedLikeAnUncompressedNodeName) {
  // A name is a counted MCOtxt string too; its decoded length is not its wire size.
  Bytes body = {0x31, 6, 0};  // inherited timestamp, compressed sender
  const Bytes name = fromHex(kReply.frame_hex);
  body.insert(body.end(), name.begin(), name.end());
  body.push_back(0);
  const Bytes text = fromHex(kEnglish.frame_hex);
  body.insert(body.end(), text.begin(), text.end());
  const Bytes raw = binary(body);
  MCOtxtDelivery delivery;
  auto frames = drain(delivery, raw);
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0], raw);
  frames.erase(frames.begin());
  EXPECT_EQ(bodyText(frames[0]), "о, а вот тебе отве: Hello world!");
  EXPECT_STREQ(mcotxt::displayBinary(0x0120, raw.data() + 9, raw[8]),
               "о, а вот тебе отве: Hello world!");
}

TEST(MCOtxtNames, CompressedReplyAuthorUsesTheSameBoundedNameField) {
  Bytes body = {0x31, 7};
  stringField(body, "Bob");
  body.push_back(0);  // compressed reply author, longer than a whole text frame
  const Bytes author = fromHex(kReply.frame_hex);
  body.insert(body.end(), author.begin(), author.end());
  integer(body, 0x10203040);
  body.push_back(0);
  const Bytes text = fromHex(kEnglish.frame_hex);
  body.insert(body.end(), text.begin(), text.end());
  const Bytes raw = binary(body);
  MCOtxtDelivery delivery;
  const auto frames = drain(delivery, raw);
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(bodyText(frames[1]), "Bob: @[о, а вот тебе отве] Hello world!");
}

TEST(MCOtxtNames, MetadataOwnsItsNamesAfterTheOutputBufferIsReused) {
  const std::string author(40, 'R');
  const std::string token = textToken(container(kEnglish, "Bob", author));
  char output[128];
  mcotxt::DecodedMessage metadata;
  ASSERT_EQ(mcotxt::decodeTextPayload(token.c_str(), output, sizeof(output), metadata),
            mcotxt::MessageStatus::Ok);
  EXPECT_EQ(sizeof(metadata.sender), 32u);
  EXPECT_EQ(sizeof(metadata.reply_author), 32u);
  EXPECT_STREQ(metadata.sender, "Bob");
  EXPECT_STREQ(metadata.reply_author, std::string(31, 'R').c_str());
  EXPECT_STREQ(output, "Hello world!");
  memset(output, 'X', sizeof(output));
  EXPECT_STREQ(metadata.sender, "Bob");
  EXPECT_STREQ(metadata.reply_author, std::string(31, 'R').c_str());
}

TEST(MCOtxtNames, InlineTokenBudgetTracksTransportFrameBudget) {
  Bytes raw;
  std::string expected;
  size_t token_size = 0;
  for (size_t n = 1; n + 5 <= MCOtxtDelivery::kTextSize; ++n) {
    const std::string text(n, 'A');
    Bytes body = {0x31, 4};
    stringField(body, text);
    const std::string token = textToken(body);
    const Bytes candidate = channel("Bob: " + token);
    if (candidate.size() > MCOtxtDelivery::kFrameSize) continue;
    raw = candidate;
    expected = "Bob: " + text;
    token_size = token.size();
  }
  ASSERT_FALSE(raw.empty());
  RecordProperty("input_frame_bytes", (int)raw.size());
  RecordProperty("encoded_token_bytes", (int)token_size);
  MCOtxtDelivery delivery;
  const auto frames = drain(delivery, raw);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(bodyText(frames[0]), expected);
}

TEST(MCOtxtDisplay, PlainAndMalformedTextKeepTheirOriginalRepresentation) {
  for (const std::string input : {"Bob: ordinary text", "Bob: mct:!!!", "mct:"}) {
    EXPECT_EQ(mcotxt::displayText(input.c_str(), true), input.c_str());
  }
  EXPECT_EQ(mcotxt::displayText(nullptr, false), nullptr);
}

TEST(MCOtxtDisplay, ChannelPrefixPrecedesReplyAndEmbeddedDecodedText) {
  const std::string input = "Bob: quote: " + textToken(container(kRussian, "", "Ann"));
  const std::string before = input;
  EXPECT_STREQ(mcotxt::displayText(input.c_str(), true), "Bob: @[Ann] quote: Привет мир!");
  EXPECT_EQ(input, before);
}

TEST(MCOtxtDisplay, DirectTextIsDecodedOnceWithoutBlePartDecorations) {
  const std::string input = textToken(container(kReply, "", "Ann"));
  const std::string expected = std::string("@[Ann] ") + kReply.text;
  EXPECT_STREQ(mcotxt::displayText(input.c_str(), false), expected.c_str());
}

TEST(MCOtxtDisplay, BinaryPreviewPreservesSenderReplyAndOriginalBytes) {
  const Bytes raw = binary(container(kRussian, "Bob", "Ann"));
  const Bytes before = raw;
  EXPECT_STREQ(mcotxt::displayBinary(0x0120, raw.data() + 9, raw[8]),
               "Bob: @[Ann] Привет мир!");
  EXPECT_EQ(raw, before);
}

TEST(MCOtxtDisplay, BinarySenderFallsBackToEnvelopeOrUnknown) {
  for (const std::string name : {"Bob", ""}) {
    const Bytes raw = binary(container(kEnglish), name);
    const std::string expected = (name.empty() ? "?" : name) + std::string(": Hello world!");
    EXPECT_STREQ(mcotxt::displayBinary(0x0120, raw.data() + 9, raw[8]), expected.c_str());
  }
}

TEST(MCOtxtDisplay, UnknownAndMalformedBinaryDoNotCreateTextPreviews) {
  for (const Bytes body : {Bytes{0x32, 4, 1, 2, 'h', 'i'}, Bytes{0x31, 0xff}, Bytes{0x13, 0}}) {
    const Bytes raw = binary(body);
    EXPECT_EQ(mcotxt::displayBinary(0x0120, raw.data() + 9, raw[8]), nullptr);
  }
  const Bytes raw = binary(container(kEnglish));
  EXPECT_EQ(mcotxt::displayBinary(0x9999, raw.data() + 9, raw[8]), nullptr);
}

TEST(MCOtxtDisplay, PreviewCopiesFitExistingUiBuffersOnUtf8Boundaries) {
  const std::string input = "Bob: я😀!";
  for (size_t capacity = 0; capacity <= input.size() + 1; ++capacity) {
    char guarded[64];
    memset(guarded, 0x5a, sizeof(guarded));
    mcotxt::copyDisplayPreview(guarded + 1, capacity, input.c_str());
    EXPECT_EQ(guarded[0], 0x5a);
    EXPECT_EQ(guarded[capacity + 1], 0x5a);
    if (!capacity) continue;
    const std::string copied(guarded + 1);
    EXPECT_LT(copied.size(), capacity);
    EXPECT_TRUE(validUtf8(copied));
    EXPECT_EQ(input.substr(0, copied.size()), copied);
  }
  char preview[12];
  mcotxt::copyDisplayPreview(preview, sizeof(preview), input.c_str());
  EXPECT_STREQ(preview, "Bob: я😀");
}

TEST(MCOtxtDisplay, CopiedPreviewSurvivesSubsequentUseOfSharedWorkspace) {
  const std::string input = "Bob: " + textToken(container(kRussian));
  char preview[80];
  mcotxt::copyDisplayPreview(preview, sizeof(preview), mcotxt::displayText(input.c_str(), true));
  const Bytes other = binary(container(kEnglish, "Ann"));
  ASSERT_NE(mcotxt::displayBinary(0x0120, other.data() + 9, other[8]), nullptr);
  EXPECT_STREQ(preview, "Bob: Привет мир!");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
