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
      ELLE_TRACE("setting endpoint to %s", e);
      ::prometheus_endpoint = std::move(e);
      instance().bind(::prometheus_endpoint);
    }

    std::string const& endpoint()
    {
      return ::prometheus_endpoint;
    }

    Prometheus& instance()
    {
      static auto res = Prometheus(endpoint());
      return res;
    }

    Prometheus::Prometheus(std::string const& addr)
    {
      bind(addr);
    }

    void
    Prometheus::bind(std::string const& addr)
    {
      if (addr != "no" && addr != "0")
        try
        {
          if (_exposer)
          {
            ELLE_LOG("exposer: rebind: %s", addr);
            _exposer->rebind(addr);
          }
          else
          {
            ELLE_LOG("exposer: create: %s", addr);
            _exposer = std::make_unique<::prometheus::Exposer>(addr);
          }
        }
        catch (std::exception const& e)
        {
          ELLE_LOG("exposer: creation failed,"
                   " metrics will not be exposed: %s", e);
          _exposer.reset();
        }
        catch (...)
        {
          ELLE_LOG("exposer: creation failed with unknown"
                   " exception type: %s",
                   boost::current_exception_diagnostic_information());
          _exposer.reset();
        }
    }

    /// An HTTP server to answer Prometheus' requests.
    ///
    /// Maybe nullptr if set up failed.
    ::prometheus::Exposer*
    exposer()
    {
      return instance()._exposer.get();
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
      ELLE_TRACE("creating gauge family %s", name);
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
      ELLE_TRACE("creating gauge: %s", labels);
      if (family)
        return {&family->Add(labels), Deleter<Gauge>{family}};
      else
        return {};
    }
  }
}
#endif
