#ifndef INFINIT_MODEL_DOUGHNUT_CONSENSUS_HH
# define INFINIT_MODEL_DOUGHNUT_CONSENSUS_HH

# include <elle/Clonable.hh>

# include <infinit/model/doughnut/Peer.hh>
# include <infinit/model/doughnut/fwd.hh>
# include <infinit/model/doughnut/protocol.hh>
# include <infinit/overlay/Overlay.hh>

# ifdef stat
#  undef stat
# endif

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
          Consensus(Doughnut& doughnut);
          virtual
          ~Consensus() = default;
          ELLE_ATTRIBUTE_R(Doughnut&, doughnut);

        /*-------.
        | Blocks |
        `-------*/
        public:
          typedef std::pair<Address, boost::optional<int>> AddressVersion;
          void
          store(std::unique_ptr<blocks::Block> block,
                StoreMode mode,
                std::unique_ptr<ConflictResolver> resolver);
          void
          fetch(std::vector<AddressVersion> const& addresses,
                std::function<void(Address, std::unique_ptr<blocks::Block>,
                                   std::exception_ptr)> res);
          std::unique_ptr<blocks::Block>
          fetch(Address address, boost::optional<int> local_version = {});
          void
          remove(Address address, blocks::RemoveSignature rs);
          static
          std::unique_ptr<blocks::Block>
          fetch_from_members(
            reactor::Generator<overlay::Overlay::WeakMember>& peers,
            Address address,
            boost::optional<int> local_version);
          void
          remove_many(Address address,
                      blocks::RemoveSignature rs,
                      int factor);
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
                 std::function<void(Address, std::unique_ptr<blocks::Block>,
                                    std::exception_ptr)> res);
          virtual
          void
          _remove(Address address, blocks::RemoveSignature rs);

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
                     std::unique_ptr<storage::Storage> storage,
                     Protocol p);


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

        /*--------------.
        | Configuration |
        `--------------*/

        class Configuration
          : public elle::serialization::VirtuallySerializable<false>
          , public elle::Clonable<Configuration>
        {
        /*--------.
        | Factory |
        `--------*/
        public:
          Configuration() = default;
          virtual
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
          virtual
          void
          serialize(elle::serialization::Serializer& s) override;
          typedef infinit::serialization_tag serialization_tag;
        };
      }
    }
  }
}

#endif
