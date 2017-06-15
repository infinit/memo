#pragma once

#if defined INFINIT_ENTREPRISE_EDITION
# define INFINIT_ENTREPRISE(...) __VA_ARGS__
#else
# define INFINIT_ENTREPRISE(...)
#endif

namespace infinit
{
  namespace cli
  {
    class Block;
    class Credentials;
#if INFINIT_WITH_DAEMON
    class Daemon;
#endif
    class Device;
    class Doctor;
    class Memo;
    class Journal;
#if MEMO_WITH_KEY_VALUE_STORE
    class KeyValueStore;
#endif
    class Network;
    class Passport;
    class Silo;
    class User;
  }
}
