#pragma once

#include <memory>

#include <infinit/model/Model.hh>

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
        Faith(std::unique_ptr<silo::Silo> storage,
              boost::optional<elle::Version> version = {});

      protected:
        std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const override;
        void
        _insert(std::unique_ptr<blocks::Block> block,
                std::unique_ptr<ConflictResolver> resolver) override;
        void
        _update(std::unique_ptr<blocks::Block> block,
                std::unique_ptr<ConflictResolver> resolver) override;
        void
        _remove(Address address, blocks::RemoveSignature rs) override;
        ELLE_ATTRIBUTE_R(std::unique_ptr<silo::Silo>, storage);
      };
    }
  }
}
