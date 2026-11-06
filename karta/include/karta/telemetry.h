// Karta — vendor-neutral telemetry contracts (port of @earendil-works/pi-telemetry).
//
// Telemetry describes what a program did while running, modeled as spans,
// attributes, events, statuses, and explicit context. No exporter, no global
// current-span state, no telemetry-backend dependency: contexts are passed
// explicitly and applications may supply an adapter.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace karta {
namespace telemetry {

// A named fact attached to a span; value stringified (bool/number as text).
struct Attribute {
  std::string key;
  std::string value;
};

struct SpanStatus {
  // "ok" or "error".
  std::string status = "ok";
  bool has_error = false;
  std::string error_name;
  std::string error_message;
};

// Opaque JSON-safe string map.
using TelemetryCarrier = std::map<std::string, std::string>;

class TelemetryMetrics;
class TelemetryPropagation;

// A timed record of one operation. Lives until the creating context settles it.
class TelemetrySpan {
 public:
  virtual ~TelemetrySpan() = default;
  virtual void add_event(const std::string& name,
                         const std::vector<Attribute>& attributes = {}) = 0;
  virtual void set_attributes(const std::vector<Attribute>& attributes) = 0;
  virtual void set_status(SpanStatus status) = 0;
  virtual void add_link(const TelemetrySpan& target,
                        const std::vector<Attribute>& attributes = {}) = 0;
  virtual const TelemetrySpan* as_context() const { return this; }
  // Opaque carrier for this span (used as an exemplar / propagation target).
  virtual TelemetryCarrier carrier() const { return {}; }

  // Settlement: owned by the creating context once the callback settles.
  virtual void settle_ok() {}
  virtual void settle_error() {}
};

// Span creation options.
struct SpanOptions {
  std::string name;
  std::vector<Attribute> attributes;
  std::vector<const TelemetrySpan*> links;
};

// TelemetryContext starts callback-managed child spans and optionally exposes
// adapter metrics/propagation.
class TelemetryContext {
 public:
  virtual ~TelemetryContext() = default;

  template <typename F>
  auto start_span(const SpanOptions& options, F&& callback) {
    auto span = create_span(options);
    try {
      auto result = callback(*span);
      span->settle_ok();
      return result;
    } catch (...) {
      span->settle_error();
      throw;
    }
  }

  virtual TelemetryMetrics* metrics() { return nullptr; }
  virtual TelemetryPropagation* propagation() { return nullptr; }

