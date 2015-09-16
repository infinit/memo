#include <infinit/model/doughnut/Replicator.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/MissingBlock.hh>

#include <elle/serialization/Serializer.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/binary/SerializerIn.hh>
#include <elle/serialization/binary/SerializerOut.hh>
#include <elle/serialization/json.hh>

#include <reactor/exception.hh>
#include <reactor/scheduler.hh>

#include <boost/filesystem/fstream.hpp>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Replicator");

namespace elle
{
  namespace serialization
  {
    typedef std::chrono::system_clock::time_point Time;
    template<> struct Serialize<Time>
    {
      typedef uint64_t Type;
      static uint64_t convert(Time& t)
      {
        Type res = std::chrono::duration_cast<std::chrono::milliseconds>(
          t.time_since_epoch()).count();
        return res;
      }
      static Time convert(uint64_t repr)
      {
        return Time(std::chrono::milliseconds(repr));
      }
    };
  }
}

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Replicator::Replicator(Doughnut& doughnut, int factor,
                             boost::filesystem::path const& journal_dir)
      : Consensus(doughnut)
      , _factor(factor)
      , _overlay(nullptr)
      , _journal_dir(journal_dir)
      , _process_thread("replicator", [&] { _process_loop();})
      {
        ELLE_LOG("using journal at %s", _journal_dir);
        boost::filesystem::create_directories(_journal_dir);
      }

      Replicator::~Replicator()
      {
        ELLE_TRACE("~Replicator");
        _process_thread.terminate_now();
        ELLE_TRACE("~~Replicator");
      }

      void
      Replicator::_process_loop()
      {
        while (true)
        {
          try
          {
            _process_cache();
          }
          catch (reactor::Terminate const&)
          {
            ELLE_TRACE("Terminating thread");
            throw;
          }
          catch (std::exception const& e)
          {
            ELLE_WARN("Exception escaped replicator loop: %s", e.what());
          }
          reactor::sleep(10_sec);
        }
      }

      void
      Replicator::_store(overlay::Overlay& overlay, blocks::Block& block, StoreMode mode)
      {
        _overlay = &overlay;
        overlay::Operation op;
        switch (mode)
        {
          case STORE_ANY:
            op = overlay::OP_INSERT_OR_UPDATE;
            break;
          case STORE_INSERT:
            op = overlay::OP_INSERT;
            break;
          case STORE_UPDATE:
            op = overlay::OP_UPDATE;
            break;
          default:
            elle::unreachable();
        }
        auto peers = overlay.lookup(block.address(), _factor, op, false);
        ELLE_TRACE("Overlay produced %s peers", peers.size());
        for (auto const& p: peers)
          p->store(block, mode);
        std::string saddress = elle::sprintf("%s", block.address());
        if (peers.size() < unsigned(_factor))
        {
          ELLE_TRACE("store with %s of %s nodes", peers.size(), _factor);
          boost::filesystem::ofstream ofs(_journal_dir / saddress);
          ofs << peers.size();
        }
        else
          boost::filesystem::remove(_journal_dir /saddress);
      }

      static
      std::unique_ptr<blocks::Block>
      fetch_from_members(overlay::Overlay::Members const& peers, Address address)
      {
        for (auto& p: peers)
        {
          try
          {
            return p->fetch(address);
          }
          catch(reactor::Terminate const& e)
          {
            throw;
          }
          catch(std::exception const& e)
          {
            ELLE_WARN("Replicator: candidate failed with %s", e.what());
          }
        }
        throw elle::Error("Replicator: All candidates failed.");
      }

      std::unique_ptr<blocks::Block>
      Replicator::_fetch(overlay::Overlay& overlay, Address address)
      {
        _overlay = &overlay;
        static bool redundancy = getenv("INFINIT_REPLICATOR_REDUNDANT_GET");
        auto peers = overlay.lookup(address, redundancy ? _factor : 1, overlay::OP_FETCH, false);
        if (signed(peers.size()) != _factor)
        {
          ELLE_TRACE("fetch with only %s of %s members", peers.size(), _factor);
        }
        return fetch_from_members(peers, address);
      }

      void
      Replicator::_remove(overlay::Overlay& overlay, Address address)
      {
        _overlay = &overlay;
        auto peers = overlay.lookup(address, _factor, overlay::OP_REMOVE, false);
        for (auto const& p: peers)
          p->remove(address);
      }

      void Replicator::_process_cache()
      {
        int network_size = 0;
        if (!_overlay)
          return;
        ELLE_TRACE("checking cache");
        boost::filesystem::directory_iterator it(_journal_dir);
        for (;it != boost::filesystem::directory_iterator(); ++it)
        {
          ELLE_TRACE("considering %s", it->path());
          Address address = Address::from_string(it->path().filename().string().substr(2));
          int known_replicas = 0;
          {
            boost::filesystem::ifstream ifs(it->path());
            ifs >> known_replicas;
          }
          if (network_size && known_replicas >= network_size)
          {
            ELLE_DEBUG("not dequeuing: network_size = %s", network_size);
            continue;
          }
          overlay::Overlay::Members peers;
          try
          {
            peers = _overlay->lookup(address, _factor, overlay::OP_FETCH, false);
          }
          catch (MissingBlock const&)
          { // assume block was deleted
            ELLE_TRACE("no hit on %s, assuming removed block", address);
            boost::filesystem::remove(it->path());
            continue;
          }
          ELLE_TRACE("Expected %s results, got %s for %s",
                     known_replicas, peers.size(), address);
          if (signed(peers.size()) >= _factor)
          {
            boost::filesystem::remove(it->path());
            continue;
          }
          std::unique_ptr<blocks::Block> block;
          try
          {
            block = fetch_from_members(peers, address);
          }
          catch (elle::Error const& e)
          {
            ELLE_WARN("Failed to fetch block data");
            continue;
          }
          int need = _factor - peers.size();
          try
          {
            peers = _overlay->lookup(address, need, overlay::OP_INSERT, false);
          }
          catch (MissingBlock const& b)
          {
            ELLE_TRACE("Got 0 out of %s peers", need);
            continue;
          }
          ELLE_TRACE("Got %s out of %s peers", peers.size(), need);
          for (auto const& p: peers)
          {
            p->store(*block, STORE_INSERT);
          }
          if (signed(peers.size()) == need)
          {
            boost::filesystem::remove(it->path());
          }
          else
          {
            int ns = peers.size() + (_factor - need);
            if (network_size == 0 || network_size < ns)
              network_size = ns;
          }
        }
        ELLE_TRACE("done checking cache");
      }
    }
  }
}