#include <infinit/model/blocks/ValidationResult.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      ValidationResult
      ValidationResult::success()
      {
        return ValidationResult(true, "success");
      }

      ValidationResult
      ValidationResult::failure(std::string const& reason)
      {
        return ValidationResult(false, reason);
      }

      ValidationResult::operator bool ()
      {
        return this->_success;
      }

      ValidationResult::ValidationResult(bool success, std::string const& reason)
        : _reason(std::move(reason))
        , _success(success)
      {}
    }
  }
}
