#include <fstream>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/serialization/binary.hh>
#include <elle/serialization/json.hh>
#include <elle/bench.hh>

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
          , _process_thread(
            new reactor::Thread(elle::sprintf("%s loop", *this),
                                [this] { this->_process_loop();}))
        {
          if (!this->_journal_dir.empty())
            boost::filesystem::create_directories(this->_journal_dir);
          if (max_size)
            this->_ops.max_size(max_size);
          this->_ops.close();
          this->_restore_journal(true);
        }

        std::unique_ptr<Local>
        Async::make_local(boost::optional<int> port,
                          std::unique_ptr<storage::Storage> storage)
        {
          return this->_backend->make_local(port, std::move(storage));
        }

        void
        Async::_restore_journal(bool first)
        {
          if (this->_journal_dir.empty())
            return;
          ELLE_TRACE_SCOPE("%s: %s journal from %s",
                           *this, first ? "restore" : "load more",_journal_dir);
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
          auto start_index = this->_first_disk_index;
          this->_first_disk_index.reset();
          for (auto const& p: files)
          {
            int id = std::stoi(p.filename().string());
            if (start_index && id < *start_index)
              continue;
            if (this->_ops.size() >= this->_ops.max_size())
            {
              if (!this->_first_disk_index)
              {
                ELLE_TRACE("in-memory asynchronous queue at capacity at index %s",
                           id);
                this->_first_disk_index = id;
              }
              if (first)
              {
                this->_next_index = std::max(id, this->_next_index);
                auto op = this->_load_op(id);
                ELLE_DEBUG("register %s", op);
                this->_last[op.address] = std::make_pair(id, nullptr);
                continue;
              }
              else
                return;
            }
            this->_next_index = std::max(id, this->_next_index);
            auto op = this->_load_op(id);
            if (op.block)
              op.block->seal();
            ELLE_DEBUG("restore %s", op);
            if (op.mode)
              this->_last[op.address] =
                std::make_pair(op.index, op.block.get());
            this->_ops.put(std::move(op));
          }
        }

        Async::Op
        Async::_load_op(int id)
        {
          auto p = this->_journal_dir / std::to_string(id);
          boost::filesystem::ifstream is(p, std::ios::binary);
          elle::serialization::binary::SerializerIn sin(is, false);
          sin.set_context<Model*>(&this->doughnut()); // FIXME: needed ?
          sin.set_context<Doughnut*>(&this->doughnut());
          sin.set_context(ACBDontWaitForSignature{});
          sin.set_context(OKBDontWaitForSignature{});
          auto op = sin.deserialize<Op>();
          op.index = id;
          return op;
        }

        void
        Async::_push_op(Op op)
        {
          op.index = ++this->_next_index;
          ELLE_TRACE_SCOPE("%s: push %s", *this, op);
          static elle::Bench bench("bench.async.pushop", 10000_sec);
          elle::Bench::BenchScope bs(bench);
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
                this->_last[op.address] =
                  std::make_pair(op.index, op.block.get());
              this->_ops.put(std::move(op));
              return;
            }
          }
          // This null indicates the block is cached on disk
          if (op.mode)
            this->_last[op.address] = std::make_pair(op.index, nullptr);
        }

        void
        Async::_store(std::unique_ptr<blocks::Block> block,
                      StoreMode mode,
                      std::unique_ptr<ConflictResolver> resolver)
        {
          this->_ops.open();
          this->_push_op(
            Op(block->address(), std::move(block), mode, std::move(resolver)));
        }

        void
        Async::_remove(Address address)
        {
          this->_ops.open();
          this->_push_op(Op(address, nullptr, {}));
        }

        // Fetch operation must be synchronous, else the consistency is not
        // preserved.
        std::unique_ptr<blocks::Block>
        Async::_fetch(Address address, boost::optional<int> local_version)
        {
          this->_ops.open();
          auto it = this->_last.find(address);
          if (it != this->_last.end())
          {
            if (it->second.second)
            {
              ELLE_TRACE("%s: fetch %s from memory queue", *this, address);
              if (local_version)
                if (auto m = dynamic_cast<blocks::MutableBlock*>(
                      it->second.second))
                  if (m->version() == *local_version)
                    return nullptr;
              return it->second.second->clone();
            }
            else
            {
              ELLE_TRACE("%s: fetch %s from disk journal", *this, address);
              return std::move(this->_load_op(it->second.first).block);
            }
          }
          return this->_backend->fetch(address);
        }

        void
        Async::_process_loop()
        {
          while (true)
          {
            try
            {
              if (this->_ops.size() <= this->_ops.max_size() / 2 &&
                  this->_first_disk_index)
                ELLE_TRACE(
                  "%s: restore additional operations from disk at index %s",
                  *this, *this->_first_disk_index)
                    this->_restore_journal();
              Op op = this->_ops.get();
              Address addr = op.address;
              ELLE_TRACE_SCOPE("%s: process %s", *this, op);
              boost::optional<StoreMode> mode = op.mode;
              std::unique_ptr<ConflictResolver>& resolver = op.resolver;
              elle::SafeFinally delete_entry(
                [&]
                {
                  if (!this->_journal_dir.empty())
                  {
                    auto path = boost::filesystem::path(this->_journal_dir) /
                      std::to_string(op.index);
                    boost::filesystem::remove(path);
                  }
                  auto it = this->_last.find(addr);
                  ELLE_ASSERT(it != this->_last.end());
                  if (mode && op.index == it->second.first)
                    this->_last.erase(it);
              });
              if (!mode)
                try
                {
                  this->_backend->remove(addr);
                }
                catch (MissingBlock const&)
                {
                  // Nothing: block was already removed.
                }
              else
                this->_backend->store(
                  std::move(op.block), *mode, std::move(resolver));
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
