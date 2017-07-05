#ifndef INFINIT_MODEL_PARANOID_PARANOID_HH
# define INFINIT_MODEL_PARANOID_PARANOID_HH

# include <memory>

# include <elle/cryptography/rsa/KeyPair.hh>

# include <memo/model/Model.hh>

namespace memo
{
  namespace model
  {
    namespace paranoid
    {
      class Paranoid
        : public Model
      {
      public:
        Paranoid(elle::cryptography::rsa::KeyPair keys,
                 std::unique_ptr<silo::Silo> storage,
                 elle::Version version);
        virtual
        ~Paranoid();

      protected:
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const override;
        void
        _insert(std::unique_ptr<blocks::Block> block,
                std::unique_ptr<ConflictResolver> resolver) override;
        void
        _update(std::unique_ptr<blocks::Block> block,
                std::unique_ptr<ConflictResolver> resolver) override;
        virtual
        void
        _remove(Address address, blocks::RemoveSignature) override;
        ELLE_ATTRIBUTE_R(elle::cryptography::rsa::KeyPair, keys);
        ELLE_ATTRIBUTE_R(std::unique_ptr<silo::Silo>, storage);
      };
    }
  }
}

#endif
