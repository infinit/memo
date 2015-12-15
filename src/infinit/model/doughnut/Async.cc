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
//DAS_MODEL_SERIALIZE(infinit::model::doughnut::consensus::Async::Op);

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        struct OpAddressOnly{};
        std::ostream&
        operator <<(std::ostream& o, Async::Op const& op)
        {
          if (op.mode)
            elle::fprintf(o, "Op::store(%s, %s)", op.index, *op.block);
          else
            elle::fprintf(o, "Op::remove(%s)", op.index);
          return o;
        }

        Async::Op::Op(elle::serialization::SerializerIn& ser)
        {
          this->serialize(ser);
        }

        void
        Async::Op::serialize(elle::serialization::Serializer& s)
        {
          s.serialize("address", address);
          if (s.context().has<OpAddressOnly>())
            return;
          s.serialize("block", block);
          s.serialize("mode", mode);
          s.serialize("resolver", resolver);
        }

        Async::Async(std::unique_ptr<Consensus> backend,
                     boost::filesystem::path journal_dir,
                     int max_size)
          : Consensus(backend->doughnut())
          , _backend(std::move(backend))
          , _operations()
          , _queue()
          , _next_index(1)
          , _last_processed_index(0)
          , _journal_dir(journal_dir)
          , _exit_requested(false)
          , _process_thread(
            new reactor::Thread(elle::sprintf("%s loop", *this),
                                [this] {
                                  bool freeze = getenv("INFINIT_ASYNC_NOPOP");
                                  if (freeze)
                                    return;
                                  this->_process_loop();
                                }))
        {
          if (!this->_journal_dir.empty())
            boost::filesystem::create_directories(this->_journal_dir);
          if (max_size)
            this->_queue.max_size(max_size);
          this->_queue.close();
          if (!this->_journal_dir.empty())
          {
            this->_init_barrier.close();
            _init_thread.reset(new reactor::Thread(elle::sprintf("%s init", *this),
              [this] { this->_init();}));
          }
          else
            this->_init_barrier.open();
        }
        void
        Async::_init()
        {
          reactor::sleep(100_ms);
          elle::SafeFinally open_barrier([this] {
              this->_init_barrier.open();
          });
         ELLE_TRACE_SCOPE("%s: restore journal from %s",
                          *this, this->_journal_dir);
         boost::filesystem::path p(_journal_dir);
         std::vector<boost::filesystem::path> files;
         for (auto it = boost::filesystem::directory_iterator(this->_journal_dir);
              it != boost::filesystem::directory_iterator();
              ++it)
           files.push_back(it->path());
         std::sort(
           files.begin(),
           files.end(),
           [] (boost::filesystem::path const& a,
               boost::filesystem::path const& b) -> bool
           {
             return std::stoi(a.filename().string()) <
               std::stoi(b.filename().string());
           });
         for (auto const& p: files)
         {
           auto id = std::stoi(p.filename().string());
           auto op = this->_load_op(id,
             this->_queue.size() < this->_queue.max_size());
           this->_next_index = std::max(id, this->_next_index);
           if (this->_queue.size() < this->_queue.max_size())
             this->_queue.put(op.index);
           else
           {
             if (!this->_first_disk_index)
             {
               ELLE_TRACE(
                 "in-memory asynchronous queue at capacity at index %s",
                 op.index);
               this->_first_disk_index = op.index;
             }
             op.block.reset();
           }
           this->_operations.emplace(std::move(op));
         }
         ELLE_TRACE("...done restoring journal");
        }

        Async::~Async()
        {
          ELLE_TRACE_SCOPE("%s: destroy", *this);
          this->_exit_requested = true;
          // Wake up the thread if needed.
          if (this->_queue.size() == 0)
            this->_queue.put(0);
          if (!reactor::wait(*this->_process_thread, 10_sec)
            || !reactor::wait(*this->_init_thread, 10_sec))
            ELLE_WARN("forcefully kiling async process loop");
        }

        std::unique_ptr<Local>
        Async::make_local(boost::optional<int> port,
                          std::unique_ptr<storage::Storage> storage)
        {
          return this->_backend->make_local(port, std::move(storage));
        }

        void
        Async::sync()
        {
          int wait_id = _next_index-1;
          while (_last_processed_index < wait_id)
            reactor::sleep(100_ms);
        }

        void
        Async::_load_operations()
        {
          if (this->_journal_dir.empty())
            return;
          for (auto it = this->_operations.get<1>().find(
                 this->_first_disk_index.get());
               it != this->_operations.get<1>().end();
               ++it)
          {
            if (this->_queue.size() >= this->_queue.max_size())
            {
              ELLE_TRACE("in-memory asynchronous queue at capacity at index %s",
                         it->index);
              this->_first_disk_index = it->index;
              return;
            }
            auto op = this->_load_op(it->index);
            ELLE_DEBUG("restore %s", op);
            this->_queue.put(op.index);
            this->_operations.get<1>().modify(
              it, [&] (Op& o) {
              o.block = std::move(op.block);
              o.mode = std::move(op.mode);
              o.resolver = std::move(op.resolver);
              });
          }
          this->_first_disk_index.reset();
        }

        Async::Op
        Async::_load_op(boost::filesystem::path const& p, bool signature)
        {
          boost::filesystem::ifstream is(p, std::ios::binary);
          elle::serialization::binary::SerializerIn sin(is, false);
          sin.set_context<Model*>(&this->doughnut()); // FIXME: needed ?
          sin.set_context<Doughnut*>(&this->doughnut());
          sin.set_context(ACBDontWaitForSignature{});
          sin.set_context(OKBDontWaitForSignature{});
          if (!signature)
          {
            sin.set_context(OpAddressOnly{});
          }
          auto op = sin.deserialize<Op>();
          if (op.block)
            op.block->seal();
          return op;
        }

        Async::Op
        Async::_load_op(int id, bool signature)
        {
          auto op = this->_load_op(this->_journal_dir / std::to_string(id), signature);
          op.index = id;
          return op;
        }

        void
        Async::_push_op(Op op)
        {
          op.index = ++this->_next_index;
          ELLE_TRACE_SCOPE("%s: push %s", *this, op);
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
                this->_queue.size() >= this->_queue.max_size())
            {
              ELLE_TRACE("in-memory asynchronous queue at capacity at index %s",
                         op.index);
              this->_first_disk_index = op.index;
              op.block.reset();
            }
            else
              this->_queue.put(op.index);
          }
          else
            op.block.reset();
          this->_operations.emplace(std::move(op));
        }

        void
        Async::_store(std::unique_ptr<blocks::Block> block,
                      StoreMode mode,
                      std::unique_ptr<ConflictResolver> resolver)
        {
          reactor::wait(this->_init_barrier);
          this->_queue.open();
          this->_push_op(
            Op(block->address(), std::move(block), mode, std::move(resolver)));
        }

        void
        Async::_remove(Address address)
        {
          reactor::wait(this->_init_barrier);
          this->_queue.open();
          this->_push_op(Op(address, nullptr, {}));
        }

        // Fetch operation must be synchronous, else the consistency is not
        // preserved.
        std::unique_ptr<blocks::Block>
        Async::_fetch(Address address, boost::optional<int> local_version)
        {
          if (this->_init_thread && !this->_init_thread->done()
            && reactor::scheduler().current() != this->_init_thread.get())
            reactor::wait(this->_init_barrier);

          this->_queue.open();
          auto it = this->_operations.find(address);
          if (it != this->_operations.end())
          {
            if (it->block)
            {
              ELLE_TRACE("%s: fetch %s from memory queue", *this, address);
              if (local_version)
                if (auto m = dynamic_cast<blocks::MutableBlock*>(
                      it->block.get()))
                  if (m->version() == *local_version)
                    return nullptr;
              return it->block->clone();
            }
            else
            {
              ELLE_TRACE("%s: fetch %s from disk journal", *this, address);
              auto res = this->_load_op(it->index).block;
              if (!res)
                throw MissingBlock(address);
              return res;
            }
          }
          return this->_backend->fetch(address);
        }

        void
        Async::_process_loop()
        {
          while (!_exit_requested)
          {
            try
            {
              if (this->_queue.size() <= this->_queue.max_size() / 2 &&
                  this->_first_disk_index)
                ELLE_TRACE(
                  "%s: restore additional operations from disk at index %s",
                  *this, *this->_first_disk_index)

                this->_load_operations();
              int index = this->_queue.get();
              if (_exit_requested)
                break;
              auto it = this->_operations.get<1>().begin();
              Op const* op = &*it;
              bool must_delete = false;
              ELLE_ASSERT_EQ(op->index, index);
              Address addr = op->address;
              ELLE_TRACE_SCOPE("%s: process %s", *this, op);
              boost::optional<StoreMode> mode = op->mode;
              int attempt = 0;
              while (true)
              {
                try
                {
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
                  {
                    this->_backend->store(
                      std::move(elle::unconst(op)->block),
                      *mode,
                      std::move(elle::unconst(op)->resolver));
                  }
                  break;
                }
                catch (elle::Error const& e)
                {
                  ELLE_LOG("error in async loop: %s", e);
                  ++attempt;
                  reactor::sleep(std::min(20000_ms,
                    boost::posix_time::milliseconds(200 * attempt)));
                  // reload block and try again
                  if (must_delete)
                    delete op;
                  op = new Op(_load_op(op->index));
                  must_delete = true;
                }
              }
              if (!this->_journal_dir.empty())
              {
                auto path = boost::filesystem::path(this->_journal_dir) /
                  std::to_string(op->index);
                boost::filesystem::remove(path);
                _last_processed_index = op->index;
              }
              this->_operations.get<1>().erase(it);
              if (must_delete)
                delete op;
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
