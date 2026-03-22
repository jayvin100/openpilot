#include "tools/cabana/dbc/dbc.h"

#include <algorithm>

#ifdef signals
#pragma push_macro("signals")
#undef signals
#define CABANA_RESTORE_SIGNALS_MACRO
#endif
#include "tools/cabana/dbc/dbc_core.h"
#ifdef CABANA_RESTORE_SIGNALS_MACRO
#pragma pop_macro("signals")
#undef CABANA_RESTORE_SIGNALS_MACRO
#endif
#include "tools/cabana/utils/util.h"

// cabana::Msg

cabana::Msg::~Msg() {
  for (auto s : sigs) {
    delete s;
  }
}

cabana::Signal *cabana::Msg::addSignal(const cabana::Signal &sig) {
  auto s = sigs.emplace_back(new cabana::Signal(sig));
  update();
  return s;
}

cabana::Signal *cabana::Msg::updateSignal(const std::string &sig_name, const cabana::Signal &new_sig) {
  auto s = sig(sig_name);
  if (s) {
    *s = new_sig;
    update();
  }
  return s;
}

void cabana::Msg::removeSignal(const std::string &sig_name) {
  auto it = std::find_if(sigs.begin(), sigs.end(), [&](auto &s) { return s->name == sig_name; });
  if (it != sigs.end()) {
    delete *it;
    sigs.erase(it);
    update();
  }
}

cabana::Msg &cabana::Msg::operator=(const cabana::Msg &other) {
  address = other.address;
  name = other.name;
  size = other.size;
  comment = other.comment;
  transmitter = other.transmitter;

  for (auto s : sigs) delete s;
  sigs.clear();
  for (auto s : other.sigs) {
    sigs.push_back(new cabana::Signal(*s));
  }

  update();
  return *this;
}

cabana::Signal *cabana::Msg::sig(const std::string &sig_name) const {
  auto it = std::find_if(sigs.begin(), sigs.end(), [&](auto &s) { return s->name == sig_name; });
  return it != sigs.end() ? *it : nullptr;
}

int cabana::Msg::indexOf(const cabana::Signal *sig) const {
  for (int i = 0; i < sigs.size(); ++i) {
    if (sigs[i] == sig) return i;
  }
  return -1;
}

std::string cabana::Msg::newSignalName() {
  std::string new_name;
  for (int i = 1; /**/; ++i) {
    new_name = "NEW_SIGNAL_" + std::to_string(i);
    if (sig(new_name) == nullptr) break;
  }
  return new_name;
}

void cabana::Msg::update() {
  if (transmitter.empty()) {
    transmitter = DEFAULT_NODE_NAME;
  }
  mask.assign(size, 0x00);
  multiplexor = nullptr;

  // sort signals
  std::sort(sigs.begin(), sigs.end(), [](auto l, auto r) {
    return std::tie(r->type, l->multiplex_value, l->start_bit, l->name) <
           std::tie(l->type, r->multiplex_value, r->start_bit, r->name);
  });

  for (auto sig : sigs) {
    if (sig->type == cabana::Signal::Type::Multiplexor) {
      multiplexor = sig;
    }
    sig->update();

    // update mask
    int i = sig->msb / 8;
    int bits = sig->size;
    while (i >= 0 && i < size && bits > 0) {
      int lsb = (int)(sig->lsb / 8) == i ? sig->lsb : i * 8;
      int msb = (int)(sig->msb / 8) == i ? sig->msb : (i + 1) * 8 - 1;

      int sz = msb - lsb + 1;
      int shift = (lsb - (i * 8));

      mask[i] |= ((1ULL << sz) - 1) << shift;

      bits -= sz;
      i = sig->is_little_endian ? i - 1 : i + 1;
    }
  }

  for (auto sig : sigs) {
    sig->multiplexor = sig->type == cabana::Signal::Type::Multiplexed ? multiplexor : nullptr;
    if (!sig->multiplexor) {
      if (sig->type == cabana::Signal::Type::Multiplexed) {
        sig->type = cabana::Signal::Type::Normal;
      }
      sig->multiplex_value = 0;
    }
  }
}

// cabana::Signal

void cabana::Signal::update() {
  updateMsbLsb(*this);
  if (receiver_name.empty()) {
    receiver_name = DEFAULT_NODE_NAME;
  }

  float h = 19 * (float)lsb / 64.0;
  h = fmod(h, 1.0);
  size_t hash = std::hash<std::string>{}(name);
  float s = 0.25 + 0.25 * (float)(hash & 0xff) / 255.0;
  float v = 0.75 + 0.25 * (float)((hash >> 8) & 0xff) / 255.0;

  color = QColor::fromHsvF(h, s, v);
  precision = std::max(num_decimals(factor), num_decimals(offset));
}

std::string cabana::Signal::formatValue(double value, bool with_unit) const {
  // Show enum string
  int64_t raw_value = round((value - offset) / factor);
  for (const auto &[val, desc] : val_desc) {
    if (std::abs(raw_value - val) < 1e-6) {
      return desc;
    }
  }

  char buf[64];
  snprintf(buf, sizeof(buf), "%.*f", precision, value);
  std::string val_str(buf);
  if (with_unit && !unit.empty()) {
    val_str += " " + unit;
  }
  return val_str;
}

bool cabana::Signal::getValue(const uint8_t *data, size_t data_size, double *val) const {
  if (multiplexor && get_raw_value(data, data_size, *multiplexor) != multiplex_value) {
    return false;
  }
  *val = get_raw_value(data, data_size, *this);
  return true;
}

bool cabana::Signal::operator==(const cabana::Signal &other) const {
  return name == other.name && size == other.size &&
         start_bit == other.start_bit &&
         msb == other.msb && lsb == other.lsb &&
         is_signed == other.is_signed && is_little_endian == other.is_little_endian &&
         factor == other.factor && offset == other.offset &&
         min == other.min && max == other.max && comment == other.comment && unit == other.unit && val_desc == other.val_desc &&
         multiplex_value == other.multiplex_value && type == other.type && receiver_name == other.receiver_name;
}

// helper functions

namespace {

dbc::Signal to_core_signal(const cabana::Signal &sig) {
  dbc::Signal core;
  core.type = static_cast<dbc::Signal::Type>(sig.type);
  core.name = sig.name;
  core.start_bit = sig.start_bit;
  core.msb = sig.msb;
  core.lsb = sig.lsb;
  core.size = sig.size;
  core.factor = sig.factor;
  core.offset = sig.offset;
  core.min = sig.min;
  core.max = sig.max;
  core.is_signed = sig.is_signed;
  core.is_little_endian = sig.is_little_endian;
  core.unit = sig.unit;
  core.comment = sig.comment;
  core.receiver_name = sig.receiver_name;
  core.multiplex_value = sig.multiplex_value;
  return core;
}

}  // namespace

double get_raw_value(const uint8_t *data, size_t data_size, const cabana::Signal &sig) {
  return dbc::rawSignalValue(to_core_signal(sig), data, data_size);
}

void updateMsbLsb(cabana::Signal &s) {
  dbc::Signal core = to_core_signal(s);
  dbc::updateMsbLsb(&core);
  s.lsb = core.lsb;
  s.msb = core.msb;
}
