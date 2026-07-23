#include "data_buffer.h"
#include "config.h"

namespace {
  DataBuffer::Record g_buf[OFFLINE_BUFFER_SIZE];
  uint8_t g_head = 0;   // vị trí bản ghi cũ nhất
  uint8_t g_len  = 0;   // số bản ghi hiện có
}

void DataBuffer::begin() { g_head = 0; g_len = 0; }

bool DataBuffer::push(const Record &r) {
  uint8_t tail = (g_head + g_len) % OFFLINE_BUFFER_SIZE;
  bool overwrite = false;
  if (g_len == OFFLINE_BUFFER_SIZE) {
    // đầy -> ghi đè bản ghi cũ nhất, dịch head
    g_head = (g_head + 1) % OFFLINE_BUFFER_SIZE;
    overwrite = true;
  } else {
    g_len++;
  }
  g_buf[tail] = r;
  return !overwrite;
}

bool DataBuffer::peek(Record &r) {
  if (g_len == 0) return false;
  r = g_buf[g_head];
  return true;
}

void DataBuffer::pop() {
  if (g_len == 0) return;
  g_head = (g_head + 1) % OFFLINE_BUFFER_SIZE;
  g_len--;
}

bool    DataBuffer::empty() { return g_len == 0; }
bool    DataBuffer::full()  { return g_len == OFFLINE_BUFFER_SIZE; }
uint8_t DataBuffer::count() { return g_len; }
