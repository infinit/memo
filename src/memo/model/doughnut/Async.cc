#include <fstream>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/range/algorithm/sort.hpp>

#include <elle/os/environ.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/json.hh>
#include <elle/bench.hh>
#include <elle/ScopedAssignment.hh>

#include <elle/das/model.hh>
#include <elle/das/serializer.hh>

#include <elle/reactor/exception.hh>
#include <elle/reactor/scheduler.hh>

#include <memo/silo/Collision.hh>

#include <memo/model/Conflict.hh>
#include <memo/model/MissingBlock.hh>
#include <memo/model/doughnut/ACB.hh>
#include <memo/model/doughnut/Async.hh>
#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/Local.hh>


ELLE_LOG_COMPONENT("memo.model.doughnut.consensus.Async");

namespace memo
{
  namespace bfs = boost::filesystem;

  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        struct OpAddressOnly{};

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
          s.serialize("remove_signature", remove_signature);
          if (s.in())
          {
            if (auto mb = dynamic_cast<blocks::MutableBlock*>(block.get()))
              version = mb->version();
            else if (auto mb = dynamic_cast<blocks::MutableBlock*>(
              remove_signature.block.get()))
              version = mb->version();
          }
        }

        Async::Async(std::unique_ptr<Consensus> backend,
                     bfs::path journal_dir,
                     int max_size)
          : StackedConsensus(std::move(backend))
          , _operations()
          , _queue()
          , _next_index(1)
          , _last_processed_index(0)
          , _journal_dir(journal_dir)
          , _exit_requested(false)
          , _process_thread(
            new elle::reactor::Thread(elle::sprintf("%s loop", *this),
                                [this] {
                                  bool freeze = getenv("INFINIT_ASYNC_NOPOP");
                                  if (freeze)
                                    return;
                                  this->_process_loop();
                                }))
          , _in_push(false)
          , _processed_op_count(0)
        {
          if (!this->_journal_dir.empty())
          {
            bfs::create_directories(this->_journal_dir);
            bfs::permissions(this->_journal_dir,
              bfs::remove_perms
              | bfs::others_all | bfs::group_all);
          }
          if (max_size)
            this->_queue.max_size(max_size);
          if (!this->_journal_dir.empty())
          {
            this->_init_barrier.close();
            this->_init_thread.reset(
              new elle::reactor::Thread(elle::sprintf("%s: restore journal", this),
                                  [this] { this->_init();}));
          }
          else
            this->_init_barrier.open();
        }

        std::vector<bfs::path>
        Async::entries(bfs::path const& root)
        {
          auto paths = std::vector<bfs::path>
            {bfs::directory_iterator(root), bfs::directory_iterator()};
          boost::sort(paths,
                    [] (bfs::path const& a, bfs::path const& b)
                    {
                      return std::stoi(a.filename().string()) <
                        std::stoi(b.filename().string());
                    });
          return paths;
        }

        void
        Async::_init()
        {
          elle::SafeFinally open_barrier([this] {
              this->_init_barrier.open();
            });
          ELLE_TRACE_SCOPE("%s: restore journal from %s",
                           *this, this->_journal_dir);
          bfs::path p(_journal_dir);
          auto files = Async::entries(p);
          for (auto const& p: files)
          {
            auto id = std::stoi(p.filename().string());
            Op op;
            try
            {
              op = this->_load_op(id,
                this->_queue.size() < this->_queue.max_size());
            }
            catch (elle::Error const& e)
            {
              ELLE_WARN("Failed to reload %s: %s", id, e);
              continue;
            }
            this->_next_index = std::max(id, this->_next_index);
            if (this->_queue.size() < this->_queue.max_size())
            {
              ELLE_WARN("PUT");
              this->_queue.put(op.index);
            }
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
          ELLE_TRACE("restored %s operations", this->_queue.size());
        }

        Async::~Async()
        {
          ELLE_TRACE_SCOPE("%s: destroy", *this);
          this->_exit_requested = true;
          // Wake up the thread if needed.
          if (this->_queue.size() == 0)
            this->_queue.put(0);
          if (!elle::reactor::wait(*this->_process_thread, 10_sec)
            || !elle::reactor::wait(*this->_init_thread, 10_sec))
            ELLE_WARN("forcefully kiling async process loop");
        }

        std::unique_ptr<Local>
        Async::make_local(
          boost::optional<int> port,
          boost::optional<boost::asio::ip::address> listen_address,
          std::unique_ptr<silo::Silo> storage)
        {
          return this->_backend->make_local(
            port, std::move(listen_address), std::move(storage));
        }

        void
        Async::sync()
        {
          int wait_id = _next_index-1;
          while (_last_processed_index < wait_id)
            elle::reactor::sleep(100_ms);
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
            ELLE_DEBUG("reload %s", it->index);
            Op op;
            try
            {
              op = this->_load_op(it->index);
            }
            catch (elle::Error const& e)
            {
              ELLE_WARN("Failed to reload %s: %s", it->index, e);
              continue;
            }
            ELLE_DEBUG("restore %s", op);
            this->_queue.put(op.index);
            this->_operations.get<1>().modify(
              it, [&] (Op& o) {
              o.block = std::move(op.block);
              o.mode = std::move(op.mode);
              o.resolver = std::move(op.resolver);
              o.remove_signature = std::move(op.remove_signature);
              });
          }
          this->_first_disk_index.reset();
        }

        Async::Op
        Async::_load_op(bfs::path const& p, bool signature)
        {
          bfs::ifstream is(p, std::ios::binary);
          elle::serialization::binary::SerializerIn sin(is);
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
          if (op.remove_signature.block)
            op.remove_signature.block->seal();
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
          op.index = -1; // for nice debug prints, we will set that later
          // squash check
          static bool squash_enabled = elle::os::getenv("INFINIT_ASYNC_DISABLE_SQUASH", "").empty();
          auto its = this->_operations.get<0>().equal_range(op.address);
          if (squash_enabled && its.second != its.first && op.resolver)
          {
            int last_candidate_index = -1;
            SquashOperation last_candidate_order = std::make_pair(
              Squash::stop, SquashConflictResolverOptions(0));
            // Check for squashability: we need resolvers, and we can't touch
            // the head of the queue since it's currently being processed
            int current = this->_operations.get<1>().begin()->index;
            std::vector<int> candidates;
            for (auto it = its.first; it != its.second; ++it)
              if (it->index != current)
                candidates.push_back(it->index);
            std::sort(candidates.begin(), candidates.end(),
              [](int x, int y) { return x > y;});
            for(int c: candidates)
            {
              auto& cop = *this->_operations.get<1>().find(c);
              if (!cop.resolver)
                continue;
              auto squash = op.resolver->squashable(*cop.resolver);
              bool keep_searching = true;
              switch (squash.first)
              {
              case Squash::stop:
                keep_searching = false;
                break;
              case Squash::skip:
                break;
              case Squash::at_first_position_stop:
              case Squash::at_last_position_stop:
                keep_searching = false;
                last_candidate_order = squash;
                last_candidate_index = c;
                break;
              case Squash::at_last_position_continue:
              case Squash::at_first_position_continue:
                last_candidate_order = squash;
                last_candidate_index = c;
                break;
              }
              if (!keep_searching)
                break;
            }
            if (last_candidate_index != -1)
            {
              auto copit = this->_operations.get<1>().find(last_candidate_index);
              auto& cop = *copit;
              auto cr = make_merge_conflict_resolver(
                    std::move(elle::unconst(cop.resolver)),
                    std::move(op.resolver),
                    last_candidate_order.second);
              // We will use the last block and the merged conflict resolver
              // for our new squashed operation.
              // But we must also set version to the first operation's version
              // or we will miss some conflicts.
              ACB* acb = dynamic_cast<ACB*>(op.block.get());
              if (!acb)
                acb = dynamic_cast<ACB*>(op.remove_signature.block.get());
              ELLE_DEBUG("Changing %s version to %s", acb, cop.version);
              if (acb)
                acb->seal(cop.version);
              else
                ELLE_WARN("Unable to reset block version (got %s (from %s)",
                          acb, op);
              if (last_candidate_order.first == Squash::at_first_position_stop
                || last_candidate_order.first == Squash::at_first_position_continue)
              {
                ELLE_DEBUG("overwriting op at %s", cop.index);
                  this->_operations.get<1>().modify(
                    copit, [&](Op& o) {
                      o.block = std::move(op.block);
                      o.mode = std::move(op.mode);
                      o.remove_signature = std::move(op.remove_signature);
                      o.resolver = std::move(cr);
                    });
                  if (!this->_journal_dir.empty())
                  {
                    auto path =
                      bfs::path(_journal_dir) / std::to_string(last_candidate_index);
                    bfs::ofstream os(path, std::ios::binary);
                    elle::serialization::binary::SerializerOut sout(os);
                    sout.set_context(ACBDontWaitForSignature{});
                    sout.set_context(OKBDontWaitForSignature{});
                    sout.serialize_forward(*copit);
                  }
                  return;
              }
              else
              { // at_last_position
                op.resolver = std::move(cr);
                int idx = last_candidate_index;
                int lastidx = this->_operations.get<1>().rbegin()->index;
                ELLE_DEBUG("Erasing op at %s", last_candidate_index);
                this->_operations.get<1>().erase(last_candidate_index);
                if (!this->_journal_dir.empty())
                {
                  auto path = bfs::path(this->_journal_dir) /
                  std::to_string(idx);
                  bfs::remove(path);
                }
                if (this->_first_disk_index
                  && this->_first_disk_index.get() == idx)
                {
                  if (++*this->_first_disk_index > lastidx)
                    this->_first_disk_index.reset();
                }
                // go on to regular push_op
              }
            }
          }
          bool reentered = this->_in_push;
          auto in_push = elle::scoped_assignment(this->_in_push, true);
          op.index = ++this->_next_index;
          ELLE_TRACE_SCOPE("%s: push %s", *this, op);
          if (!this->_journal_dir.empty())
          {
            auto path =
              bfs::path(_journal_dir) / std::to_string(op.index);
            bfs::ofstream os(path, std::ios::binary);
            elle::serialization::binary::SerializerOut sout(os);
            sout.set_context(ACBDontWaitForSignature{});
            sout.set_context(OKBDontWaitForSignature{});
            sout.serialize_forward(op);
          }
          if (reentered)
          {
            this->_reentered_ops.emplace_back(std::move(op));
            return;
          }
          auto queue = [this](Op op)
          {
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
          };
          queue(std::move(op));
          for (auto& op: this->_reentered_ops)
            queue(std::move(op));
          this->_reentered_ops.clear();
        }

        void
        Async::_store(std::unique_ptr<blocks::Block> block,
                      StoreMode mode,
                      std::unique_ptr<ConflictResolver> resolver)
        {
          elle::reactor::wait(this->_init_barrier);
          this->_queue.open();
          this->_push_op(
            Op(block->address(), std::move(block), mode, std::move(resolver)));
        }

        void
        Async::_remove(Address address, blocks::RemoveSignature rs)
        {
          elle::reactor::wait(this->_init_barrier);
          this->_queue.open();
          this->_push_op(Op(address, nullptr, {}, {}, std::move(rs)));
        }

        void
        Async::_fetch(std::vector<AddressVersion> const& addresses,
                      std::function<void(Address, std::unique_ptr<blocks::Block>,
                                         std::exception_ptr)> res)
        {
          // Do not deadlock from init_thread.
          if (this->_init_thread && !this->_init_thread->done() &&
              elle::reactor::scheduler().current() != this->_init_thread.get())
            elle::reactor::wait(this->_init_barrier);
          this->_queue.open();
          std::vector<AddressVersion> remain;
          for (auto addr: addresses)
          {
            bool hit = false;
            try
            {
              auto block = this->_fetch_cache(addr.first, addr.second, hit);
              if (hit)
                res(addr.first, std::move(block), {});
              else
                remain.push_back(addr);
            }
            catch (MissingBlock const& mb)
            {
              res(addr.first, {}, std::current_exception());
            }
          }
          this->_backend->fetch(remain, res);
        }

        std::unique_ptr<blocks::Block>
        Async::_fetch(Address address, boost::optional<int> local_version)
        {
          // Do not deadlock from init_thread.
          if (this->_init_thread && !this->_init_thread->done() &&
              elle::reactor::scheduler().current() != this->_init_thread.get())
            elle::reactor::wait(this->_init_barrier);
          this->_queue.open();

          bool hit = false;
          auto block = this->_fetch_cache(address, local_version, hit);
          if (hit)
            return block;
          else
            return this->_backend->fetch(address, local_version);
        }

        // Fetch operation must be synchronous, else the consistency is not
        // preserved.
        std::unique_ptr<blocks::Block>
        Async::_fetch_cache(Address address, boost::optional<int> local_version,
                            bool& hit)
        {
          hit = false;
          auto its = this->_operations.equal_range(address);
          auto it = std::max_element(its.first, its.second,
            [](auto const& e1, auto const& e2) {
              return e1.index < e2.index;
            });
          if (it != this->_operations.end())
          {
            hit = true;
            if (it->block)
            {
              ELLE_TRACE("%s: fetch %f from memory queue", *this, address);
              if (local_version)
                if (auto m = dynamic_cast<blocks::MutableBlock*>(
                      it->block.get()))
                  if (m->version() == *local_version)
                    return nullptr;
              return it->block->clone();
            }
            else
            {
              ELLE_TRACE("%s: fetch %f from disk journal at %s", *this, address, it->index);
              auto res = this->_load_op(it->index).block;
              if (!res)
                throw MissingBlock(address);
              return res;
            }
          }
          return {};
        }

        void
        Async::_process_loop()
        {
          elle::reactor::wait(this->_init_barrier);
          while (!this->_exit_requested)
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
              if (this->_exit_requested)
                break;
              auto it = this->_operations.get<1>().begin();
              while (it == this->_operations.get<1>().end() || it->index > index)
              {
                ELLE_DEBUG("index %s in queue not in ops (have %s)", index,
                           it->index);
                index = this->_queue.get();
                it = this->_operations.get<1>().begin();
              }

              elle::generic_unique_ptr<Op const> op(&*it, [] (Op const*) {});
              ELLE_ASSERT_EQ(op->index, index);
              this->_process_operation(std::move(op));
              if (!this->_journal_dir.empty())
              {
                auto path = bfs::path(this->_journal_dir) / std::to_string(index);
                bfs::remove(path);
                this->_last_processed_index = index;
              }
              this->_operations.get<1>().erase(it);
            }
            catch (elle::Error const& e)
            {
              ELLE_ABORT("%s: async loop killed: %s\n",
                         this, e.what(), e.backtrace());
            }
          }
          ELLE_TRACE("exiting loop");
        }

        void
        Async::_process_operation(elle::generic_unique_ptr<Op const> op)
        {
          ++_processed_op_count;
          static const int delay = std::stoi(elle::os::getenv("INFINIT_ASYNC_POP_DELAY", "0"));
          if (delay)
            elle::reactor::sleep(boost::posix_time::milliseconds(delay));
          Address addr = op->address;
          boost::optional<StoreMode> mode = op->mode;
          int attempt = 0;
          while (true)
          {
            try
            {
              ELLE_TRACE_SCOPE("process %s", op);
              if (!mode)
                try
                {
                  this->_backend->remove(addr, op->remove_signature);
                }
                catch (MissingBlock const&)
                {
                  // Nothing: block was already removed.
                }
              else
              {
#ifndef __clang__
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

                this->_backend->store(
                  std::move(elle::unconst(op.get())->block),
                  *mode,
                  std::move(elle::unconst(op.get())->resolver));
#ifndef __clang__
# pragma GCC diagnostic pop
#endif
              }
              break;
            }
            catch (silo::Collision const& c)
            {
              // check for idempotence
              try
              {
                auto block = this->_backend->fetch(op->address);
                // op->block was moved, reload
                auto o = this->_load_op(op->index);
                if (block->blocks::Block::data()
                  == o.block->blocks::Block::data())
                {
                  ELLE_LOG("skip idempotent replay %s", op->index);
                  break;
                }
              }
              catch (elle::Error const& e)
              {
                ELLE_LOG("error in async loop rechecking %s: %s", op->index, e);
              }
            }
            catch (elle::Error const& e)
            {
              ELLE_LOG("error in async loop on %s: %s, from:\n%s",
                       op->index, e, e.backtrace());
            }
            // If we land here (no break) an error occurred
            {
              ++attempt;
              auto delay = std::min(
                20000_ms,
                boost::posix_time::milliseconds(200 * attempt));
              ELLE_DEBUG("wait %s before retrying", delay)
                elle::reactor::sleep(delay);
            }
            // reload block and try again
            auto index = op->index;
            ELLE_TRACE("reload operation %s", index)
              try
              {
                op = elle::generic_unique_ptr<Op const>(
                  new Op(this->_load_op(index)));
              }
              catch (elle::Error const& e)
              {
                ELLE_WARN("%s: failed to reload %s: %s",
                          this, index, e);
                break;
              }
          }
        }

        /*-----------.
        | Monitoring |
        `-----------*/

        elle::json::Object
        Async::redundancy()
        {
          return this->_backend->redundancy();
        }

        elle::json::Object
        Async::stats()
        {
          return this->_backend->stats();
        }

        /*----------.
        | Operation |
        `----------*/

        Async::Op::Op(Address address_,
                      std::unique_ptr<blocks::Block>&& block_,
                      boost::optional<StoreMode> mode_,
                      std::unique_ptr<ConflictResolver> resolver_,
                      blocks::RemoveSignature remove_signature_)
          : address(address_)
          , block(std::move(block_))
          , mode(std::move(mode_))
          , resolver(std::move(resolver_))
          , remove_signature(remove_signature_)
          , version(-1)
        {
          if (auto mb = dynamic_cast<blocks::MutableBlock*>(block.get()))
            version = mb->version();
          else if (auto mb = dynamic_cast<blocks::MutableBlock*>(
            remove_signature.block.get()))
            version = mb->version();
        }

        Async::Op::Op(Op&& b)
        : address(b.address)
        {
          *this = std::move(b);
        }

        void
        Async::Op::operator=(Op&& b)
        {
          address = b.address;
          block = std::move(b.block);
          mode = b.mode;
          resolver = std::move(b.resolver);
          remove_signature = std::move(b.remove_signature);
          index = b.index;
          if (auto mb = dynamic_cast<blocks::MutableBlock*>(block.get()))
            version = mb->version();
          else if (auto mb = dynamic_cast<blocks::MutableBlock*>(
            remove_signature.block.get()))
            version = mb->version();
        }

        void
        Async::print_queue()
        {
          auto& indexed = this->_operations.get<1>();
          for (auto const& op: indexed)
          {
            ELLE_LOG("%s: %s", op.index, op.resolver->description());
          }
        }

        std::ostream&
        operator <<(std::ostream& o, Async::Op const& op)
        {
          if (op.mode)
            elle::fprintf(o, "Op::store(%s, %s)", op.index, op.block);
          else
            elle::fprintf(o, "Op::remove(%s)", op.index);
          return o;
        }
      }
    }
  }
}
