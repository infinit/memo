#ifndef INFINIT_MODEL_PARANOID_PARANOID_HH
# define INFINIT_MODEL_PARANOID_PARANOID_HH

# include <memory>

# include <cryptography/rsa/KeyPair.hh>

# include <infinit/model/Model.hh>

namespace infinit
{
  namespace model
  {
    namespace paranoid
    {
      class Paranoid
        : public Model
      {
      public:
        Paranoid(infinit::cryptography::rsa::KeyPair keys,
                 std::unique_ptr<storage::Storage> storage,
                 boost::optional<elle::Version> version);
        virtual
        ~Paranoid();

      protected:
        virtual
        void
        _store(std::unique_ptr<blocks::Block> block,
               StoreMode mode,
               std::unique_ptr<ConflictResolver> resolver) override;
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const override;
        virtual
        void
        _remove(Address address, blocks::RemoveSignature) override;
        ELLE_ATTRIBUTE_R(infinit::cryptography::rsa::KeyPair, keys);
        ELLE_ATTRIBUTE_R(std::unique_ptr<storage::Storage>, storage);
      };
    }
  }
}

#endif
