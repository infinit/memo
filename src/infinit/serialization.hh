#ifndef INFINIT_SERIALIZATION_HH
# define INFINIT_SERIALIZATION_HH

# include <elle/Version.hh>

namespace infinit
{
  struct serialization_tag
  {
    static elle::Version version;
  };
}

#endif
