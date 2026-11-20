#include "x11/listener.hpp"

#include "log.hpp"

#include <X11/Xlib.h>
#include <X11/extensions/record.h>

#include <chrono>
#include <cstring>
#include <poll.h>

namespace layoutfix {
namespace {

constexpr int kKeyPress = 2;
constexpr int kKeyRelease = 3;
constexpr int kButtonPress = 4;

} // namespace

Listener::~Listener() { stop(); }

void Listener::stop() {
  if (!started_ || x11_.ctrl() == nullptr || context_ == 0) {
    return;
  }
  started_ = false;
  auto *ctrl = x11_.ctrl();
  auto *data = x11_.data();
  // Disable лише кладе запит у буфер. Без XFlush сервер не закінчить потік,
  // і XCloseDisplay зависне в XSync на з'єднанні XRecord.
  XRecordDisableContext(ctrl, context_);
  XFlush(ctrl);

  if (data != nullptr) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!record_done_ && std::chrono::steady_clock::now() < deadline) {
      XRecordProcessReplies(data);
      if (record_done_) {
        break;
      }
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now())
                            .count();
      if (left <= 0) {
        break;
      }
      pollfd pfd{x11_.data_fd(), POLLIN, 0};
      if (poll(&pfd, 1, static_cast<int>(left)) <= 0) {
        break;
      }
    }
  }

  XRecordFreeContext(ctrl, context_);
  XFlush(ctrl);
  context_ = 0;
  if (!record_done_) {
    log::error("XRecord не завершився, закриваю з'єднання без очікування");
    x11_.abandon_close();
  }
}

bool Listener::start() {
  auto *ctrl = x11_.ctrl();
  auto *data = x11_.data();
  int major = 0;
  int minor = 0;
  if (XRecordQueryVersion(ctrl, &major, &minor) == 0) {
    log::error("XRecord недоступний");
    return false;
  }

  XRecordRange *range = XRecordAllocRange();
  if (range == nullptr) {
    log::error("не вдалося виділити діапазон XRecord");
    return false;
  }
  range->device_events.first = kKeyPress;
  range->device_events.last = kButtonPress;

  XRecordClientSpec spec = XRecordAllClients;
  XRecordRange *ranges[] = {range};
  context_ = XRecordCreateContext(ctrl, 0, &spec, 1, ranges, 1);
  XFree(range);
  if (context_ == 0) {
    log::error("не вдалося створити контекст XRecord");
    return false;
  }
  // Контекст створено на іншому з'єднанні. Без синхронізації Enable
  // може дійти до сервера раніше, ніж Create, і повернути BadContext.
  XSync(ctrl, False);
  if (XRecordEnableContextAsync(data, context_, &Listener::callback,
                                reinterpret_cast<XPointer>(this)) == 0) {
    log::error("не вдалося увімкнути XRecord");
    return false;
  }
  started_ = true;
  return true;
}

void Listener::callback(XPointer closure, XRecordInterceptData *hook) {
  auto *self = reinterpret_cast<Listener *>(closure);
  if (hook == nullptr) {
    return;
  }
  if (hook->category == XRecordEndOfData) {
    self->record_done_ = true;
    XRecordFreeData(hook);
    return;
  }
  if (hook->category != XRecordFromServer || hook->data == nullptr ||
      hook->data_len < 8) {
    XRecordFreeData(hook);
    return;
  }

  unsigned char bytes[32];
  std::memcpy(bytes, hook->data, sizeof bytes);
  const bool swapped = hook->client_swapped;
  XRecordFreeData(hook);

  RawEvent event;
  const unsigned type = bytes[0];
  event.keycode = bytes[1];
  const unsigned state =
      swapped ? (static_cast<unsigned>(bytes[28]) << 8) | bytes[29]
              : bytes[28] | (static_cast<unsigned>(bytes[29]) << 8);
  event.state = state;
  if (type == kKeyPress) {
    event.type = RawEvent::Type::Press;
  } else if (type == kKeyRelease) {
    event.type = RawEvent::Type::Release;
  } else if (type == kButtonPress) {
    event.type = RawEvent::Type::Button;
  } else {
    return;
  }
  self->queue_.push_back(event);
}

void Listener::drain() {
  auto *data = x11_.data();
  if (data == nullptr) {
    return;
  }
  for (int i = 0; i < 100; ++i) {
    XRecordProcessReplies(data);
    pollfd pfd{x11_.data_fd(), POLLIN, 0};
    if (poll(&pfd, 1, 0) <= 0) {
      break;
    }
  }
}

std::vector<RawEvent> Listener::take() {
  std::vector<RawEvent> out;
  out.swap(queue_);
  return out;
}

} // namespace layoutfix
