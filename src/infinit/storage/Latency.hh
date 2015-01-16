#ifndef INFINIT_STORAGE_LATENCY_HH
#define INFINIT_STORAGE_LATENCY_HH

#include <deque>
#include <reactor/scheduler.hh>
#include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Latency: public Storage
    {
    public:
      Latency(std::unique_ptr<Storage> backend,
        reactor::DurationOpt latency_get,
        reactor::DurationOpt latency_set,
        reactor::DurationOpt latency_erase
        );
    protected:
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      void
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      void
      _erase(Key k) override;
    private:
      std::unique_ptr<Storage> _backend;
      reactor::DurationOpt _latency_get;
      reactor::DurationOpt _latency_set;
      reactor::DurationOpt _latency_erase;
    };
  }
}


#endif