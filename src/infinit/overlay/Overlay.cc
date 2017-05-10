#include <boost/range/algorithm_ext/erase.hpp>

#include <elle/log.hh>
#include <elle/make-vector.hh>

#include <elle/reactor/Scope.hh>

#include <infinit/overlay/Overlay.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/DummyPeer.hh>
#include <infinit/model/prometheus.hh>

ELLE_LOG_COMPONENT("infinit.overlay.Overlay");

namespace infinit
{
  namespace overlay
  {
    /*-------------.
    | Construction |
    `-------------*/

    Overlay::Overlay(model::doughnut::Doughnut* dht,
                     std::shared_ptr<infinit::model::doughnut::Local> local)
      : _doughnut(dht)
      , _local(local)
      , _storing(true)
    {
#if INFINIT_ENABLE_PROMETHEUS
      if (auto* g = _doughnut->member_gauge().get())
      {
        ELLE_LOG_COMPONENT("infinit.overlay.Overlay.prometheus");
        ELLE_TRACE("%s: construct, gauge: %s", this, g->Value());
        this->on_discovery().connect([this, g](NodeLocation, bool){
            ELLE_DEBUG("%s: signaling one more than %s",
                     this, g->Value());
            g->Increment();
          });
        this->on_disappearance().connect([this, g](model::Address, bool){
            ELLE_DEBUG("%s: signaling one less than %s",
                     this, g->Value());
            g->Decrement();
          });
      }
#endif
    }

    Overlay::~Overlay()
    {
      ELLE_TRACE("%s: destruct", this);
    }

    model::Address const&
    Overlay::id() const
    {
      return this->_doughnut->id();
    }

    elle::json::Json
    Overlay::query(std::string const& k, boost::optional<std::string> const& v)
    {
      return {};
    }

    void
    Overlay::cleanup()
    {
      ELLE_TRACE_SCOPE("%s: cleanup", this);
      this->_cleanup();
    }

    void
    Overlay::_cleanup()
    {}

    /*------.
    | Peers |
    `------*/

    void
    Overlay::discover(Endpoints const& peer)
    {
      this->discover(NodeLocation(model::Address::null, peer));
    }

    void
    Overlay::discover(std::vector<Endpoints> const& peers)
    {
      this->discover
        (elle::make_vector(peers,
                           [](auto const& eps)
                           {
                             return NodeLocation{model::Address::null, eps};
                           }));
    }

    void
    Overlay::discover(NodeLocation const& peer)
    {
      this->discover(NodeLocations{peer});
    }

    void
    Overlay::discover(NodeLocations const& peers_)
    {
      ELLE_TRACE_SCOPE("%s: discover %f", this, peers_);
      auto peers = peers_;
      auto it = boost::remove_erase_if(peers,
        [this] (NodeLocation const& nl)
        {
          bool is_us = nl.id() == this->doughnut()->id();
          if (is_us)
            ELLE_TRACE("%s: removing ourself from peer list", this);
          return is_us;
        });
      this->_discover(peers);
    }

    bool
    Overlay::discovered(model::Address id)
    {
      return this->_discovered(id);
    }

    /*-------.
    | Lookup |
    `-------*/

    auto
    Overlay::allocate(model::Address address, int n) const
      -> MemberGenerator
    {
      ELLE_TRACE_SCOPE("%s: allocate %s nodes for %f", this, n, address);
      return this->_allocate(address, n);
    }

    auto
    Overlay::lookup(std::vector<model::Address> const& addresses, int n) const
      -> LocationGenerator
    {
      ELLE_TRACE_SCOPE("%s: lookup %s nodes for %f", *this, n, addresses);
      return this->_lookup(addresses, n);
    }

    auto
    Overlay::lookup(model::Address address, int n, bool fast) const
      -> MemberGenerator
    {
      ELLE_TRACE_SCOPE("%s: lookup%s %s nodes for %f",
                       this, fast ? " (fast)" : "", n, address);
      return this->_lookup(address, n, fast);
    }

    auto
    Overlay::lookup(model::Address address) const
      -> WeakMember
    {
      ELLE_TRACE_SCOPE("%s: lookup 1 node for %f", this, address);
      for (auto res: this->_lookup(address, 1, false))
        return res;
      throw model::MissingBlock(address);
    }

    auto
    Overlay::_lookup(std::vector<model::Address> const& addresses, int n) const
      -> LocationGenerator
    {
      return [this, addresses, n](LocationGenerator::yielder const& yield)
        {
          for (auto const& a: addresses)
            for (auto res: this->_lookup(a, n, false))
              yield(std::make_pair(a, res));
        };
    }

    Configuration::Configuration(elle::serialization::SerializerIn& input)
    {
      this->serialize(input);
    }

    auto
    Overlay::lookup_node(model::Address address) const
      -> WeakMember
    {
      if (auto res = this->_lookup_node(address))
        return res;
      else
        throw NodeNotFound(address);
    }

    auto
    Overlay::lookup_nodes(std::unordered_set<model::Address> addresses) const
      -> MemberGenerator
    {
      ELLE_TRACE_SCOPE("%s: lookup nodes %f", this, addresses);
      return [this, addresses](MemberGenerator::yielder const& yield)
        {
          elle::reactor::for_each_parallel(
            addresses,
            [&] (model::Address const& address)
            {
              try
              {
                if (auto res = this->_lookup_node(address))
                {
                  yield(std::move(res));
                  return;
                }
              }
              catch (elle::Error const& e)
              {
                ELLE_TRACE("%s: failed to lookup node %f: %s",
                           this, address, e);
              }
              yield(WeakMember(new model::doughnut::DummyPeer(
                                 *this->doughnut(), address)));
            },
            "fetch node by address");
        };
    }

    /*----------.
    | Printable |
    `----------*/

    void
    Overlay::print(std::ostream& o) const
    {
      elle::fprintf(o, "%s(%f)", elle::type_info(*this), this->id());
    }


    /*--------------.
    | Configuration |
    `--------------*/

    void
    Configuration::serialize(elle::serialization::Serializer& s)
    {
      try
      {
        s.serialize("rpc_protocol", this->rpc_protocol);
      }
      catch (elle::serialization::Error const&)
      {}
    }

    /*-----------.
    | Exceptions |
    `-----------*/

    NodeNotFound::NodeNotFound(model::Address id)
      : elle::Error(elle::sprintf("node not found: %f", id))
      , _id(id)
    {}
  }
}
