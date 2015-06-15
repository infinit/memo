#ifndef INFINIT_OVERLAY_OVERLAY_HH
# define INFINIT_OVERLAY_OVERLAY_HH

# include <reactor/network/tcp-socket.hh>

# include <infinit/model/Address.hh>

namespace infinit
{
  namespace overlay
  {
    class Overlay
    {
    /*------.
    | Types |
    `------*/
    public:
      typedef boost::asio::ip::tcp::endpoint Member;
      typedef std::vector<Member> Members;

    /*-------.
    | Lookup |
    `-------*/
    public:
      /// Lookup a list of nodes
      Members
      lookup(model::Address address, int n) const;
      /// Lookup a single node
      Member
      lookup(model::Address address) const;
    protected:
      virtual
      Members
      _lookup(model::Address address, int n) const = 0;
    };
  }
}

#endif
