#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>

#include <elle/serialization/json.hh>
#include <elle/serialization/binary/SerializerIn.hh>
#include <elle/serialization/binary/SerializerOut.hh>
#include <elle/bench.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Cache");


namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      static Cache::TimePoint now()
      {
        return Cache::Clock::now();
      }

      Cache::Cache(Doughnut& doughnut, std::unique_ptr<Consensus> backend,
                   Duration mut_cache_ttl,
                   long const_cache_size)
      : Consensus(doughnut)
      , _backend(std::move(backend))
      , _mut_cache_ttl(mut_cache_ttl)
      , _const_cache_size(const_cache_size)
      {
      }

      Cache::~Cache()
      {
      }

      void
      Cache::_remove(overlay::Overlay& overlay, Address address)
      {
        _cleanup();
        auto hit = _cache.find(address);
        if (hit != _cache.end())
        {
          auto t = hit->second.first;
          if (dynamic_cast<blocks::ImmutableBlock*>(hit->second.second.get()))
            _const_cache_time.erase(t);
          else
            _mut_cache_time.erase(t);
          _cache.erase(hit);
        }
        _backend->remove(overlay, address);
      }
      std::unique_ptr<blocks::Block>
      Cache::_fetch(overlay::Overlay& overlay, Address address)
      {
        static elle::Bench bench_hit("cache.hit", 10_sec);
        _cleanup();
        auto hit = _cache.find(address);
        if (hit != _cache.end())
        {
          if (dynamic_cast<blocks::ImmutableBlock*>(hit->second.second.get()))
          {
            auto it = _const_cache_time.find(hit->second.first);
            ELLE_ASSERT(it != _const_cache_time.end());
            ELLE_ASSERT_EQ(address, it->second);
            _const_cache_time.erase(it);
            auto t = now();
            _const_cache_time.insert(std::make_pair(t, address));
            hit->second.first = t;
          }
          bench_hit.add(1);
          return _copy(*hit->second.second);
        }
        bench_hit.add(0);
        auto res = _backend->fetch(overlay, address);
        auto t = now();
        _cache.insert(std::make_pair(address, std::make_pair(t, _copy(*res))));
        if (dynamic_cast<blocks::ImmutableBlock*>(res.get()))
        {
          auto ir = _const_cache_time.insert(std::make_pair(t, address));
          ELLE_ASSERT(ir.second);
        }
        else
        {
          auto ir = _mut_cache_time.insert(std::make_pair(t, address));
          ELLE_ASSERT(ir.second);
        }
        return res;
      }
      void
      Cache::_store(overlay::Overlay& overlay,
                    blocks::Block& block,
                    StoreMode mode,
                    ConflictResolver resolver)
      {
        _cleanup();
        auto hit = _cache.find(block.address());
        if (hit != _cache.end())
        {
          TimePoint t = hit->second.first;
          if (dynamic_cast<blocks::ImmutableBlock*>(&block))
            ; // nothing to do, this does not count as a use
          else
          { // but counts as a refresh
            _mut_cache_time.erase(t);
            t = now();
            _mut_cache_time.insert(std::make_pair(t, block.address()));
            hit->second.first = t;
          }
          hit->second.second = _copy(block);
        }
        else
        {
          auto t = now();
          if (dynamic_cast<blocks::ImmutableBlock*>(&block))
            _const_cache_time.insert(std::make_pair(t, block.address()));
          else
            _mut_cache_time.insert(std::make_pair(t, block.address()));
          _cache.insert(std::make_pair(block.address(),
            std::make_pair(t, _copy(block))));
        }
        _backend->store(overlay, block, mode, resolver);
      }
      void
      Cache::_cleanup()
      {
        auto t = now();
        while (!_mut_cache_time.empty())
        {
          auto b = _mut_cache_time.begin();
          if (!(t - b->first < _mut_cache_ttl))
          {
            _cache.erase(b->second);
            _mut_cache_time.erase(b); 
          }
          else
            break;
        }
        while (signed(_const_cache_time.size()) > _const_cache_size)
        {
          auto b = _const_cache_time.begin();
          _cache.erase(b->second);
          _const_cache_time.erase(b);
        }
        ELLE_ASSERT_EQ(_cache.size(), _const_cache_time.size() + _mut_cache_time.size());
        ELLE_DEBUG("cache: mut %s  const %s", _mut_cache_time.size(), _const_cache_time.size()); 
      }
      std::unique_ptr<blocks::Block>
      Cache::_copy(blocks::Block& block)
      {
        std::stringstream ss;
        elle::serialization::json::serialize(block, ss, false);
        elle::serialization::json::SerializerIn out(ss, false);
        out.set_context<Doughnut*>(&this->_doughnut);
        return out.deserialize<std::unique_ptr<blocks::Block>>();
        /*
        elle::Buffer buf;
        elle::IOStream os(buf.ostreambuf());
        {
          elle::serialization::binary::SerializerOut out(os, false);
          out.serialize_forward(block);
        }
        elle::IOStream is(buf.istreambuf());
        elle::serialization::binary::SerializerIn in(is, false);
        in.set_context<Doughnut*>(&this->_doughnut);
        return in.deserialize<std::unique_ptr<blocks::Block>>();*/
      }
    }
  }
}