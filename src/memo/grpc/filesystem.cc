#ifdef MEMO_WINDOWS
# include <fcntl.h>
#endif

#include <sys/stat.h> // S_IMFT...

#include <elle/utils.hh>

#include <elle/reactor/filesystem.hh>
#include <elle/reactor/scheduler.hh>

#include <memo/grpc/fs.grpc.pb.h>

ELLE_LOG_COMPONENT("memo.grpc.filesystem");

namespace memo
{
  namespace grpc
  {
    class FSImpl: public ::FileSystem::Service
    {
    public:
      using Status = ::grpc::Status;
      using Ctx = ::grpc::ServerContext;
      FSImpl(elle::reactor::filesystem::FileSystem& fs);
      Status
      MkDir(Ctx*, const ::Path* request, ::FsStatus* response);
      Status
      ListDir(Ctx*, const ::Path* request, ::DirectoryContent* response);
      Status
      RmDir(Ctx*, const ::Path* request, ::FsStatus* response);
      Status
      RMFile(Ctx*, const ::Path* request, ::FsStatus* response);
      Status
      OpenFile(Ctx*, const ::Path* request, ::StatusHandle* response);
      Status
      CloseFile(Ctx*, const ::Handle* request, ::FsStatus* response);
      Status
      Read(Ctx*, const ::HandleRange* request, ::StatusBuffer* response);
      Status
      Write(Ctx*, const ::HandleBuffer* request, ::FsStatus* response);
      Status
      ReadStream(Ctx*, const ::HandleRange* request, ::grpc::ServerWriter<::StatusBuffer>* writer);
      Status
      WriteStream(Ctx*, ::grpc::ServerReader<::HandleBuffer>* reader, ::FsStatus* response);
    private:
      void
      managed(const char* name, ::FsStatus& status, std::function<void()> f);
      elle::reactor::Scheduler& _sched;
      elle::reactor::filesystem::FileSystem& _fs;
      using Handles =
        std::unordered_map<std::string,
                           std::unique_ptr<elle::reactor::filesystem::Handle>>;
      Handles _handles;
      int _next_handle;
    };

    FSImpl::FSImpl(elle::reactor::filesystem::FileSystem& fs)
      : _sched(elle::reactor::scheduler())
      , _fs(fs)
      , _next_handle(0)
    {}

    void
    FSImpl::managed(const char* name, ::FsStatus& status, std::function<void()> f)
    {
      _sched.mt_run<void>(name, [&] {
          try
          {
            f();
          }
          catch (elle::reactor::filesystem::Error const& e)
          {
            ELLE_TRACE("filesystem error: %s", e);
            status.set_code(e.error_code());
            status.set_message(e.what());
          }
          catch (elle::Error const& e)
          {
            ELLE_TRACE("filesystem error: %s", e);
            status.set_code(1);
            status.set_message(e.what());
          }
      });
    }

    ::grpc::Status
    FSImpl::MkDir(Ctx*, const ::Path* request, ::FsStatus* response)
    {
      managed("MkDir", *response, [&] {
          _fs.path(request->path())->mkdir(0644);
      });
      return Status::OK;
    }

    ::grpc::Status
    FSImpl::ListDir(Ctx*, const ::Path* request, ::DirectoryContent* response)
    {
      managed("ListDir", *response->mutable_status(), [&] {
          _fs.path(request->path())->list_directory(
            [&](std::string const& name, struct stat* st) {
              auto ent = response->add_content();
              ent->set_name(name);
              ent->set_type(S_ISDIR(st->st_mode) ? ENTRY_DIRECTORY : ENTRY_FILE);
          });
      });
      return Status::OK;
    }

    ::grpc::Status
    FSImpl::RmDir(Ctx*, const ::Path* request, ::FsStatus* response)
    {
      managed("RmDir", *response, [&] {
          _fs.path(request->path())->rmdir();
      });
      return Status::OK;
    }

    ::grpc::Status
    FSImpl::RMFile(Ctx*, const ::Path* request, ::FsStatus* response)
    {
      managed("RMFile", *response, [&] {
          _fs.path(request->path())->unlink();
      });
      return Status::OK;
    }

    ::grpc::Status
    FSImpl::OpenFile(Ctx*, const ::Path* request, ::StatusHandle* response)
    {
      managed("OpenFile", *response->mutable_status(), [&] {
          auto handle = _fs.path(request->path())->create(O_CREAT|O_RDWR, 0644);
          auto id = std::to_string(++_next_handle);
          _handles[id] = std::move(handle);
          response->mutable_handle()->set_handle(id);
      });
      return Status::OK;
    }

