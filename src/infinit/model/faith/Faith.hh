#ifndef INFINIT_MODEL_FAITH_FAITH_HH
# define INFINIT_MODEL_FAITH_FAITH_HH

# include <memory>

# include <infinit/model/Model.hh>

namespace infinit
{
  namespace model
  {
    namespace faith
    {
      class Faith
        : public Model
      {
      public:
        Faith(std::unique_ptr<storage::Storage> storage);
      protected:
        virtual
        void
        _store(std::unique_ptr<blocks::Block> block,
               StoreMode mode,
               std::unique_ptr<ConflictResolver> resolver) override;
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address) const override;
        virtual
        void
        _remove(Address address) override;
        ELLE_ATTRIBUTE_R(std::unique_ptr<storage::Storage>, storage);
      };
    }
  }
}

#endif
