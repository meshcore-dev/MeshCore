#pragma once

#include <MeshCore.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>

class MeshEventListener {
public:
    virtual void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) = 0;
    virtual void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) = 0;
    virtual void onQueueSizeChanged(int msgcount) = 0;
    virtual void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) { }
    virtual void onControlDataRecv(const mesh::Packet* pkt) { }
    virtual void onChannelDataRecv(mesh::Packet *pkt, const mesh::GroupChannel &channel, uint16_t data_type,
                                    const uint8_t *data, size_t data_len) { }
    virtual void onACKRecv(uint32_t ack_crc) { }
    virtual uint8_t onUnhandledRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                        uint8_t len, uint8_t *reply) { return 0; /* unknown request type */ }
    virtual bool onUnhandledResponse(const ContactInfo &from, uint32_t tag, const uint8_t* data, uint8_t len) { return false; /* not handled */ }
    virtual void onTraceRecv(mesh::Packet *pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                            const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) { }
    virtual void onRawDataRecv(mesh::Packet *pkt) { }
    virtual ~MeshEventListener() { }
};
