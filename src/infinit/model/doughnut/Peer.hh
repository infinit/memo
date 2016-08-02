#ifndef INFINIT_MODEL_DOUGHNUT_PEER_HH
# define INFINIT_MODEL_DOUGHNUT_PEER_HH

# include <elle/Duration.hh>

# include <infinit/model/blocks/Block.hh>
# include <infinit/model/doughnut/fwd.hh>
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
        Peer(Doughnut& dht, Address id);
        virtual
        ~Peer();
        ELLE_ATTRIBUTE_R(Doughnut&, doughnut, protected);
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
        std::unique_ptr<blocks::Block>
        fetch(Address address,
              boost::optional<int> local_version) const;
        virtual
        void
        remove(Address address, blocks::RemoveSignature rs) = 0;
      protected:
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const = 0;

      /*----------.
      | Printable |
      `----------*/
      public:
        /// Print pretty representation to \a stream.
        virtual
        void
        print(std::ostream& stream) const override;
      };
    }
  }
}

#endif
