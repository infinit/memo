#ifndef INFINIT_MODEL_BLOCKS_ACL_VALIDATIONRESULT_HH
# define INFINIT_MODEL_BLOCKS_ACL_VALIDATIONRESULT_HH

# include <string>

# include <elle/attribute.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      class ValidationResult
      {
      public:
        static
        ValidationResult
        success();
        static
        ValidationResult
        failure(std::string const& reason);
        static
        ValidationResult
        conflict(std::string const& reason);
        operator bool ();
        ELLE_ATTRIBUTE_R(std::string, reason);

      private:
        ValidationResult(bool success, bool conflict, std::string const& reason);
        ELLE_ATTRIBUTE(bool, success);
        ELLE_ATTRIBUTE_R(bool, conflict);
      };
    }
  }
}

#endif
