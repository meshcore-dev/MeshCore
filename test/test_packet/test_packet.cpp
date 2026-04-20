#include <gtest/gtest.h>
#include <Packet.h>
#include <string.h>

using namespace mesh;

// --- Construction ---

TEST(PacketTest, DefaultConstruction) {
    Packet pkt;
    EXPECT_EQ(pkt.header, 0);
    EXPECT_EQ(pkt.path_len, 0);
    EXPECT_EQ(pkt.payload_len, 0);
}

// --- Round-trip: writeTo -> readFrom ---

TEST(PacketTest, FloodRoundTrip) {
    Packet original;
    original.header = (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD;
    original.path_len = 3;
    memcpy(original.path, "\x01\x02\x03", 3);
    const char* msg = "hello mesh";
    original.payload_len = strlen(msg);
    memcpy(original.payload, msg, original.payload_len);

    uint8_t wire[MAX_TRANS_UNIT];
    uint8_t len = original.writeTo(wire);

    Packet restored;
    ASSERT_TRUE(restored.readFrom(wire, len));
    EXPECT_EQ(restored.header, original.header);
    EXPECT_EQ(restored.path_len, original.path_len);
    EXPECT_EQ(memcmp(restored.path, original.path, original.path_len), 0);
    EXPECT_EQ(restored.payload_len, original.payload_len);
    EXPECT_EQ(memcmp(restored.payload, original.payload, original.payload_len), 0);
}

TEST(PacketTest, DirectRouteRoundTrip) {
    Packet original;
    original.header = (PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
    original.path_len = 5;
    memset(original.path, 0xAB, 5);
    original.payload_len = 10;
    memset(original.payload, 0xCD, 10);

    uint8_t wire[MAX_TRANS_UNIT];
    uint8_t len = original.writeTo(wire);

    Packet restored;
    ASSERT_TRUE(restored.readFrom(wire, len));
    EXPECT_EQ(restored.getRouteType(), ROUTE_TYPE_DIRECT);
    EXPECT_EQ(restored.getPayloadType(), PAYLOAD_TYPE_REQ);
    EXPECT_EQ(restored.path_len, 5);
    EXPECT_EQ(restored.payload_len, 10);
    EXPECT_EQ(memcmp(restored.path, original.path, 5), 0);
    EXPECT_EQ(memcmp(restored.payload, original.payload, 10), 0);
}

TEST(PacketTest, TransportFloodRoundTrip) {
    Packet original;
    original.header = (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) | ROUTE_TYPE_TRANSPORT_FLOOD;
    original.transport_codes[0] = 0x1234;
    original.transport_codes[1] = 0x5678;
    original.path_len = 0;
    original.payload_len = 4;
    memcpy(original.payload, "\xDE\xAD\xBE\xEF", 4);

    uint8_t wire[MAX_TRANS_UNIT];
    uint8_t len = original.writeTo(wire);

    Packet restored;
    ASSERT_TRUE(restored.readFrom(wire, len));
    EXPECT_TRUE(restored.hasTransportCodes());
    EXPECT_EQ(restored.transport_codes[0], 0x1234);
    EXPECT_EQ(restored.transport_codes[1], 0x5678);
    EXPECT_EQ(restored.payload_len, 4);
}

TEST(PacketTest, TransportDirectRoundTrip) {
    Packet original;
    original.header = (PAYLOAD_TYPE_RESPONSE << PH_TYPE_SHIFT) | ROUTE_TYPE_TRANSPORT_DIRECT;
    original.transport_codes[0] = 0xAAAA;
    original.transport_codes[1] = 0xBBBB;
    original.path_len = 2;
    memcpy(original.path, "\x0F\xF0", 2);
    original.payload_len = 1;
    original.payload[0] = 0x42;

    uint8_t wire[MAX_TRANS_UNIT];
    uint8_t len = original.writeTo(wire);

    Packet restored;
    ASSERT_TRUE(restored.readFrom(wire, len));
    EXPECT_TRUE(restored.hasTransportCodes());
    EXPECT_TRUE(restored.isRouteDirect());
    EXPECT_EQ(restored.transport_codes[0], 0xAAAA);
    EXPECT_EQ(restored.transport_codes[1], 0xBBBB);
    EXPECT_EQ(restored.path_len, 2);
    EXPECT_EQ(restored.payload_len, 1);
    EXPECT_EQ(restored.payload[0], 0x42);
}

// --- Header field extraction ---

TEST(PacketTest, HeaderFields) {
    Packet pkt;
    pkt.header = (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD | (PAYLOAD_VER_1 << PH_VER_SHIFT);
    EXPECT_EQ(pkt.getRouteType(), ROUTE_TYPE_FLOOD);
    EXPECT_EQ(pkt.getPayloadType(), PAYLOAD_TYPE_ADVERT);
    EXPECT_EQ(pkt.getPayloadVer(), PAYLOAD_VER_1);
    EXPECT_TRUE(pkt.isRouteFlood());
    EXPECT_FALSE(pkt.isRouteDirect());
    EXPECT_FALSE(pkt.hasTransportCodes());
}

TEST(PacketTest, AllPayloadTypes) {
    const uint8_t types[] = {
        PAYLOAD_TYPE_REQ, PAYLOAD_TYPE_RESPONSE, PAYLOAD_TYPE_TXT_MSG,
        PAYLOAD_TYPE_ACK, PAYLOAD_TYPE_ADVERT, PAYLOAD_TYPE_GRP_TXT,
        PAYLOAD_TYPE_GRP_DATA, PAYLOAD_TYPE_ANON_REQ, PAYLOAD_TYPE_PATH,
        PAYLOAD_TYPE_TRACE, PAYLOAD_TYPE_MULTIPART, PAYLOAD_TYPE_CONTROL,
        PAYLOAD_TYPE_RAW_CUSTOM
    };
    for (uint8_t t : types) {
        Packet pkt;
        pkt.header = (t << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD;
        EXPECT_EQ(pkt.getPayloadType(), t) << "payload type " << (int)t;
    }
}

// --- Wire length calculation ---

TEST(PacketTest, RawLengthNoTransport) {
    Packet pkt;
    pkt.header = ROUTE_TYPE_FLOOD;
    pkt.path_len = 10;
    pkt.payload_len = 20;
    // header(1) + path_len_field(1) + path(10) + payload(20) = 32
    EXPECT_EQ(pkt.getRawLength(), 32);
}

TEST(PacketTest, RawLengthWithTransport) {
    Packet pkt;
    pkt.header = ROUTE_TYPE_TRANSPORT_FLOOD;
    pkt.path_len = 10;
    pkt.payload_len = 20;
    // header(1) + transport(4) + path_len_field(1) + path(10) + payload(20) = 36
    EXPECT_EQ(pkt.getRawLength(), 36);
}

// --- readFrom rejection of bad input ---

TEST(PacketTest, ReadFromRejectsTruncated) {
    // A minimal valid flood packet: header(1) + path_len(1) + payload(1+) = 3 bytes min
    uint8_t bad[] = {ROUTE_TYPE_FLOOD, 0x00};  // only 2 bytes, no payload
    Packet pkt;
    EXPECT_FALSE(pkt.readFrom(bad, sizeof(bad)));
}

TEST(PacketTest, ReadFromRejectsOversizePath) {
    uint8_t bad[3] = {ROUTE_TYPE_FLOOD, MAX_PATH_SIZE + 1, 0x00};
    Packet pkt;
    EXPECT_FALSE(pkt.readFrom(bad, sizeof(bad)));
}

// --- Hash computation ---

TEST(PacketTest, SamePayloadSameHash) {
    Packet a, b;
    a.header = (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
    b.header = (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
    memcpy(a.payload, "test", 4); a.payload_len = 4;
    memcpy(b.payload, "test", 4); b.payload_len = 4;

    uint8_t hash_a[MAX_HASH_SIZE], hash_b[MAX_HASH_SIZE];
    a.calculatePacketHash(hash_a);
    b.calculatePacketHash(hash_b);
    EXPECT_EQ(memcmp(hash_a, hash_b, MAX_HASH_SIZE), 0);
}

TEST(PacketTest, DifferentPayloadDifferentHash) {
    Packet a, b;
    a.header = (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
    b.header = (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
    memcpy(a.payload, "aaaa", 4); a.payload_len = 4;
    memcpy(b.payload, "bbbb", 4); b.payload_len = 4;

    uint8_t hash_a[MAX_HASH_SIZE], hash_b[MAX_HASH_SIZE];
    a.calculatePacketHash(hash_a);
    b.calculatePacketHash(hash_b);
    EXPECT_NE(memcmp(hash_a, hash_b, MAX_HASH_SIZE), 0);
}

TEST(PacketTest, DifferentTypeDifferentHash) {
    Packet a, b;
    a.header = (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
    b.header = (PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT);
    memcpy(a.payload, "same", 4); a.payload_len = 4;
    memcpy(b.payload, "same", 4); b.payload_len = 4;

    uint8_t hash_a[MAX_HASH_SIZE], hash_b[MAX_HASH_SIZE];
    a.calculatePacketHash(hash_a);
    b.calculatePacketHash(hash_b);
    EXPECT_NE(memcmp(hash_a, hash_b, MAX_HASH_SIZE), 0);
}

// --- Do-not-retransmit marker ---

TEST(PacketTest, DoNotRetransmitMarker) {
    Packet pkt;
    EXPECT_FALSE(pkt.isMarkedDoNotRetransmit());
    pkt.markDoNotRetransmit();
    EXPECT_TRUE(pkt.isMarkedDoNotRetransmit());
    EXPECT_EQ(pkt.header, 0xFF);
}

// --- SNR conversion ---

TEST(PacketTest, SNRConversion) {
    Packet pkt;
    pkt._snr = 20;   // 20 / 4.0 = 5.0 dB
    EXPECT_FLOAT_EQ(pkt.getSNR(), 5.0f);

    pkt._snr = -8;   // -8 / 4.0 = -2.0 dB
    EXPECT_FLOAT_EQ(pkt.getSNR(), -2.0f);

    pkt._snr = 0;
    EXPECT_FLOAT_EQ(pkt.getSNR(), 0.0f);
}
