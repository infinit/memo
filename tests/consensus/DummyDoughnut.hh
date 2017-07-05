#pragma once

#include <memo/model/doughnut/Doughnut.hh>

class DummyDoughnut
  : public memo::model::doughnut::Doughnut
{
public:
  DummyDoughnut()
    : DummyDoughnut(memo::model::Address::random(0), // FIXME
                    elle::cryptography::rsa::keypair::generate(1024))
  {}

  DummyDoughnut(memo::model::Address id,
                elle::cryptography::rsa::KeyPair keys)
    : memo::model::doughnut::Doughnut(
      id, std::make_shared<elle::cryptography::rsa::KeyPair>(keys),
      keys.public_key(),
      memo::model::doughnut::Passport(keys.K(), "network", keys),
      [] (memo::model::doughnut::Doughnut&)
      { return nullptr; },
      [] (memo::model::doughnut::Doughnut&,
          std::shared_ptr<memo::model::doughnut::Local>)
      { return nullptr; })
  {}
};
