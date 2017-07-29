#pragma once

namespace memo
{
  namespace model
  {
    class User
    {
    public:
      virtual ~User() {}
      virtual std::string name() const { return {}; };
    };
  }
}
