#pragma once

#if INFINIT_ENABLE_PROMETHEUS
# include <memory>
# include <string>

# include <prometheus/family.h>
# include <prometheus/gauge.h>

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

    class Prometheus
    {
    public:
      /// Create a family of gauges.
      Family<Gauge>*
      make_gauge_family(std::string const& name,
                        std::string const& help);

      /// Create a new member to a family.
      /// Should be removed eventually.
      UniquePtr<Gauge>
      make(Family<Gauge>* family,
           Labels const& labels);
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