    ::grpc::Status
    FSImpl::CloseFile(Ctx*, const ::Handle* request, ::FsStatus* response)
    {
      managed("CloseFile", *response, [&] {
          auto it = _handles.find(request->handle());
          if (it == _handles.end())
            throw elle::reactor::filesystem::Error(EBADF, "bad handle");
          it->second->close();
          _handles.erase(it);
      });
      return Status::OK;
    }

    ::grpc::Status
    FSImpl::Read(Ctx*, const ::HandleRange* request, ::StatusBuffer* response)
    {
      managed("Read", *response->mutable_status(), [&] {
          auto it = _handles.find(request->handle().handle());
          if (it == _handles.end())
            throw elle::reactor::filesystem::Error(EBADF, "bad handle");
          auto* buf = response->mutable_buffer()->mutable_data();
          buf->resize(request->range().size());
          int sz = it->second->read(
            elle::WeakBuffer(elle::unconst(buf->data()), buf->size()),
            buf->size(),
            request->range().offset());
          buf->resize(sz);
          response->mutable_buffer()->set_offset(request->range().offset());
      });
      return Status::OK;
    }

    ::grpc::Status
    FSImpl::Write(Ctx*, const ::HandleBuffer* request, ::FsStatus* response)
    {
      managed("Write", *response, [&] {
          auto it = _handles.find(request->handle().handle());
          if (it == _handles.end())
            throw elle::reactor::filesystem::Error(EBADF, "bad handle");
          it->second->write(
            elle::ConstWeakBuffer(request->buffer().data().data(),
                                  request->buffer().data().size()),
            request->buffer().data().size(),
            request->buffer().offset());
      });
      return Status::OK;
    }

    ::grpc::Status
    FSImpl::ReadStream(Ctx*, const ::HandleRange* request, ::grpc::ServerWriter<::StatusBuffer>* writer)
    {
      static const int64_t chunk = 1 << 19; // 512K
      ::FsStatus status;
      managed("ReadStream", status, [&] {
          auto it = _handles.find(request->handle().handle());
          if (it == _handles.end())
            throw elle::reactor::filesystem::Error(EBADF, "bad handle");
          auto* handle = it->second.get();
          auto start = request->range().offset();
          auto size = request->range().size();
          auto pos = start;
          ::StatusBuffer sb;
          sb.mutable_buffer()->mutable_data()->resize(chunk);
          while (size < 0 || pos < start + size)
          {
            auto sz = chunk;
            if (size >= 0)
              sz = std::min(sz, start + size - pos);
            auto r = handle->read(elle::WeakBuffer(
               elle::unconst(sb.mutable_buffer()->mutable_data()->data()),
               sz), sz, pos);
            sb.mutable_buffer()->set_offset(pos);
            sb.mutable_buffer()->mutable_data()->resize(r);
            ELLE_DEBUG("reading chunk at %s of size %s",  pos, r);
            writer->Write(sb);
            pos += r;
            if (r != sz)
              break;
          }
      });
      if (status.code() != 0)
      {
        ::StatusBuffer sb;
        sb.mutable_status()->CopyFrom(status);
        writer->Write(sb);
      }
      return Status::OK;
    }

    ::grpc::Status
    FSImpl::WriteStream(Ctx*, ::grpc::ServerReader< ::HandleBuffer>* reader, ::FsStatus* response)
    {
      managed("WriteStream", *response, [&] {
          std::string handle_str;
          elle::reactor::filesystem::Handle* handle = nullptr;
          ::HandleBuffer hb;
          while (reader->Read(&hb))
          {
            if (!handle)
            {
              handle_str = hb.handle().handle();
              auto it = _handles.find(handle_str);
              if (it == _handles.end())
                throw elle::reactor::filesystem::Error(EBADF, "bad handle");
              handle = it->second.get();
            }
            else if (handle_str !=  hb.handle().handle())
              throw elle::reactor::filesystem::Error(EBADF, "handle does not match");
            ELLE_DEBUG("writing chunk at %s",  hb.buffer().offset());
            handle->write(
              elle::ConstWeakBuffer(hb.buffer().data().data(),
                                    hb.buffer().data().size()),
              hb.buffer().data().size(),
              hb.buffer().offset());
          }
      });
      return Status::OK;
    }

    std::unique_ptr< ::grpc::Service>
    filesystem_service(elle::reactor::filesystem::FileSystem& fs)
    {
      return std::make_unique<FSImpl>(fs);
    }
  }
}
