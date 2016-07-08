#ifndef INFINIT_MODEL_DOUGHNUT_CONFLICT_UBUPERSERTER_HH
# define INFINIT_MODEL_DOUGHNUT_CONFLICT_UBUPERSERTER_HH

# include <infinit/model/Model.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      struct UserBlockUpserter
        : public DummyConflictResolver
      {
        typedef DummyConflictResolver Super;

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
        typedef DummyConflictResolver Super;

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

#endif
