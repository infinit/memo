#pragma once

#include <elle/Clonable.hh>

#include <infinit/model/Model.hh>
#include <infinit/model/doughnut/Peer.hh>
#include <infinit/model/doughnut/fwd.hh>
#include <infinit/model/doughnut/protocol.hh>
#include <infinit/overlay/Overlay.hh>

#ifdef stat
# undef stat
#endif

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        class Consensus
          : public elle::Printable
        {
        public:
          using ReceiveBlock = Model::ReceiveBlock;
          Consensus(Doughnut& doughnut);
          virtual
          ~Consensus() = default;
          ELLE_ATTRIBUTE_R(Doughnut&, doughnut);

        /*-------.
        | Blocks |
        `-------*/
        public:
          using AddressVersion = std::pair<Address, boost::optional<int>>;
          void
          store(std::unique_ptr<blocks::Block> block,
                StoreMode mode,
                std::unique_ptr<ConflictResolver> resolver);
          void
          fetch(std::vector<AddressVersion> const& addresses,
                ReceiveBlock res);
          std::unique_ptr<blocks::Block>
          fetch(Address address, boost::optional<int> local_version = {});
          void
          remove(Address address, blocks::RemoveSignature rs);
          using MemberGenerator = overlay::Overlay::MemberGenerator;
          static
          std::unique_ptr<blocks::Block>
          fetch_from_members(MemberGenerator& peers,
                             Address address,
                             boost::optional<int> local_version);
          void
          remove_many(Address address,
                      blocks::RemoveSignature rs,
                      int factor);
          void
          resign();
        protected:
          virtual
          void
          _store(std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver);
          virtual
          std::unique_ptr<blocks::Block>
          _fetch(Address address, boost::optional<int> local_version);
          virtual
          void
          _fetch(std::vector<AddressVersion> const& addresses,
                 ReceiveBlock res);
          virtual
          void
          _remove(Address address, blocks::RemoveSignature rs);
          virtual
          void
          _resign();

        /*-----.
        | Stat |
        `-----*/
        public:
          class Stat
          {
          public:
            virtual
            void
            serialize(elle::serialization::Serializer& s);
          };
          virtual
          std::unique_ptr<Stat>
          stat(Address const& address);

        /*--------.
        | Factory |
        `--------*/
        public:
          virtual
          std::unique_ptr<Local>
          make_local(boost::optional<int> port,
                     boost::optional<boost::asio::ip::address> listen_address,
                     std::unique_ptr<silo::Storage> storage);


        /*-----------.
        | Monitoring |
        `-----------*/
        public:
          virtual
          elle::json::Object
          redundancy();
          virtual
          elle::json::Object
          stats();

        /*----------.
        | Printable |
        `----------*/
        public:
          void
          print(std::ostream&) const override;
        };

        class StackedConsensus
          : public Consensus
        {
        public:
          StackedConsensus(std::unique_ptr<Consensus> backend);
          template<typename C>
          static
          C*
          find(Consensus* top);
          ELLE_ATTRIBUTE_R(std::unique_ptr<Consensus>, backend, protected);
        };

        /*--------------.
        | Configuration |
        `--------------*/

        class Configuration
          : public elle::serialization::VirtuallySerializable<Configuration, false>
          , public elle::Clonable<Configuration>
        {
        /*--------.
        | Factory |
        `--------*/
        public:
          Configuration() = default;

          std::unique_ptr<Configuration>
          clone() const override;
          virtual
          std::unique_ptr<Consensus>
          make(model::doughnut::Doughnut& dht);

        /*--------------.
        | Serialization |
        `--------------*/
        public:
          Configuration(elle::serialization::SerializerIn& s);
          static constexpr char const* virtually_serializable_key = "type";

          void
          serialize(elle::serialization::Serializer& s) override;
          using serialization_tag = infinit::serialization_tag;
        };
      }
    }
  }
}

# include <infinit/model/doughnut/Consensus.hxx>
