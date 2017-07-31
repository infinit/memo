#pragma once

#include <memo/model/Model.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      struct UserBlockUpserter
        : public DummyConflictResolver
      {
        using Super = memo::model::DummyConflictResolver;

        UserBlockUpserter(std::string const& name);

        UserBlockUpserter(elle::serialization::Serializer& s,
                          elle::Version const& version);

        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;

        std::string
        description() const override;

      private:
        ELLE_ATTRIBUTE(std::string, name);
      };

      struct ReverseUserBlockUpserter
        : public DummyConflictResolver
      {
        using Super = memo::model::DummyConflictResolver;

        ReverseUserBlockUpserter(std::string const& name);

        ReverseUserBlockUpserter(elle::serialization::Serializer& s,
                                 elle::Version const& version);

        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;

        std::string
        description() const override;

      private:
        ELLE_ATTRIBUTE(std::string, name);
      };
    }
  }
}
