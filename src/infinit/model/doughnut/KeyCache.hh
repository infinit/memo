#pragma once

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace bmi = boost::multi_index;

      struct KeyHash
      {
        KeyHash(int h, elle::cryptography::rsa::PublicKey k)
          : hash(h)
          , key(std::make_shared(std::move(k)))
        {}

        KeyHash(int h, std::shared_ptr<elle::cryptography::rsa::PublicKey> k)
          : hash(h)
          , key(std::move(k))
        {}

        int hash;
        std::shared_ptr<elle::cryptography::rsa::PublicKey> key;
        elle::cryptography::rsa::PublicKey const& raw_key() const
        {
          return *key;
        }
      };

      using KeyCache = bmi::multi_index_container<
        KeyHash,
        bmi::indexed_by<
          bmi::hashed_unique<
            bmi::const_mem_fun<
              KeyHash,
              elle::cryptography::rsa::PublicKey const&, &KeyHash::raw_key>,
            std::hash<elle::cryptography::rsa::PublicKey>>,
          bmi::hashed_unique<
            bmi::member<KeyHash, int, &KeyHash::hash>>>>;
    }
  }
}
