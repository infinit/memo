#pragma once

#include <memory>
#include <string>

#include <prometheus/registry.h>

namespace prometheus
{
  class Exposer;
}

namespace infinit
{
  namespace prometheus
  {
    /// Set the Prometheus publishing address.
    void prometheus_endpoint(std::string e);

    /// Get the Prometheus publishing address.
    std::string const& prometheus_endpoint();

    /// An HTTP server to answer Prometheus's requests.
    ///
    /// Maybe nullptr if set up failed.
    ::prometheus::Exposer*
    exposer();

    /// Where to register the measurements to expose to Prometheus.
    ///
    /// Maybe nullptr if set up failed.
    std::shared_ptr<::prometheus::Registry>
    registry();
  }
}
