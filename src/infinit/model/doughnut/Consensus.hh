#ifndef INFINIT_MODEL_DOUGHNUT_CONSENSUS_HH
# define INFINIT_MODEL_DOUGHNUT_CONSENSUS_HH

# include <infinit/model/doughnut/fwd.hh>
# include <infinit/model/doughnut/Peer.hh>
# include <infinit/overlay/Overlay.hh>

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
          virtual ~Consensus() {}
          ELLE_ATTRIBUTE_R(Doughnut&, doughnut);

        /*-------.
        | Blocks |
        `-------*/
        public:
          void
          store(overlay::Overlay& overlay,
                std::unique_ptr<blocks::Block> block,
                StoreMode mode,
                std::unique_ptr<ConflictResolver> resolver);
          std::unique_ptr<blocks::Block>
          fetch(overlay::Overlay& overlay, Address address,
                boost::optional<int> local_version = {});
          void
          remove(overlay::Overlay& overlay, Address address);
          static
          std::unique_ptr<blocks::Block>
          fetch_from_members(
            reactor::Generator<overlay::Overlay::Member>& peers,
            Address address,
            boost::optional<int> local_version);
          void
          remove_many(overlay::Overlay& overlay, Address address, int factor);
        protected:
          virtual
          void
          _store(overlay::Overlay& overlay,
                 std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver);
          virtual
          std::unique_ptr<blocks::Block>
          _fetch(overlay::Overlay& overlay, Address address,
                 boost::optional<int> local_version);
          virtual
          void
          _remove(overlay::Overlay& overlay, Address address);
          std::shared_ptr<Peer>
          _owner(overlay::Overlay& overlay,
                 Address const& address,
                 overlay::Operation op) const;

        /*--------.
        | Factory |
        `--------*/
        public:
          virtual
          std::unique_ptr<Local>
          make_local(boost::optional<int> port,
                     std::unique_ptr<storage::Storage> storage);

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
          : public elle::serialization::VirtuallySerializable
        {
        /*--------.
        | Factory |
        `--------*/
        public:
          Configuration() = default;
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
