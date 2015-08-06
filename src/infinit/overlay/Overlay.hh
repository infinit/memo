#ifndef INFINIT_OVERLAY_OVERLAY_HH
# define INFINIT_OVERLAY_OVERLAY_HH

# include <reactor/network/tcp-socket.hh>

# include <infinit/model/Address.hh>
# include <infinit/model/doughnut/fwd.hh>

namespace infinit
{
  namespace overlay
  {
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
      Overlay();

    /*-------.
    | Lookup |
    `-------*/
    public:
      /// Lookup a list of nodes
      Members
      lookup(model::Address address, int n, Operation op) const;
      /// Lookup a single node
      Member
      lookup(model::Address address, Operation op) const;

      virtual
      void
      register_local(std::shared_ptr<model::doughnut::Local> local);

      ELLE_ATTRIBUTE_RWX(model::doughnut::Doughnut*, doughnut);

    protected:
      virtual
      Members
      _lookup(model::Address address, int n, Operation op) const = 0;
    };

    struct Configuration
      : public elle::serialization::VirtuallySerializable
    {
      static constexpr char const* virtually_serializable_key = "type";

      virtual
      std::unique_ptr<infinit::overlay::Overlay>
      make(std::vector<std::string> const& hosts, bool server) = 0;
    };
  }
}

#endif
