#pragma once

#if INFINIT_ENABLE_PROMETHEUS
# include <memory>
# include <string>

# include <prometheus/counter.h>
# include <prometheus/family.h>
# include <prometheus/gauge.h>

namespace prometheus
{
  class Exposer;
}

namespace infinit
{
  namespace prometheus
  {
    /// A characteristic profile for a metric instance.
    using Labels = std::map<std::string, std::string>;
    /// A family of metrics.
    template <typename Metric>
    using Family = ::prometheus::Family<Metric>;
    /// A non-monotonic (i.e., increasing/decreasing) counter.
    using Gauge = ::prometheus::Gauge;
    /// A monotonic counter
    using Counter = ::prometheus::Counter;

    /// Delete a metric, i.e. remove it from its family.
    template <typename Metric>
    struct Deleter
    {
      Family<Metric>* family;
      void operator()(Metric* m)
      {
        family->Remove(m);
      }
    };

    /// A managed metric.
    template <typename Metric>
    using UniquePtr = std::unique_ptr<Metric, Deleter<Metric>>;

    /// A managed gauge.
    using GaugePtr = UniquePtr<Gauge>;
    using CounterPtr = UniquePtr<Counter>;

    class Prometheus
    {
    public:
      Prometheus(std::string const& addr);

      /// Specify/change the publishing port.
      void
      bind(std::string const& addr);

      /// Create a family of gauges.
      Family<Gauge>*
      make_gauge_family(std::string const& name,
                        std::string const& help);

      /// Create a family of counters.
      Family<Counter>*
      make_counter_family(std::string const& name,
                        std::string const& help);
      /// Create a new member to a family.
      /// Should be removed eventually.
      UniquePtr<Gauge>
      make(Family<Gauge>* family,
           Labels const& labels);
      UniquePtr<Counter>
      make(Family<Counter>* family,
           Labels const& labels);

      /// The http exposer.
      std::unique_ptr<::prometheus::Exposer> _exposer;
    };

    /// Set the Prometheus publishing address.
    void endpoint(std::string e);

    /// Get the Prometheus publishing address.
    std::string const& endpoint();

    /// The instance to use.
    Prometheus& instance();
  }
}
#endif
