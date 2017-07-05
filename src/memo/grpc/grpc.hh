#include <elle/reactor/fwd.hh>

#include <boost/optional.hpp>

#include <memo/model/Endpoints.hh>
#include <memo/model/Model.hh>

namespace grpc
{
  class Service;
}

namespace memo
{
  namespace grpc
  {
    void
    serve_grpc(memo::model::Model& dht,
               std::string const& ep,
               int* effective_port = nullptr);
    std::unique_ptr<::grpc::Service>
    doughnut_service(memo::model::Model& dht);

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
