#ifndef INFINIT_OVERLAY_OVERLAY_HH
# define INFINIT_OVERLAY_OVERLAY_HH

# include <unordered_map>

# include <elle/json/json.hh>
# include <elle/log.hh>


# include <reactor/network/tcp-socket.hh>
# include <reactor/Generator.hh>

# include <infinit/model/Address.hh>
# include <infinit/model/Endpoints.hh>
# include <infinit/model/doughnut/fwd.hh>
# include <infinit/serialization.hh>

namespace infinit
{
  namespace overlay
  {
    using model::Endpoints;
    using model::NodeLocation;
    using model::NodeLocations;

    enum Operation
    {
      OP_FETCH,
      OP_INSERT,
      OP_UPDATE,
      OP_REMOVE,
      OP_FETCH_FAST, ///< Fetch faster but can return a subset of requested nodes
    };

    class Overlay
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
              std::shared_ptr<infinit::model::doughnut::Local> local,
              model::Address node_id);
      virtual
      ~Overlay();
      ELLE_ATTRIBUTE_R(model::Address, node_id);
      ELLE_ATTRIBUTE_R(model::doughnut::Doughnut*, doughnut);
      ELLE_ATTRIBUTE_R(std::shared_ptr<model::doughnut::Local>, local);

    /*------.
    | Peers |
    `------*/
    public:
      void
      discover(NodeLocations const& peers);
    protected:
      virtual
      void
      _discover(NodeLocations const& peers) = 0;

    /*------.
    | Hooks |
    `------*/
    public:
      ELLE_ATTRIBUTE_RX(
        boost::signals2::signal<void (model::Address id,
                                      bool observer)>, on_discover);
      ELLE_ATTRIBUTE_RX(
        boost::signals2::signal<void (model::Address id,
                                      bool observer)>, on_disappear);

    /*-------.
    | Lookup |
    `-------*/
    public:
      /// Lookup multiple addresses (OP_FETCH/UPDATE only)
      reactor::Generator<std::pair<model::Address, WeakMember>>
      lookup(std::vector<model::Address> const& addresses, int n) const;
      /// Lookup a list of nodes
      reactor::Generator<WeakMember>
      lookup(model::Address address, int n, Operation op) const;
      /// Lookup a single node
      WeakMember
      lookup(model::Address address, Operation op) const;
      /** Lookup a node from its id.
       *
       * @arg id Id of the node to lookup.
       * @raise elle::Error if the node is not found.
       */
      WeakMember
      lookup_node(model::Address id);
      /** Lookup nodes from their ids.
       *
       * @arg ids ids of the nodes to lookup.
       * @raise elle::Error if the node is not found.
       */
      reactor::Generator<WeakMember>
      lookup_nodes(std::unordered_set<model::Address> ids);
    protected:
      virtual
      reactor::Generator<std::pair<model::Address, WeakMember>>
      _lookup(std::vector<model::Address> const& addresses, int n) const;
      virtual
      reactor::Generator<WeakMember>
      _lookup(model::Address address, int n, Operation op) const = 0;
      virtual
      WeakMember
      _lookup_node(model::Address address) = 0;

    /*------.
    | Query |
    `------*/
    public:
      /// Query overlay specific informations.
      virtual
      elle::json::Json
      query(std::string const& k, boost::optional<std::string> const& v);
    };

    struct Configuration
      : public elle::serialization::VirtuallySerializable<false>
    {
      Configuration() = default;
      Configuration(elle::serialization::SerializerIn& input);
      static constexpr char const* virtually_serializable_key = "type";
      /// Perform any initialization required at join time.
      // virtual
      // void
      // join();
      virtual
      void
      serialize(elle::serialization::Serializer& s) override;
      typedef infinit::serialization_tag serialization_tag;
      virtual
      std::unique_ptr<infinit::overlay::Overlay>
      make(model::Address id,
           std::vector<Endpoints> const&,
           std::shared_ptr<model::doughnut::Local> local,
           model::doughnut::Doughnut* doughnut) = 0;
    };
  }
}

#endif
