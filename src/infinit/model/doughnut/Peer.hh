#ifndef INFINIT_MODEL_DOUGHNUT_PEER_HH
# define INFINIT_MODEL_DOUGHNUT_PEER_HH

# include <elle/Duration.hh>

# include <infinit/model/blocks/Block.hh>
# include <infinit/model/Model.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Peer
        : public elle::Printable
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Peer(Address id);
        virtual
        ~Peer();
        ELLE_ATTRIBUTE_R(Address, id);

      /*-----------.
      | Networking |
      `-----------*/
      public:
        virtual
        void
        connect(elle::DurationOpt timeout = elle::DurationOpt()) = 0;
        virtual
        void
        reconnect(elle::DurationOpt timeout = elle::DurationOpt()) = 0;

      /*-------.
      | Blocks |
      `-------*/
      public:
        virtual
        void
        store(blocks::Block const& block, StoreMode mode) = 0;
        virtual
        std::unique_ptr<blocks::Block>
        fetch(Address address) const = 0;
        virtual
        void
        remove(Address address) = 0;
      };
    }
  }
}

#endif
