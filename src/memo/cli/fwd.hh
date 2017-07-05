#pragma once

#if defined MEMO_ENTREPRISE_EDITION
# define MEMO_ENTREPRISE(...) __VA_ARGS__
#else
# define MEMO_ENTREPRISE(...)
#endif

namespace memo
{
  namespace cli
  {
    class Block;
    class Credentials;
#if MEMO_WITH_DAEMON
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
