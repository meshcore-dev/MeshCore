#include "StaticPoolPacketManager.h"

PacketQueue::PacketQueue(int max_entries) {
  _table = new mesh::Packet*[max_entries];
  _pri_table = new uint8_t[max_entries];
  _schedule_table = new uint32_t[max_entries];
  _size = max_entries;
  _num = 0;
}

int PacketQueue::countBefore(uint32_t now) const {
  int n = 0;
  for (int j = 0; j < _num; j++) {
    if ((int32_t)(_schedule_table[j] - now) > 0) continue;   // scheduled for future... ignore for now
    n++;
  }
  return n;
}

mesh::Packet* PacketQueue::get(uint32_t now) {
  uint8_t min_pri = 0xFF;
  int best_idx = -1;
  for (int j = 0; j < _num; j++) {
    if ((int32_t)(_schedule_table[j] - now) > 0) continue;   // scheduled for future... ignore for now
    if (_pri_table[j] < min_pri) {  // select most important priority amongst non-future entries
      min_pri = _pri_table[j];
      best_idx = j;
    }
  }
  if (best_idx < 0) return NULL;   // empty, or all items are still in the future

  mesh::Packet* top = _table[best_idx];
  int i = best_idx;
  _num--;
  while (i < _num) {
    _table[i] = _table[i+1];
    _pri_table[i] = _pri_table[i+1];
    _schedule_table[i] = _schedule_table[i+1];
    i++;
  }
  return top;
}

mesh::Packet* PacketQueue::removeByIdx(int i) {
  if (i >= _num) return NULL;  // invalid index

  mesh::Packet* item = _table[i];
  _num--;
  while (i < _num) {
    _table[i] = _table[i+1];
    _pri_table[i] = _pri_table[i+1];
    _schedule_table[i] = _schedule_table[i+1];
    i++;
  }
  return item;
}

bool PacketQueue::add(mesh::Packet* packet, uint8_t priority, uint32_t scheduled_for) {
  if (_num == _size) {
    return false;
  }
  _table[_num] = packet;
  _pri_table[_num] = priority;
  _schedule_table[_num] = scheduled_for;
  _num++;
  return true;
}

StaticPoolPacketManager::StaticPoolPacketManager(int pool_size): unused(pool_size), send_queue(pool_size), rx_queue(pool_size) {
  // load up our unusued Packet pool
  for (int i = 0; i < pool_size; i++) {
    unused.add(new mesh::Packet(), 0, 0);
  }
}

mesh::Packet* StaticPoolPacketManager::allocNew() {
  return unused.removeByIdx(0);  // just get first one (returns NULL if empty)
}

void StaticPoolPacketManager::free(mesh::Packet* packet) {
  unused.add(packet, 0, 0);
}

void StaticPoolPacketManager::queueOutbound(mesh::Packet* packet, uint8_t priority, uint32_t scheduled_for) {
  if (!send_queue.add(packet, priority, scheduled_for)) {
    MESH_DEBUG_PRINTLN("queueOutbound: send queue full, dropping packet");
    free(packet);
  }
}

mesh::Packet* StaticPoolPacketManager::getNextOutbound(uint32_t now) {
  //send_queue.sort();   // sort by scheduled_for/priority first
  return send_queue.get(now);
}

int  StaticPoolPacketManager::getOutboundCount(uint32_t now) const {
  return send_queue.countBefore(now);
}

int StaticPoolPacketManager::getFreeCount() const {
  return unused.count();
}

mesh::Packet* StaticPoolPacketManager::getOutboundByIdx(int i) {
  return send_queue.itemAt(i);
}
mesh::Packet* StaticPoolPacketManager::removeOutboundByIdx(int i) {
  return send_queue.removeByIdx(i);
}

void StaticPoolPacketManager::queueInbound(mesh::Packet* packet, uint32_t scheduled_for) {
  if (!rx_queue.add(packet, 0, scheduled_for)) {
    MESH_DEBUG_PRINTLN("queueInbound: rx queue full, dropping packet");
    free(packet);
  }
}
mesh::Packet* StaticPoolPacketManager::getNextInbound(uint32_t now) {
  return rx_queue.get(now);
}
