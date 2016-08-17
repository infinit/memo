#include <infinit/model/faith/Faith.hh>

#include <boost/uuid/random_generator.hpp>

#include <elle/log.hh>
#include <elle/serialization/binary.hh>

#include <cryptography/hash.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.faith.Faith");

typedef elle::serialization::Binary Serializer;

namespace infinit
{
  namespace model
  {
    namespace faith
    {
      Faith::Faith(std::unique_ptr<storage::Storage> storage,
                   boost::optional<elle::Version> version)
        : Model(std::move(version))
        , _storage(std::move(storage))
      {}

      void
      Faith::_store(std::unique_ptr<blocks::Block> block,
                    StoreMode mode,
                    std::unique_ptr<ConflictResolver> resolver)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, *block);
        elle::Buffer data;
        {
          elle::IOStream s(data.ostreambuf());
          Serializer::SerializerOut output(s, false);
          output.serialize_forward(block);
        }
        this->_storage->set(block->address(),
                            data,
                            mode == STORE_INSERT,
                            mode == STORE_UPDATE);
      }

      std::unique_ptr<blocks::Block>
      Faith::_fetch(Address address, boost::optional<int>) const
      {
        ELLE_TRACE_SCOPE("%s: fetch block at %x", *this, address);
        try
        {
          auto data = this->_storage->get(address);
          elle::IOStream s(data.istreambuf());
          Serializer::SerializerIn input(s, false);
          auto res = input.deserialize<std::unique_ptr<blocks::Block>>();
          return res;
        }
        catch (infinit::storage::MissingKey const&)
        {
          throw MissingBlock(address);
        }
      }

      void
      Faith::_remove(Address address, blocks::RemoveSignature)
      {
        ELLE_TRACE_SCOPE("%s: remove block at %x", *this, address);
        try
        {
          this->_storage->erase(address);
        }
        catch (infinit::storage::MissingKey const&)
        {
          throw MissingBlock(address);
        }
      }

      struct FaithModelConfig:
        public ModelConfig
      {
      public:
        FaithModelConfig(elle::serialization::SerializerIn& input,
                         elle::Version version)
          : ModelConfig(nullptr, std::move(version))
        {
          this->serialize(input);
        }

        void
        serialize(elle::serialization::Serializer& s) override
        {
          ModelConfig::serialize(s);
        }

        virtual
        std::unique_ptr<infinit::model::Model>
        make(std::vector<Endpoints> const&,
             bool,
             boost::filesystem::path const&) override
        {
          return elle::make_unique<infinit::model::faith::Faith>(
            this->storage->make(), this->version);
        }
      };

      static const elle::serialization::Hierarchy<ModelConfig>::
      Register<FaithModelConfig> _register_FaithModelConfig("faith");
    }
  }
}
