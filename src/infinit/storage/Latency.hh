#pragma once

#include <deque>

#include <elle/reactor/scheduler.hh>

#include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Latency: public Storage
    {
    public:
      Latency(std::unique_ptr<Storage> backend,
              elle::reactor::DurationOpt latency_get,
              elle::reactor::DurationOpt latency_set,
              elle::reactor::DurationOpt latency_erase);
      std::string
      type() const override { return "latency"; }

    protected:
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;

    private:
      std::unique_ptr<Storage> _backend;
      elle::reactor::DurationOpt _latency_get;
      elle::reactor::DurationOpt _latency_set;
      elle::reactor::DurationOpt _latency_erase;
    };
  }
}
