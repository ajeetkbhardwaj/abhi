// Karta — telemetry reference implementations (port of @earendil-works/pi-telemetry).
// No-op context, in-memory adapter, passive metrics/propagation.

#include "karta/telemetry.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace karta {
namespace telemetry {

namespace {

std::string hex64(uint64_t v) {
  static const char* digits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[i] = digits[v & 0x0f];
    v >>= 4;
  }
  return out;
}

long long now_ms() {
  using namespace std::chrono;
  return static_cast<long long>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

}  // namespace

// ========================================================================
// No-op span / context / metrics / propagation
// ========================================================================

namespace {

class NoopSpan : public TelemetrySpan, public std::enable_shared_from_this<NoopSpan> {
 public:
  void add_event(const std::string&, const std::vector<Attribute>&) override {}
  void set_attributes(const std::vector<Attribute>&) override {}
  void set_status(SpanStatus) override {}
  void add_link(const TelemetrySpan&, const std::vector<Attribute>&) override {}
};

class NoopContext : public TelemetryContext {
 protected:
  std::shared_ptr<TelemetrySpan> create_span(const SpanOptions&) override {
    return std::make_shared<NoopSpan>();
  }
};

class NoopCounter : public TelemetryCounter {
 public:
  void add(long long, const std::vector<Attribute>&) override {}
};
class NoopUpDownCounter : public TelemetryUpDownCounter {
 public:
  void add(long long, const std::vector<Attribute>&) override {}
};
class NoopHistogram : public TelemetryHistogram {
 public:
  void record(long long, const std::vector<Attribute>&, const TelemetrySpan*)
  override {}
};

class NoopMetrics : public TelemetryMetrics {
 public:
  std::unique_ptr<TelemetryCounter> counter(const std::string&,
                                            const InstrumentOptions&) override {
    return std::make_unique<NoopCounter>();
  }
  std::unique_ptr<TelemetryUpDownCounter> up_down_counter(
      const std::string&, const InstrumentOptions&) override {
    return std::make_unique<NoopUpDownCounter>();
  }
  std::unique_ptr<TelemetryHistogram> histogram(
      const std::string&, const HistogramOptions&) override {
    return std::make_unique<NoopHistogram>();
  }
};

class NoopPropagation : public TelemetryPropagation {
 public:
  TelemetryCarrier inject(const TelemetryContext&) override { return {}; }
  std::shared_ptr<TelemetryContext> extract(const TelemetryCarrier&) override {
    return std::make_shared<NoopContext>();
  }
};

}  // namespace

static NoopContext g_noop_context;
static NoopMetrics g_noop_metrics;
static NoopPropagation g_noop_propagation;

TelemetryContext& NOOP_TELEMETRY_CONTEXT = g_noop_context;
TelemetryMetrics& NOOP_TELEMETRY_METRICS = g_noop_metrics;
TelemetryPropagation& NOOP_TELEMETRY_PROPAGATION = g_noop_propagation;

// ========================================================================
// In-memory reference adapter
// ========================================================================

class InMemoryTelemetryContext::SpanImpl : public TelemetrySpan,
                                           public std::enable_shared_from_this<SpanImpl> {
 public:
  using Event = RecordedTelemetrySpan::Event;

  SpanImpl(long long id, const SpanOptions& options, const TelemetryCarrier& parent_context)
      : id_(id), parent_context_(parent_context) {
    started_ms_ = now_ms();
    context_ = {{"span_id", hex64(static_cast<uint64_t>(id))}};
    for (const Attribute& a : options.attributes) {
      if (!a.value.empty()) attrs_.insert_or_assign(a.key, a.value);
    }
    for (const TelemetrySpan* link : options.links) {
      if (!link) continue;
      links_.push_back({link->carrier(), {}});
    }
  }

  void set_on_settle(std::function<void(std::shared_ptr<SpanImpl>)> fn) {
    on_settle_ = std::move(fn);
  }

  void add_event(const std::string& name,
                 const std::vector<Attribute>& attributes) override {
    if (settled_) return;
    Event ev;
    ev.name = name;
    for (const Attribute& a : attributes) {
      if (!a.value.empty()) ev.attributes.push_back(a);
    }
    events_.push_back(std::move(ev));
  }

  void set_attributes(const std::vector<Attribute>& attributes) override {
    if (settled_) return;
    for (const Attribute& a : attributes) {
      if (!a.value.empty()) attrs_.insert_or_assign(a.key, a.value);
    }
  }

  void set_status(SpanStatus status) override {
    if (settled_) return;
    status_ = std::move(status);
  }

  void add_link(const TelemetrySpan& target,
                const std::vector<Attribute>& attributes) override {
    if (settled_) return;
    std::vector<Attribute> copy;
    for (const Attribute& a : attributes) {
      if (!a.value.empty()) copy.push_back(a);
    }
    links_.emplace_back(target.carrier(), std::move(copy));
  }

  const TelemetrySpan* as_context() const override { return this; }

  TelemetryCarrier carrier() const override { return context_; }

  void settle_ok() {
    if (settled_) return;
    settled_ = true;
    status_.status = "ok";
    end_sequence_ = ++SpanImpl::g_end_sequence;
    ended_ms_ = now_ms();
    if (on_settle_) on_settle_(shared_from_this());
  }

  void settle_error() {
    if (settled_) return;
    settled_ = true;
    end_sequence_ = ++SpanImpl::g_end_sequence;
    ended_ms_ = now_ms();
    if (on_settle_) on_settle_(shared_from_this());
  }

  long long id() const { return id_; }
  bool settled() const { return settled_; }
  long long end_sequence() const { return end_sequence_; }
  long long started_ms() const { return started_ms_; }
  long long ended_ms() const { return ended_ms_; }
  const SpanStatus& status() const { return status_; }
  const std::map<std::string, std::string>& attributes() const { return attrs_; }
  const std::vector<Event>& events() const { return events_; }
  const std::vector<std::pair<TelemetryCarrier, std::vector<Attribute>>>& links()
  const {
    return links_;
  }
  const TelemetryCarrier& context() const { return context_; }
  const TelemetryCarrier& parent_context() const { return parent_context_; }

  // Set by Impl to drive the shared end-sequence counter.
  static long long& end_sequence_ref() { return g_end_sequence; }

 private:
  static long long g_end_sequence;

  long long id_;
  TelemetryCarrier context_;
  TelemetryCarrier parent_context_;
  bool settled_ = false;
  long long end_sequence_ = 0;
  long long started_ms_ = 0;
  long long ended_ms_ = 0;
  SpanStatus status_;
  std::map<std::string, std::string> attrs_;
  std::vector<Event> events_;
  std::vector<std::pair<TelemetryCarrier, std::vector<Attribute>>> links_;
  std::function<void(std::shared_ptr<SpanImpl>)> on_settle_;
};

long long InMemoryTelemetryContext::SpanImpl::g_end_sequence = 0;

class InMemoryTelemetryContext::Impl {
 public:
  std::shared_ptr<SpanImpl> create_span(const SpanOptions& options,
                                        const TelemetryCarrier& parent_context) {
    std::lock_guard<std::mutex> lock(mutex_);
    long long id = next_id_++;
    auto span = std::make_shared<SpanImpl>(id, options, parent_context);
    span->set_on_settle([this](std::shared_ptr<SpanImpl> s) {
      std::lock_guard<std::mutex> lock(mutex_);
      settled_spans_.push_back(s);
    });
    spans_.push_back(span);
    return span;
  }

