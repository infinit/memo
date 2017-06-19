#include <elle/reactor/fwd.hh>

#include <boost/optional.hpp>

#include <infinit/model/Endpoints.hh>
#include <infinit/model/Model.hh>

namespace grpc
{
  class Service;
}

namespace infinit
{
  namespace grpc
  {
    void
    serve_grpc(infinit::model::Model& dht,
               std::string const& ep,
               int* effective_port = nullptr);
    std::unique_ptr<::grpc::Service>
    doughnut_service(infinit::model::Model& dht);

    /**
     *  GRPC tasks (invoked by grpc callbacks) should acquire a Task
     * from the callback thread, and abort if proceed() returns false
    */
    class Task
    {
    public:
      Task();
      ~Task();
    public:
      bool
      proceed() const;
    private:
      bool _proceed;
    };
  }
}
