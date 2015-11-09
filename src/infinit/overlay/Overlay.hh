#ifndef INFINIT_OVERLAY_OVERLAY_HH
# define INFINIT_OVERLAY_OVERLAY_HH

# include <unordered_map>

# include <elle/json/json.hh>

# include <reactor/network/tcp-socket.hh>
# include <reactor/Generator.hh>

# include <infinit/model/Address.hh>
# include <infinit/model/doughnut/fwd.hh>
# include <infinit/serialization.hh>

namespace infinit
{
  namespace overlay
  {
    typedef std::unordered_map<model::Address, std::vector<std::string>>
      NodeEndpoints;

    enum Operation
    {
      OP_FETCH,
      OP_INSERT,
      OP_UPDATE,
      OP_INSERT_OR_UPDATE, // for cases where we're not sure
      OP_REMOVE
    };

    class Overlay
    {
    /*------.
    | Types |
    `------*/
    public:
      typedef std::shared_ptr<model::doughnut::Peer> Member;
      typedef std::vector<Member> Members;

    /*-------------.
    | Construction |
    `-------------*/
    public:
      Overlay(model::Address node_id);
      virtual
      ~Overlay() {}
      virtual
      void
      register_local(std::shared_ptr<model::doughnut::Local> local);
      ELLE_ATTRIBUTE_R(model::Address, node_id);
      ELLE_ATTRIBUTE_RWX(model::doughnut::Doughnut*, doughnut);

    /*-------.
    | Lookup |
    `-------*/
    public:
      /// Lookup a list of nodes
      reactor::Generator<Member>
      lookup(model::Address address, int n, Operation op) const;
      /// Lookup a single node
      Member
      lookup(model::Address address, Operation op) const;
      /// Lookup a node from its uid
      Member
      lookup_node(model::Address address);
      /// Lookup nodes from uids
      reactor::Generator<Overlay::Member>
      lookup_nodes(std::unordered_set<model::Address> address);
    protected:
      virtual
      reactor::Generator<Member>
      _lookup(model::Address address, int n, Operation op) const = 0;
      virtual
      Member
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
      : public elle::serialization::VirtuallySerializable
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
      make(model::Address id, NodeEndpoints const&, bool server,
           model::doughnut::Doughnut* doughnut) = 0;
    };
  }
}

#endif
