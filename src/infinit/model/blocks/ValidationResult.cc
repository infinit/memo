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
        return ValidationResult(true, false, "success");
      }

      ValidationResult
      ValidationResult::failure(std::string const& reason)
      {
        return ValidationResult(false, false, reason);
      }

      ValidationResult
      ValidationResult::conflict(std::string const& reason)
      {
        return ValidationResult(false, true, reason);
      }

      ValidationResult::operator bool ()
      {
        return this->_success;
      }

      ValidationResult::ValidationResult(bool success, bool conflict, std::string const& reason)
        : _reason(std::move(reason))
        , _success(success)
        , _conflict(conflict)
      {}
    }
  }
}