 protected:
  virtual std::shared_ptr<TelemetrySpan> create_span(const SpanOptions& options) = 0;
};

class TelemetryPropagation {
 public:
  virtual ~TelemetryPropagation() = default;
  virtual TelemetryCarrier inject(const TelemetryContext& context) = 0;
  virtual std::shared_ptr<TelemetryContext> extract(const TelemetryCarrier& carrier) = 0;
};

extern TelemetryPropagation& NOOP_TELEMETRY_PROPAGATION;

class TelemetryCounter {
 public:
  virtual ~TelemetryCounter() = default;
  virtual void add(long long value, const std::vector<Attribute>& attributes = {}) = 0;
};

class TelemetryUpDownCounter {
 public:
  virtual ~TelemetryUpDownCounter() = default;
  virtual void add(long long value, const std::vector<Attribute>& attributes = {}) = 0;
};

class TelemetryHistogram {
 public:
  virtual ~TelemetryHistogram() = default;
  virtual void record(long long value, const std::vector<Attribute>& attributes = {},
                      const TelemetrySpan* exemplar = nullptr) = 0;
};

struct InstrumentOptions {
  std::string description;
  std::string unit;
};

struct HistogramOptions : InstrumentOptions {
  std::vector<long long> boundaries;
};

class TelemetryMetrics {
 public:
  virtual ~TelemetryMetrics() = default;
  virtual std::unique_ptr<TelemetryCounter> counter(
      const std::string& name, const InstrumentOptions& options = {}) = 0;
  virtual std::unique_ptr<TelemetryUpDownCounter> up_down_counter(
      const std::string& name, const InstrumentOptions& options = {}) = 0;
  virtual std::unique_ptr<TelemetryHistogram> histogram(
      const std::string& name, const HistogramOptions& options = {}) = 0;
};

extern TelemetryMetrics& NOOP_TELEMETRY_METRICS;

// Captured measurement snapshot.
struct RecordedTelemetryMeasurement {
  std::string kind;
  std::string name;
  std::string description;
  std::string unit;
  std::vector<long long> boundaries;
  long long value = 0;
  std::vector<Attribute> attributes;
  TelemetryCarrier context;
};

// Normalized captured span snapshot.
struct RecordedTelemetrySpan {
  long long id = 0;
  std::string parent_id;
  TelemetryCarrier context;
  TelemetryCarrier parent_context;
  std::vector<std::pair<TelemetryCarrier, std::vector<Attribute>>> links;
  std::vector<Attribute> attributes;
  struct Event {
    std::string name;
    std::vector<Attribute> attributes;
  };
  std::vector<Event> events;
  SpanStatus status;
  bool settled = false;
  long long end_sequence = 0;
};

struct RecordedTelemetryEvent {
  std::string name;
  std::vector<Attribute> attributes;
};

struct RecordedTelemetryLink {
  TelemetryCarrier target;
  std::vector<Attribute> attributes;
};

struct StoredSpan {
  long long started_ms = 0;
  long long ended_ms = 0;
  SpanStatus status;
};

struct StoredTrace {
  std::vector<StoredSpan> spans;
};

struct TraceQuery {
  std::string name;
  long long start_min = 0;
  long long start_max = 0;
  bool errors = false;
  std::vector<Attribute> attributes;
  int page_size = 20;
};

struct StoredMetric {
  std::string name;
  long long count = 0;
  long long sum = 0;
  long long minimum = 0;
  long long maximum = 0;
};

struct MetricQuery {
  std::string name;
  long long start_min = 0;
  long long start_max = 0;
  std::vector<Attribute> attributes;
};

class TelemetryStore {
 public:
  virtual ~TelemetryStore() = default;
  virtual StoredTrace find_traces(const TraceQuery& query = {}) = 0;
  virtual std::unique_ptr<StoredSpan> get_trace(const std::string& trace_id) = 0;
  virtual std::vector<StoredMetric> find_metrics(const MetricQuery& query = {}) = 0;
};

// ----------------------------------------------------------------
// Reference implementations
// ----------------------------------------------------------------

extern TelemetryContext& NOOP_TELEMETRY_CONTEXT;

class InMemoryTelemetryContext : public TelemetryContext,
                                 public TelemetryStore,
                                 public TelemetryPropagation,
                                 public TelemetryMetrics {
 public:
  InMemoryTelemetryContext();
  ~InMemoryTelemetryContext() override;

  TelemetryMetrics* metrics() override { return &NOOP_TELEMETRY_METRICS; }
  TelemetryPropagation* propagation() override { return &NOOP_TELEMETRY_PROPAGATION; }

  // TelemetryStore
  StoredTrace find_traces(const TraceQuery& query = {}) override;
  std::unique_ptr<StoredSpan> get_trace(const std::string& trace_id) override;
  std::vector<StoredMetric> find_metrics(const MetricQuery& query = {}) override;

  // TelemetryPropagation
  TelemetryCarrier inject(const TelemetryContext& context) override;
  std::shared_ptr<TelemetryContext> extract(const TelemetryCarrier& carrier) override;

  // TelemetryMetrics
  std::unique_ptr<TelemetryCounter> counter(const std::string& name,
                                            const InstrumentOptions& options = {}) override;
  std::unique_ptr<TelemetryUpDownCounter> up_down_counter(
      const std::string& name, const InstrumentOptions& options = {}) override;
  std::unique_ptr<TelemetryHistogram> histogram(
      const std::string& name, const HistogramOptions& options = {}) override;

  std::vector<RecordedTelemetrySpan> get_spans();
  std::vector<RecordedTelemetryMeasurement> get_metrics();

 private:
  class SpanImpl;
  class MetricsImpl;
  class PropagationImpl;

  std::shared_ptr<TelemetrySpan> create_span(const SpanOptions& options) override;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ----------------------------------------------------------------
// Typed schemas
// ----------------------------------------------------------------

struct TelemetryEventAttributeDefinition {
  std::string type;
  bool required = false;
  std::string description;
};

struct TelemetryEventDefinition {
  std::string description;
  std::map<std::string, TelemetryEventAttributeDefinition> attributes;
};

struct TelemetryParentDefinition {
  std::string kind;   // "any" | "root_or_external" | "spans"
  std::vector<std::string> spans;
};

struct TelemetrySpanDefinition {
  std::string description;
  TelemetryParentDefinition parent;
  std::map<std::string, TelemetryEventAttributeDefinition> start_attributes;
  std::map<std::string, TelemetryEventAttributeDefinition> end_attributes;
  std::map<std::string, TelemetryEventDefinition> events;
  SpanStatus status;
};

struct TelemetrySchemaDefinition {
  int version = 1;
  std::map<std::string, TelemetrySpanDefinition> spans;
};

inline TelemetrySchemaDefinition define_telemetry_schema(TelemetrySchemaDefinition schema) {
  return schema;
}

}  // namespace telemetry
}  // namespace karta
