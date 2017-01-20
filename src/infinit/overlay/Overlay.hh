#ifndef INFINIT_OVERLAY_OVERLAY_HH
# define INFINIT_OVERLAY_OVERLAY_HH

# include <unordered_map>

# include <elle/Clonable.hh>
# include <elle/json/json.hh>
# include <elle/log.hh>

# include <reactor/network/tcp-socket.hh>
# include <reactor/Generator.hh>

# include <infinit/model/Address.hh>
# include <infinit/model/Endpoints.hh>
# include <infinit/model/doughnut/fwd.hh>
# include <infinit/model/doughnut/protocol.hh>
# include <infinit/serialization.hh>

namespace infinit
{
  namespace overlay
  {
    using model::Endpoints;
    using model::NodeLocation;
    using model::NodeLocations;

    class Overlay
      : public elle::Printable
    {
    /*------.
    | Types |
    `------*/
    public:
      typedef std::shared_ptr<model::doughnut::Peer> Member;
      typedef std::ambivalent_ptr<model::doughnut::Peer> WeakMember;
      typedef std::vector<Member> Members;

    /*-------------.
    | Construction |
    `-------------*/
    public:
      Overlay(model::doughnut::Doughnut* dht,
              std::shared_ptr<infinit::model::doughnut::Local> local);
      virtual
      ~Overlay();
      ELLE_ATTRIBUTE_R(model::doughnut::Doughnut*, doughnut);
      ELLE_attribute_r(model::Address, id);
      ELLE_ATTRIBUTE_R(std::shared_ptr<model::doughnut::Local>, local);

    /*------.
    | Peers |
    `------*/
    public:
      void
      discover(Endpoints const& peer);
      void
      discover(std::vector<Endpoints> const& peers);
      void
      discover(NodeLocation const& peer);
      void
      discover(NodeLocations const& peers);
      bool
      discovered(model::Address id);
    protected:
      virtual
      void
      _discover(NodeLocations const& peers) = 0;
      virtual
      bool
      _discovered(model::Address id) = 0;

    /*------.
    | Hooks |
    `------*/
    public:
      ELLE_ATTRIBUTE_RX(
        boost::signals2::signal<void (NodeLocation id,
                                      bool observer)>, on_discover);
      ELLE_ATTRIBUTE_RX(
        boost::signals2::signal<void (model::Address id,
                                      bool observer)>, on_disappear);

    /*-------.
    | Lookup |
    `-------*/
    public:
      /** Find owner for a new block
       *
       *  @arg address  Address of the blocks to place
       *  @arg n        How many owners to look for
       *  @arg fast     Whether to prefer a faster, partial answer.
       */
      reactor::Generator<WeakMember>
      allocate(model::Address address, int n) const;
      /// Lookup multiple addresses (OP_FETCH/UPDATE only)
      reactor::Generator<std::pair<model::Address, WeakMember>>
      lookup(std::vector<model::Address> const& addresses, int n) const;
      /** Lookup blocks
       *
       *  @arg address  Address of the blocks to search
       *  @arg n        How many owners to look for
       *  @arg fast     Whether to prefer a faster, partial answer.
       */
      reactor::Generator<WeakMember>
      lookup(model::Address address, int n, bool fast = false) const;
      /// Lookup a single block owner
      WeakMember
      lookup(model::Address address) const;
      /** Lookup a node from its id.
       *
       * @arg id Id of the node to lookup.
       * @raise elle::Error if the node is not found.
       */
      WeakMember
      lookup_node(model::Address id) const;
      /** Lookup nodes from their ids.
       *
       * @arg ids ids of the nodes to lookup.
       * @raise elle::Error if the node is not found.
       */
      reactor::Generator<WeakMember>
      lookup_nodes(std::unordered_set<model::Address> ids) const;
    protected:
      virtual
      reactor::Generator<WeakMember>
      _allocate(model::Address address, int n) const = 0;
      virtual
      reactor::Generator<std::pair<model::Address, WeakMember>>
      _lookup(std::vector<model::Address> const& addresses, int n) const;
      virtual
      reactor::Generator<WeakMember>
      _lookup(model::Address address, int n, bool fast) const = 0;
      /** Lookup a node by id
       *
       *  @raise elle::Error if the node cannot be found.
       */
      virtual
      WeakMember
      _lookup_node(model::Address address) const = 0;

    /*------.
    | Query |
    `------*/
    public:
      /// Query overlay specific informations.
      virtual
      elle::json::Json
      query(std::string const& k, boost::optional<std::string> const& v);

    /*-----------.
    | Monitoring |
    `-----------*/
    public:
      virtual
      std::string
      type_name() = 0;
      virtual
      elle::json::Array
      peer_list() = 0;
      virtual
      elle::json::Object
      stats() = 0;

    /*----------.
    | Printable |
    `----------*/
    public:
      void
      print(std::ostream& o) const override;
    };

    struct Configuration
      : public elle::serialization::VirtuallySerializable<false>
      , public elle::Clonable<Configuration>
    {
      model::doughnut::Protocol rpc_protocol;

      Configuration();
      Configuration(elle::serialization::SerializerIn& input);
      static constexpr char const* virtually_serializable_key = "type";
      /// Perform any initialization required at join time.
      // virtual
      // void
      // join();
      void
      serialize(elle::serialization::Serializer& s) override;
      typedef infinit::serialization_tag serialization_tag;
      virtual
      std::unique_ptr<infinit::overlay::Overlay>
      make(std::shared_ptr<model::doughnut::Local> local,
           model::doughnut::Doughnut* doughnut) = 0;
    };

    /*-----------.
    | Exceptions |
    `-----------*/

    class NodeNotFound
      : public elle::Error
    {
    public:
      NodeNotFound(model::Address id);
      ELLE_ATTRIBUTE_R(model::Address, id);
    };
  }
}

#endif
