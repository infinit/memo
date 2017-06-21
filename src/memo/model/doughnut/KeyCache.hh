#pragma once

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      namespace bmi = boost::multi_index;

      struct KeyHash
      {
        KeyHash(int h, std::shared_ptr<elle::cryptography::rsa::PublicKey> k)
          : hash(h)
          , key(std::move(k))
        {}

        KeyHash(int h, elle::cryptography::rsa::PublicKey k)
          : KeyHash{h, std::make_shared(std::move(k))}
        {}

        elle::cryptography::rsa::PublicKey const& raw_key() const
        {
          return *key;
        }

        int hash;
        std::shared_ptr<elle::cryptography::rsa::PublicKey> key;
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
