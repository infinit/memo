#include <infinit/model/faith/Faith.hh>

#include <boost/uuid/random_generator.hpp>

#include <elle/log.hh>
#include <elle/serialization/binary.hh>

#include <elle/cryptography/hash.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/silo/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.faith.Faith");

using Serializer = elle::serialization::Binary;

namespace infinit
{
  namespace model
  {
    namespace faith
    {
      Faith::Faith(std::unique_ptr<silo::Storage> storage,
                   boost::optional<elle::Version> version)
        : Model(std::move(version))
        , _storage(std::move(storage))
      {}

      std::unique_ptr<blocks::Block>
      Faith::_fetch(Address address, boost::optional<int>) const
      {
        ELLE_TRACE_SCOPE("%s: fetch block at %x", *this, address);
        try
        {
          auto data = this->_storage->get(address);
          return elle::serialization::deserialize<
            Serializer, std::unique_ptr<blocks::Block>>(data, false);
        }
        catch (infinit::silo::MissingKey const&)
        {
          throw MissingBlock(address);
        }
      }

      void
      Faith::_insert(std::unique_ptr<blocks::Block> block,
                     std::unique_ptr<ConflictResolver> resolver)
      {
        ELLE_TRACE_SCOPE("%s: insert %f", *this, *block);
        this->_storage->set(
          block->address(),
          elle::serialization::serialize<Serializer>(block, false),
          true,
          false);
      }

      void
      Faith::_update(std::unique_ptr<blocks::Block> block,
                     std::unique_ptr<ConflictResolver> resolver)
      {
        ELLE_TRACE_SCOPE("%s: update %f", *this, *block);
        this->_storage->set(
          block->address(),
          elle::serialization::serialize<Serializer>(block, false),
          false,
          true);
      }

      void
      Faith::_remove(Address address, blocks::RemoveSignature)
      {
        ELLE_TRACE_SCOPE("%s: remove block at %x", *this, address);
        try
        {
          this->_storage->erase(address);
        }
        catch (infinit::silo::MissingKey const&)
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
        make(bool,
             boost::filesystem::path const&) override
        {
          return std::make_unique<infinit::model::faith::Faith>(
            this->storage->make(), this->version);
        }
      };

      static const elle::serialization::Hierarchy<ModelConfig>::
      Register<FaithModelConfig> _register_FaithModelConfig("faith");
    }
  }
}
