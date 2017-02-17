#include <infinit/model/Endpoints.hh>
#include <infinit/model/Model.hh>

namespace infinit
{
  namespace grpc
  {
    void serve_grpc(infinit::model::Model& dht, model::Endpoint ep);
  }
}