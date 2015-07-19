#ifndef INFINIT_MODEL_USER_HH
# define INFINIT_MODEL_USER_HH

namespace infinit
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