  std::vector<RecordedTelemetrySpan> snapshot_spans() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RecordedTelemetrySpan> out;
    out.reserve(spans_.size());
    for (const auto& s : spans_) {
      RecordedTelemetrySpan r;
      r.id = s->id();
      r.context = s->context();
      r.parent_context = s->parent_context();
      r.links = s->links();
      for (const auto& kv : s->attributes())
        r.attributes.push_back({kv.first, kv.second});
      for (const auto& ev : s->events()) {
        RecordedTelemetrySpan::Event e;
        e.name = ev.name;
        e.attributes = ev.attributes;
        r.events.push_back(std::move(e));
      }
      r.status = s->status();
      r.settled = s->settled();
      r.end_sequence = s->end_sequence();
      out.push_back(std::move(r));
    }
    return out;
  }

  std::mutex mutex_;
  long long next_id_ = 1;
  std::vector<std::shared_ptr<SpanImpl>> spans_;
  std::vector<std::shared_ptr<SpanImpl>> settled_spans_;

  // Metrics instruments
  struct CounterInst : public TelemetryCounter {
    void add(long long value, const std::vector<Attribute>& attributes) override {
      std::lock_guard<std::mutex> lock(mutex_);
      RecordedTelemetryMeasurement m;
      m.kind = "counter";
      m.name = name;
      m.description = description;
      m.unit = unit;
      m.value = value;
      for (const Attribute& a : attributes) {
        if (!a.value.empty()) m.attributes.push_back(a);
      }
      measurements.push_back(std::move(m));
    }
    std::string name;
    std::string description;
    std::string unit;
    std::mutex mutex_;
    std::vector<RecordedTelemetryMeasurement> measurements;
  };

  struct UpDownInst : public TelemetryUpDownCounter {
    void add(long long value, const std::vector<Attribute>& attributes) override {
      std::lock_guard<std::mutex> lock(mutex_);
      RecordedTelemetryMeasurement m;
      m.kind = "upDownCounter";
      m.name = name;
      m.description = description;
      m.unit = unit;
      m.value = value;
      for (const Attribute& a : attributes) {
        if (!a.value.empty()) m.attributes.push_back(a);
      }
      measurements.push_back(std::move(m));
    }
    std::string name;
    std::string description;
    std::string unit;
    std::mutex mutex_;
    std::vector<RecordedTelemetryMeasurement> measurements;
  };

