#pragma once

#if MEMO_ENABLE_PROMETHEUS
# include <memory>
# include <string>

# include <prometheus/counter.h>
# include <prometheus/family.h>
# include <prometheus/gauge.h>

namespace prometheus
{
  class Exposer;
}

namespace memo
{
  namespace prometheus
  {
    /// A characteristic profile for a metric instance.
    using Labels = std::map<std::string, std::string>;
    /// A family of metrics.
    template <typename Metric>
    using Family = ::prometheus::Family<Metric>;
    /// A monotonic counter.
    using Counter = ::prometheus::Counter;
    /// A non-monotonic (i.e., increasing/decreasing) counter.
    using Gauge = ::prometheus::Gauge;

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

    /// A managed counter.
    using CounterPtr = UniquePtr<Counter>;
    /// A managed gauge.
    using GaugePtr = UniquePtr<Gauge>;

    class Prometheus
    {
    public:
      Prometheus(std::string const& addr);

      /// Specify/change the publishing port.
      void
      bind(std::string const& addr);

      /// Create a family of gauges.
      Family<Gauge>*
      make_gauge_family(std::string const& name, std::string const& help);
      /// Create a family of counters.
      Family<Counter>*
      make_counter_family(std::string const& name, std::string const& help);
      /// Create a new member to a family.
      /// Should be removed eventually.
      UniquePtr<Gauge>
      make(Family<Gauge>* family, Labels const& labels);
      UniquePtr<Counter>
      make(Family<Counter>* family, Labels const& labels);

      /// The http exposer.
      std::unique_ptr<::prometheus::Exposer> _exposer;
    };

    /// Set the Prometheus publishing address.
    void endpoint(std::string e);

    /// Get the Prometheus publishing address.
    std::string const& endpoint();

    /// The instance to use.
    Prometheus& instance();

    /// Create a family of gauges.
    inline
    Family<Gauge>*
    make_gauge_family(std::string const& name, std::string const& help)
    {
      return instance().make_gauge_family(name, help);
    }

    /// Create a family of counters.
    inline
    Family<Counter>*
    make_counter_family(std::string const& name, std::string const& help)
    {
      return instance().make_counter_family(name, help);
    }

    /// Create a metric.
    template <typename Metric>
    UniquePtr<Metric>
    make(Family<Metric>* family, Labels const& labels)
    {
      return instance().make(family, labels);
    }

    /// Increment a counter or a gauge, if they are defined.
    template <typename Metric>
    void increment(UniquePtr<Metric>& p)
    {
      if (p)
        p->Increment();
    }

    /// Increment a gauge, if they are defined.
    inline
    void decrement(UniquePtr<Gauge>& p)
    {
      if (p)
        p->Decrement();
    }
  }
}
#else // !MEMOy_ENABLE_PROMETHEUS
namespace memo
{
  namespace prometheus
  {
    /// A characteristic profile for a metric instance.
    using Labels = std::map<std::string, std::string>;

    /// A managed metric.
    template <typename Metric>
    using UniquePtr = std::unique_ptr<Metric>;

    template <typename Metric>
    struct Family {};

    struct Counter {};
    struct Gauge {};

    /// A managed counter.
    using CounterPtr = UniquePtr<Counter>;
    /// A managed gauge.
    using GaugePtr = UniquePtr<Gauge>;

    /// Set the Prometheus publishing address.
    inline
    void endpoint(std::string e)
    {
      elle::err("prometheus is not supported on this platform");
    }

    /// Create a family of gauges.
    inline
    Family<Gauge>*
    make_gauge_family(std::string const& name, std::string const& help)
    {
      return nullptr;
    }

    /// Create a family of counters.
    inline
    Family<Counter>*
    make_counter_family(std::string const& name, std::string const& help)
    {
      return nullptr;
    }

    /// Create a metric.
    template <typename Metric>
    UniquePtr<Metric>
    make(Family<Metric>* family, Labels const& labels)
    {
      return nullptr;
    }

    /// Increment a counter or a gauge, if they are defined.
    template <typename Metric>
    void increment(UniquePtr<Metric>&)
    {}

    /// Increment a gauge, if they are defined.
    inline
    void decrement(UniquePtr<Gauge>&)
    {}
  }
}
#endif
