#ifndef DUMMYDOUGHNUT_HH
# define DUMMYDOUGHNUT_HH

#include <infinit/model/doughnut/Doughnut.hh>

class DummyDoughnut
  : public infinit::model::doughnut::Doughnut
{
public:
  DummyDoughnut()
    : DummyDoughnut(infinit::model::Address::random(0), // FIXME
                    infinit::cryptography::rsa::keypair::generate(1024))
  {}

  DummyDoughnut(infinit::model::Address id,
                infinit::cryptography::rsa::KeyPair keys)
    : infinit::model::doughnut::Doughnut(
      id, std::make_shared<infinit::cryptography::rsa::KeyPair>(keys),
      keys.public_key(),
      infinit::model::doughnut::Passport(keys.K(), "network", keys),
      [] (infinit::model::doughnut::Doughnut&)
      { return nullptr; },
      [] (infinit::model::doughnut::Doughnut&,
          std::shared_ptr<infinit::model::doughnut::Local>)
      { return nullptr; },
      {}, nullptr)
  {}
};

#endif
