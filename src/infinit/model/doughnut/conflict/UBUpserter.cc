#include <infinit/model/doughnut/conflict/UBUpserter.hh>

#include <elle/printf.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      UserBlockUpserter::UserBlockUpserter(std::string const& name)
        : _name(name)
      {
      }

      UserBlockUpserter::UserBlockUpserter(elle::serialization::Serializer& s,
                                           elle::Version const& version)
        : Super()
      {
        this->serialize(s, version);
      }

      void
      UserBlockUpserter::serialize(elle::serialization::Serializer& s,
                                   elle::Version const& version)
      {
        Super::serialize(s, version);
        s.serialize("name", this->_name);
      }

      std::string
      UserBlockUpserter::description() const
      {
        return elle::sprintf("write User Block for %s", this->_name);
      }

      ReverseUserBlockUpserter::ReverseUserBlockUpserter(std::string const& name)
        : _name(name)
      {
      }

      ReverseUserBlockUpserter::ReverseUserBlockUpserter(elle::serialization::Serializer& s,
                                           elle::Version const& version)
        : Super()
      {
        this->serialize(s, version);
      }

      void
      ReverseUserBlockUpserter::serialize(elle::serialization::Serializer& s,
                                   elle::Version const& version)
      {
        Super::serialize(s, version);
        s.serialize("name", this->_name);
      }

      std::string
      ReverseUserBlockUpserter::description() const
      {
        return elle::sprintf("write Reverse User Block for %s", this->_name);
      }

      static const elle::serialization::Hierarchy<infinit::model::ConflictResolver>::
      Register<infinit::model::doughnut::UserBlockUpserter> _register_user_block_uperserter("UserBlockUpserter");

      static const elle::serialization::Hierarchy<infinit::model::ConflictResolver>::
      Register<ReverseUserBlockUpserter> _register_reverse_user_block_uperserter("ReverseUserBlockUpserter");
    }
  }
}
