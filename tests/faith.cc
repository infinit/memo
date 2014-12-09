#include <elle/log.hh>
#include <elle/test.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/Block.hh>
#include <infinit/model/faith/Faith.hh>
#include <infinit/storage/Memory.hh>

ELLE_LOG_COMPONENT("infinit.model.faith.test");

static
void
faith()
{
  infinit::storage::Memory storage;
  infinit::model::faith::Faith faith(storage);

  auto block1 = faith.make_block();
  auto block2 = faith.make_block();
  BOOST_CHECK_NE(block1->address(), block2->address());
  ELLE_LOG("store blocks")
  {
    faith.store(*block1);
    BOOST_CHECK_EQUAL(*faith.fetch(block1->address()), *block1);
    faith.store(*block2);
    BOOST_CHECK_EQUAL(*faith.fetch(block2->address()), *block2);
  }
  ELLE_LOG("update block")
  {
    std::string update("twerk is the new twist");
    block2->data() = elle::Buffer(update.c_str(), update.length());
    BOOST_CHECK_NE(*faith.fetch(block2->address()), *block2);
    std::cerr << "STORE " << block2->data() << std::endl;
    faith.store(*block2);
    std::cerr << "STORED " << block2->data() << std::endl;
    BOOST_CHECK_EQUAL(*faith.fetch(block2->address()), *block2);
  }
  ELLE_LOG("fetch non-existent block")
  {
    auto block3 = faith.make_block();
    BOOST_CHECK_THROW(faith.fetch(block3->address()),
                      infinit::model::MissingBlock);
  }
}


ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(faith));
}
