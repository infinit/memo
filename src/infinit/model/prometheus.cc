#if INFINIT_ENABLE_PROMETHEUS
# include <infinit/model/prometheus.hh>

# include <boost/exception/diagnostic_information.hpp>

# include <elle/log.hh>
# include <elle/os/environ.hh>

# include <prometheus/exposer.h>
# include <prometheus/registry.h>

ELLE_LOG_COMPONENT("infinit.prometheus");

namespace
{
  auto prometheus_endpoint
    = elle::os::getenv("INFINIT_PROMETHEUS_ENDPOINT", "127.0.0.1:8080");
}

namespace infinit
{
  namespace prometheus
  {
    void endpoint(std::string e)
    {
      ::prometheus_endpoint = std::move(e);
    }

    std::string const& endpoint()
    {
      return ::prometheus_endpoint;
    }

    Prometheus& instance()
    {
      static auto res = Prometheus();
      return res;
    }

    /// An HTTP server to answer Prometheus's requests.
    ///
    /// Maybe nullptr if set up failed.
    ::prometheus::Exposer*
    exposer()
    {
      static auto res = []() -> std::unique_ptr<::prometheus::Exposer> {
        auto const addr = endpoint();
        if (addr != "no" || addr != "0")
          try
          {
            ELLE_LOG("exposer: create: %s", addr);
            auto res = std::make_unique<::prometheus::Exposer>(addr);
            ELLE_LOG("exposer: creation succeeded");
            return res;
          }
          catch (std::exception const& e)
          {
            ELLE_LOG("exposer: creation failed,"
                     " metrics will not be exposed: %s", e);
          }
          catch (...)
          {
            ELLE_LOG("exposer: creation failed with unknown"
                     " exception type: %s",
                     boost::current_exception_diagnostic_information());
          }
        return {};
      }();
      return res.get();
    }

    /// Where to register the measurements to expose to Prometheus.
    std::shared_ptr<::prometheus::Registry>
    registry()
    {
      // Build and ask the exposer to scrape the registry on incoming
      // scrapes.
      static auto res = []() -> std::shared_ptr<::prometheus::Registry>
      {
        if (auto e = exposer())
        {
          auto res = std::make_shared<::prometheus::Registry>();
          e->RegisterCollectable(res);
          return res;
        }
        else
          return nullptr;
      }();
      return res;
    }

    auto
    Prometheus::make_gauge_family(std::string const& name,
                                  std::string const& help)
      -> Family<Gauge>*
    {
      // Add a new member gauge family to the registry.
      if (auto reg = registry())
      {
        auto& res = ::prometheus::BuildGauge()
          .Name(name)
          .Help(help)
          .Register(*reg);
        return &res;
      }
      else
        return {};
    }

    auto
    Prometheus::make(Family<Gauge>* family,
                     Labels const& labels)
      -> UniquePtr<Gauge>
    {
      if (family)
        return {&family->Add(labels), Deleter<Gauge>{family}};
      else
        return {};
    }
  }
}
#endif
