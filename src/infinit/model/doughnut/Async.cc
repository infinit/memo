#include <fstream>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/serialization/binary.hh>
#include <elle/serialization/json.hh>

#include <das/model.hh>
#include <das/serializer.hh>

#include <reactor/exception.hh>
#include <reactor/scheduler.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>


ELLE_LOG_COMPONENT("infinit.model.doughnut.consensus.Async");

DAS_MODEL(infinit::model::doughnut::consensus::Async::Op,
          (address, block, mode, resolver),
          DasOp);
DAS_MODEL_DEFAULT(infinit::model::doughnut::consensus::Async::Op, DasOp)
DAS_MODEL_SERIALIZE(infinit::model::doughnut::consensus::Async::Op);

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        std::ostream&
        operator <<(std::ostream& o, Async::Op const& op)
        {
          if (op.mode)
            elle::fprintf(o, "Op::store(%s, %s)", op.index, *op.block);
          else
            elle::fprintf(o, "Op::remove(%s)", op.index);
          return o;
        }

        Async::Async(std::unique_ptr<Consensus> backend,
                     boost::filesystem::path journal_dir,
                     int max_size)
          : Consensus(backend->doughnut())
          , _backend(std::move(backend))
          , _next_index(1)
          , _journal_dir(journal_dir)
          , _started()
          , _process_thread(
            new reactor::Thread("async consensus",
                                [this] { this->_process_loop();}))
        {
          if (!this->_journal_dir.empty())
            boost::filesystem::create_directories(this->_journal_dir);
          if (max_size)
            this->_ops.max_size(max_size);
        }

        std::unique_ptr<Local>
        Async::make_local(boost::optional<int> port,
                          std::unique_ptr<storage::Storage> storage)
        {
          return this->_backend->make_local(port, std::move(storage));
        }

        void
        Async::_restore_journal(overlay::Overlay& overlay)
        {
          if (this->_journal_dir.empty())
            return;
          ELLE_TRACE_SCOPE("%s: restore journal from %s", *this, _journal_dir);
          boost::filesystem::path p(_journal_dir);
          boost::filesystem::directory_iterator it(p);
          std::vector<boost::filesystem::path> files;
          while (it != boost::filesystem::directory_iterator())
          {
            files.push_back(it->path());
            ++it;
          }
          std::sort(files.begin(), files.end(),
            [] (boost::filesystem::path const&a,
                boost::filesystem::path const& b) -> bool
            {
              return std::stoi(a.filename().string()) <
                std::stoi(b.filename().string());
            });
          for (auto const& p: files)
          {
            int id = std::stoi(p.filename().string());
            if (this->_first_disk_index && id < *this->_first_disk_index)
              continue;
            if (this->_ops.size() >= this->_ops.max_size())
            {
              ELLE_TRACE("in-memory asynchronous queue at capacity at index %s",
                         id);
              this->_first_disk_index = id;
              return;
            }
            this->_next_index = std::max(id, this->_next_index);
            boost::filesystem::ifstream is(p, std::ios::binary);
            elle::serialization::binary::SerializerIn sin(is, false);
            sin.set_context<Model*>(&this->doughnut()); // FIXME: needed ?
            sin.set_context<Doughnut*>(&this->doughnut());
            sin.set_context(ACBDontWaitForSignature{});
            sin.set_context(OKBDontWaitForSignature{});
            auto op = sin.deserialize<Op>();
            if (op.block)
              op.block->seal();
            op.index = id;
            ELLE_DEBUG("restored %s", op);
            if (op.mode)
              this->_last[op.address] = op.block.get();
            this->_ops.put(std::move(op));
          }
          this->_first_disk_index.reset();
        }

        void
        Async::_push_op(Op op)
        {
          ELLE_TRACE_SCOPE("%s: push %s", *this, op);
          op.index = ++_next_index;
          if (!this->_journal_dir.empty())
          {
            auto path =
              boost::filesystem::path(_journal_dir) / std::to_string(op.index);
            boost::filesystem::ofstream os(path, std::ios::binary);
            elle::serialization::binary::SerializerOut sout(os, false);
            sout.set_context(ACBDontWaitForSignature{});
            sout.set_context(OKBDontWaitForSignature{});
            sout.serialize_forward(op);
          }
          if (!this->_first_disk_index)
          {
            if (!this->_journal_dir.empty() &&
                this->_ops.size() >= this->_ops.max_size())
            {
              ELLE_TRACE("in-memory asynchronous queue at capacity at index %s",
                         op.index);
              this->_first_disk_index = op.index;
            }
            else
            {
              if (op.mode)
                this->_last[op.address] = op.block.get();
              this->_ops.put(std::move(op));
            }
          }
        }

        void
        Async::_store(overlay::Overlay& overlay,
                      std::unique_ptr<blocks::Block> block,
                      StoreMode mode,
                      std::unique_ptr<ConflictResolver> resolver)
        {
          this->_started.open();
          this->_push_op(
            Op(block->address(), std::move(block), mode, std::move(resolver)));
        }

        void
        Async::_remove(overlay::Overlay& overlay,
                Address address)
        {
          this->_started.open();
          this->_push_op(Op(address, nullptr, {}));
        }

        // Fetch operation must be synchronous, else the consistency is not
        // preserved.
        std::unique_ptr<blocks::Block>
        Async::_fetch(overlay::Overlay& overlay,
                      Address address,
                      boost::optional<int>)
        {
          this->_started.open();
          if (this->_last.find(address) != _last.end())
          {
            ELLE_TRACE("%s: fetch %s from journal", *this, address);
            return this->_last[address]->clone();
          }
          return this->_backend->fetch(overlay, address);
        }

        void
        Async::_process_loop()
        {
          reactor::wait(this->_started);
          overlay::Overlay& overlay = *this->doughnut().overlay();
          this->_restore_journal(overlay);
          while (true)
          {
            try
            {
              if (this->_ops.size() <= this->_ops.max_size() / 2 &&
                  this->_first_disk_index)
                ELLE_TRACE(
                  "%s: restore additional operations from disk at index %s",
                  *this, *this->_first_disk_index)
                    this->_restore_journal(overlay);
              Op op = this->_ops.get();
              Address addr = op.address;
              ELLE_TRACE_SCOPE("%s: process %s", *this, op);
              boost::optional<StoreMode> mode = op.mode;
              std::unique_ptr<ConflictResolver>& resolver = op.resolver;
              auto ptr = op.block.get();
              elle::SafeFinally delete_entry(
                [&]
                {
                  if (!this->_journal_dir.empty())
                  {
                    auto path = boost::filesystem::path(this->_journal_dir) /
                      std::to_string(op.index);
                    boost::filesystem::remove(path);
                  }
                  if (mode && ptr == this->_last[addr])
                    this->_last.erase(addr);
              });
              if (!mode)
                try
                {
                  this->_backend->remove(overlay, addr);
                }
                catch (MissingBlock const&)
                {
                  // Nothing: block was already removed.
                }
              else
                this->_backend->store(overlay,
                                      std::move(op.block),
                                      *mode,
                                      std::move(resolver));
            }
            catch (elle::Error const& e)
            {
              ELLE_ABORT("%s: async loop killed: %s", *this, e.what());
            }
          }
        }

        /*----------.
        | Operation |
        `----------*/

        Async::Op::Op(Address address_,
                      std::unique_ptr<blocks::Block>&& block_,
                      boost::optional<StoreMode> mode_,
                      std::unique_ptr<ConflictResolver> resolver_)
          : address(address_)
          , block(std::move(block_))
          , mode(std::move(mode_))
          , resolver(std::move(resolver_))
        {}
      }
    }
  }
}
