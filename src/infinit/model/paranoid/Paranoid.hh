#ifndef INFINIT_MODEL_PARANOID_PARANOID_HH
# define INFINIT_MODEL_PARANOID_PARANOID_HH

# include <memory>

# include <cryptography/rsa/KeyPair.hh>

# include <infinit/model/Model.hh>
# include <infinit/storage/Storage.hh>

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
                 std::unique_ptr<storage::Storage> storage);
        virtual
        ~Paranoid();

      protected:
        virtual
        void
        _store(blocks::Block& block, StoreMode mode, ConflictResolver resolver) override;
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address) const override;
        virtual
        void
        _remove(Address address) override;
        ELLE_ATTRIBUTE_R(infinit::cryptography::rsa::KeyPair, keys);
        ELLE_ATTRIBUTE_R(std::unique_ptr<storage::Storage>, storage);
      };
    }
  }
}

#endif
