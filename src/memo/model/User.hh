#ifndef INFINIT_MODEL_USER_HH
# define INFINIT_MODEL_USER_HH

namespace memo
{
  namespace model
  {
    class User
    {
    public:
      virtual ~User()
      {}
      virtual std::string name() {return "";};
    };
  }
}

#endif
