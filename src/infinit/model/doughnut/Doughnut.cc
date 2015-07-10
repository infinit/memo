#include <infinit/model/doughnut/Doughnut.hh>

#include <elle/Buffer.hh>
#include <elle/Error.hh>
#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/serialization/json.hh> // FIXME

#include <reactor/Scope.hh>
#include <reactor/exception.hh>

#include <infinit/storage/MissingKey.hh>

#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/Remote.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Doughnut");

#include <infinit/model/doughnut/CHB.cc>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class PlainMutableBlock: public blocks::MutableBlock
      {
      public:
        typedef blocks::MutableBlock Super;
        PlainMutableBlock()
        : Super(Address::random()) {}
        PlainMutableBlock(elle::serialization::Serializer& input)
        : Super(input) {}
      };
      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<PlainMutableBlock> _register_pmb_serialization("PMB");
      class PlainImmutableBlock: public blocks::ImmutableBlock
      {
      public:
        PlainImmutableBlock()
        : Super(Address::random()) {}
        PlainImmutableBlock(elle::Buffer b)
        : Super(Address::random(), b)
        {}
        PlainImmutableBlock(elle::serialization::Serializer& input)
        : Super(input)
        {}
        typedef blocks::ImmutableBlock Super;
      };
      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<PlainImmutableBlock> _register_pib_serialization("PIB");
      Doughnut::Doughnut(cryptography::KeyPair keys,
                         std::unique_ptr<overlay::Overlay> overlay,
                         bool plain, int write_n, int read_n)
        : _overlay(std::move(overlay))
        , _keys(std::move(keys))
        , _plain(plain)
        , _write_n(write_n)
        , _read_n(read_n)
      {}

      std::unique_ptr<blocks::MutableBlock>
      Doughnut::_make_mutable_block() const
      {
        ELLE_TRACE_SCOPE("%s: create OKB", *this);
        if (_plain)
           return elle::make_unique<PlainMutableBlock>();
        auto res = elle::make_unique<OKB>(const_cast<Doughnut*>(this));
        return std::move(res);
      }

      std::unique_ptr<blocks::ImmutableBlock>
      Doughnut::_make_immutable_block(elle::Buffer content) const
      {
        ELLE_TRACE_SCOPE("%s: create CHB", *this);
        if (_plain)
          return elle::make_unique<PlainImmutableBlock>(std::move(content));
        else
          return elle::make_unique<CHB>(std::move(content));
      }

      std::unique_ptr<blocks::ACLBlock>
      Doughnut::_make_acl_block() const
      {
        ELLE_TRACE_SCOPE("%s: create ACB", *this);
        ELLE_ASSERT(!this->_plain); // LALALALA
        return elle::make_unique<ACB>(const_cast<Doughnut*>(this));
      }

      void
      Doughnut::_store(blocks::Block& block, StoreMode mode)
      {
        overlay::Operation op;
        switch (mode)
        {
        case STORE_ANY:
          op = overlay::OP_INSERT_OR_UPDATE;
          break;
        case STORE_INSERT:
          op = overlay::OP_INSERT;
          break;
        case STORE_UPDATE:
          op = overlay::OP_UPDATE;
          break;
        }
        if (_write_n == 1)
          this->_owner(block.address(), op)->store(block, mode);
        else
        {
          auto endpoints = this->_overlay->lookup(block.address(), _write_n, op);
          elle::With<reactor::Scope>() << [&](reactor::Scope& s)
          {
            for (auto & e: endpoints)
              s.run_background("store", [e, &block, mode] {
                  elle::make_unique<Remote>(e)->store(block, mode);
              });
            s.wait();
          };
        }
      }

      std::unique_ptr<blocks::Block>
      Doughnut::_fetch(Address address) const
      {
        std::unique_ptr<blocks::Block> res;
        if (_read_n == 1)
        {
          try
          {
            res = this->_owner(address, overlay::OP_FETCH)->fetch(address);
          }
          catch (infinit::storage::MissingKey const&)
          {
            return nullptr;
          }
          catch (reactor::Terminate const&)
          {
            throw;
          }
          catch (std::exception const& e)
          {
            ELLE_WARN("Doughnut: unexpected exception while fetching %x: %s (%s)",
                      address, e.what(), typeid(e).name());
            ELLE_WARN("Workaround exception slicing bug: assuming MissingKey");
            return nullptr;
          }
        }
        else
        {
          auto endpoints = this->_overlay->lookup(address, _write_n, overlay::OP_FETCH);
          std::vector<std::unique_ptr<blocks::Block>> blocks;
          elle::With<reactor::Scope>() << [&](reactor::Scope& s)
          {
            for (auto & e: endpoints)
              s.run_background("fetch", [e,&blocks,address] {
                  blocks.push_back(elle::make_unique<Remote>(e)->fetch(address));
              });
            s.wait();
          };
          res = std::move(blocks.front());
        }
        if (auto okb = elle::cast<OKB>::runtime(res))
        {
          okb->_doughnut = const_cast<Doughnut*>(this);
          return std::move(okb);
        }
        else if (auto acb = elle::cast<ACB>::runtime(res))
        {
          acb->_doughnut = const_cast<Doughnut*>(this);
          return std::move(acb);
        }
        else
          return res;
      }

      void
      Doughnut::_remove(Address address)
      {
        if (_write_n == 1)
          this->_owner(address, overlay::OP_REMOVE)->remove(address);
        else
        {
          auto endpoints = this->_overlay->lookup(address, _write_n,
                                                  overlay::OP_REMOVE);
          elle::With<reactor::Scope>() << [&](reactor::Scope& s)
          {
            for (auto & e: endpoints)
              s.run_background("remove", [e, address] {
                  elle::make_unique<Remote>(e)->remove(address);
              });
            s.wait();
          };
        }
      }

      std::unique_ptr<Peer>
      Doughnut::_owner(Address const& address, overlay::Operation op) const
      {
        return elle::make_unique<Remote>(this->_overlay->lookup(address, op));
      }
    }
  }
}
