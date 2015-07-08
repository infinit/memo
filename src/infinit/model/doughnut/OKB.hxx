#ifndef INFINIT_MODEL_DOUGHNUT_OKB_HXX
# define INFINIT_MODEL_DOUGHNUT_OKB_HXX

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      template <typename Block>
      template <typename T>
      bool
      BaseOKB<Block>::_validate_version(blocks::Block const& other_,
                                        int T::*member,
                                        int version) const
      {
        ELLE_LOG_COMPONENT("infinit.model.doughnut.OKB");
        auto other = dynamic_cast<T const*>(&other_);
        if (!other)
        {
          ELLE_TRACE("%s: writing over a different block type", *this);
          return false;
        }
        auto other_version = other->*member;
        if (version < other_version)
        {
          ELLE_TRACE("%s: version (%s) is older than stored version (%s)",
                     *this, version, other_version);
          return false;
        }
        else
        {
          ELLE_DUMP(
            "%s: version (%s) is newer or equal than stored version (%s)",
            *this, version, other_version);
          return true;
        }
      }
    }
  }
}

#endif
