#pragma once

#include <memory>

#include <memo/grpc/grpc.hh>

namespace memo
{
  namespace grpc
  {
    void
    serve_memo_vs(model::Model& model,
                  std::string const& ep,
                  int* effective_port = nullptr);
  }
}