  struct HistInst : public TelemetryHistogram {
    void record(long long value, const std::vector<Attribute>& attributes,
                const TelemetrySpan* exemplar) override {
      std::lock_guard<std::mutex> lock(mutex_);
      RecordedTelemetryMeasurement m;
      m.kind = "histogram";
      m.name = name;
      m.description = description;
      m.unit = unit;
      m.boundaries = boundaries;
      m.value = value;
      for (const Attribute& a : attributes) {
        if (!a.value.empty()) m.attributes.push_back(a);
      }
      if (exemplar) m.context = exemplar->carrier();
      measurements.push_back(std::move(m));
    }
    std::string name;
    std::string description;
    std::string unit;
    std::vector<long long> boundaries;
    std::mutex mutex_;
    std::vector<RecordedTelemetryMeasurement> measurements;
  };

  std::vector<std::unique_ptr<CounterInst>> counters_;
  std::vector<std::unique_ptr<UpDownInst>> updowns_;
  std::vector<std::unique_ptr<HistInst>> hists_;
};

InMemoryTelemetryContext::InMemoryTelemetryContext()
    : impl_(std::make_unique<Impl>()) {}
InMemoryTelemetryContext::~InMemoryTelemetryContext() = default;

std::shared_ptr<TelemetrySpan> InMemoryTelemetryContext::create_span(
    const SpanOptions& options) {
  return impl_->create_span(options, {});
}

std::vector<RecordedTelemetrySpan> InMemoryTelemetryContext::get_spans() {
  return impl_->snapshot_spans();
}

std::vector<RecordedTelemetryMeasurement> InMemoryTelemetryContext::get_metrics() {
  std::lock_guard<std::mutex> lock(impl_->mutex_);
  std::vector<RecordedTelemetryMeasurement> out;
  for (auto& c : impl_->counters_) {
    std::lock_guard<std::mutex> lock(c->mutex_);
    for (auto& m : c->measurements) out.push_back(std::move(m));
  }
  for (auto& u : impl_->updowns_) {
    std::lock_guard<std::mutex> lock(u->mutex_);
    for (auto& m : u->measurements) out.push_back(std::move(m));
  }
  for (auto& h : impl_->hists_) {
    std::lock_guard<std::mutex> lock(h->mutex_);
    for (auto& m : h->measurements) out.push_back(std::move(m));
  }
  return out;
}

StoredTrace InMemoryTelemetryContext::find_traces(const TraceQuery&) { return {}; }
std::unique_ptr<StoredSpan> InMemoryTelemetryContext::get_trace(
    const std::string&) {
  return nullptr;
}
std::vector<StoredMetric> InMemoryTelemetryContext::find_metrics(
    const MetricQuery&) {
  return {};
}

TelemetryCarrier InMemoryTelemetryContext::inject(const TelemetryContext&) {
  return {};
}
std::shared_ptr<TelemetryContext> InMemoryTelemetryContext::extract(
    const TelemetryCarrier&) {
  return std::make_shared<NoopContext>();
}

std::unique_ptr<TelemetryCounter> InMemoryTelemetryContext::counter(
    const std::string& name, const InstrumentOptions& options) {
  std::lock_guard<std::mutex> lock(impl_->mutex_);
  auto inst = std::make_unique<Impl::CounterInst>();
  inst->name = name;
  inst->description = options.description;
  inst->unit = options.unit;
  auto* raw = inst.get();
  impl_->counters_.push_back(std::move(inst));
  return std::unique_ptr<TelemetryCounter>(raw);
}

std::unique_ptr<TelemetryUpDownCounter> InMemoryTelemetryContext::up_down_counter(
    const std::string& name, const InstrumentOptions& options) {
  std::lock_guard<std::mutex> lock(impl_->mutex_);
  auto inst = std::make_unique<Impl::UpDownInst>();
  inst->name = name;
  inst->description = options.description;
  inst->unit = options.unit;
  auto* raw = inst.get();
  impl_->updowns_.push_back(std::move(inst));
  return std::unique_ptr<TelemetryUpDownCounter>(raw);
}

std::unique_ptr<TelemetryHistogram> InMemoryTelemetryContext::histogram(
    const std::string& name, const HistogramOptions& options) {
  std::lock_guard<std::mutex> lock(impl_->mutex_);
  auto inst = std::make_unique<Impl::HistInst>();
  inst->name = name;
  inst->description = options.description;
  inst->unit = options.unit;
  inst->boundaries = options.boundaries;
  auto* raw = inst.get();
  impl_->hists_.push_back(std::move(inst));
  return std::unique_ptr<TelemetryHistogram>(raw);
}

}  // namespace telemetry
}  // namespace karta
